/**
 * @file processing_strategy.cpp
 * @brief AUTO mode processing strategy implementation
 *
 * Processes LDIF and Master List files in one go:
 * Parse → Validate → Save to DB → Save to LDAP (if available)
 *
 * LDAP is optional: if unavailable, certificates are saved with stored_in_ldap=FALSE
 * and the reconciliation engine syncs them to LDAP later.
 */

#include "processing_strategy.h"
#include "ldif_processor.h"
#include "common.h"
#include "common/masterlist_processor.h"
#include "common/progress_manager.h"
#include "domain/models/validation_statistics.h"
#include "infrastructure/service_container.h"
#include "services/ldap_storage_service.h"
#include "services/validation_service.h"
#include "repositories/upload_repository.h"
#include "query_helpers.h"
#include "repositories/validation_repository.h"
#include "i_query_executor.h"
#include <drogon/HttpTypes.h>
#include <json/json.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <ldap.h>

using common::ValidationStatistics;
using common::CertificateMetadata;
using common::IcaoComplianceStatus;
using common::ProcessingStage;

// Global service container (defined in main.cpp)
extern infrastructure::ServiceContainer* g_services;

void AutoProcessingStrategy::processLdifEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing {} LDIF entries for upload {}", entries.size(), uploadId);

    if (!g_services) {
        spdlog::error("ServiceContainer not initialized — cannot process LDIF entries");
        throw std::runtime_error("ServiceContainer not initialized");
    }

    ValidationStats stats;  // Existing validation statistics (legacy)
    common::ValidationStatistics enhancedStats{};  // Enhanced statistics with metadata tracking

    // Pre-scan entries to calculate total counts for "X/Total" progress display
    LdifProcessor::TotalCounts totalCounts;
    for (const auto& entry : entries) {
        if (entry.hasAttribute("userCertificate;binary") || entry.hasAttribute("cACertificate;binary")) {
            totalCounts.totalCerts++;
        }
        if (entry.hasAttribute("certificateRevocationList;binary")) {
            totalCounts.totalCrl++;
        }
        if (entry.hasAttribute("pkdMasterListContent;binary") || entry.hasAttribute("pkdMasterListContent")) {
            totalCounts.totalMl++;
        }
    }
    spdlog::info("AUTO mode: Pre-scan complete - {} certs, {} CRLs, {} MLs",
                totalCounts.totalCerts, totalCounts.totalCrl, totalCounts.totalMl);

    // Process all entries (save to DB, validate, upload to LDAP) with total counts for progress display
    auto counts = LdifProcessor::processEntries(uploadId, entries, ld, stats, enhancedStats, &totalCounts);

    // If duplicates were skipped (resume mode), recalculate statistics from DB for accuracy.
    // The in-memory counts only reflect newly processed certificates, not previously processed ones.
    bool hasSkippedDuplicates = (enhancedStats.duplicateCount > 0);

    if (hasSkippedDuplicates && g_services->queryExecutor()) {
        spdlog::info("Resume mode: {} duplicates skipped — recalculating statistics from DB", enhancedStats.duplicateCount);

        try {
            // Recalculate certificate counts by type from DB
            auto certCounts = g_services->queryExecutor()->executeQuery(
                "SELECT certificate_type, COUNT(*) as cnt FROM certificate WHERE upload_id = $1 GROUP BY certificate_type",
                {uploadId});

            int dbCscaCount = 0, dbDscCount = 0, dbDscNcCount = 0;
            for (const auto& row : certCounts) {
                std::string type = row["certificate_type"].asString();
                int cnt = common::db::scalarToInt(row["cnt"]);
                if (type == "CSCA") dbCscaCount = cnt;
                else if (type == "DSC") dbDscCount = cnt;
                else if (type == "DSC_NC") dbDscNcCount = cnt;
            }

            // Recalculate CRL count from DB
            auto crlCountResult = g_services->queryExecutor()->executeScalar(
                "SELECT COUNT(*) FROM crl WHERE upload_id = $1", {uploadId});
            int dbCrlCount = common::db::scalarToInt(crlCountResult);

            counts.cscaCount = dbCscaCount;
            counts.dscCount = dbDscCount;
            counts.dscNcCount = dbDscNcCount;
            counts.crlCount = dbCrlCount;

            spdlog::info("Resume mode: DB counts — CSCA={}, DSC={}, DSC_NC={}, CRL={}",
                        dbCscaCount, dbDscCount, dbDscNcCount, dbCrlCount);

            // Recalculate validation statistics from DB
            auto valCounts = g_services->queryExecutor()->executeQuery(
                "SELECT validation_status, COUNT(*) as cnt FROM validation_result WHERE upload_id = $1 GROUP BY validation_status",
                {uploadId});

            enhancedStats.validCount = 0;
            enhancedStats.invalidCount = 0;
            enhancedStats.pendingCount = 0;
            enhancedStats.expiredValidCount = 0;
            for (const auto& row : valCounts) {
                std::string status = row["validation_status"].asString();
                int cnt = common::db::scalarToInt(row["cnt"]);
                if (status == "VALID") enhancedStats.validCount = cnt;
                else if (status == "INVALID") enhancedStats.invalidCount = cnt;
                else if (status == "PENDING") enhancedStats.pendingCount = cnt;
                else if (status == "EXPIRED_VALID") enhancedStats.expiredValidCount = cnt;
            }

            // Recalculate trust chain and ICAO compliance counts
            auto tcValid = g_services->queryExecutor()->executeScalar(
                "SELECT COUNT(*) FROM validation_result WHERE upload_id = $1 AND trust_chain_valid = TRUE", {uploadId});
            enhancedStats.trustChainValidCount = common::db::scalarToInt(tcValid);

            auto cscaNotFound = g_services->queryExecutor()->executeScalar(
                "SELECT COUNT(*) FROM validation_result WHERE upload_id = $1 AND validation_status = 'PENDING' AND csca_found = FALSE", {uploadId});
            enhancedStats.cscaNotFoundCount = common::db::scalarToInt(cscaNotFound);

            enhancedStats.trustChainInvalidCount = enhancedStats.invalidCount;

            auto icaoCompliant = g_services->queryExecutor()->executeScalar(
                "SELECT COUNT(*) FROM validation_result WHERE upload_id = $1 AND icao_compliant = TRUE", {uploadId});
            enhancedStats.icaoCompliantCount = common::db::scalarToInt(icaoCompliant);

            auto icaoNonCompliant = g_services->queryExecutor()->executeScalar(
                "SELECT COUNT(*) FROM validation_result WHERE upload_id = $1 AND icao_compliant = FALSE", {uploadId});
            enhancedStats.icaoNonCompliantCount = common::db::scalarToInt(icaoNonCompliant);

            spdlog::info("Resume mode: Validation counts — valid={}, invalid={}, pending={}, expired_valid={}",
                        enhancedStats.validCount, enhancedStats.invalidCount,
                        enhancedStats.pendingCount, enhancedStats.expiredValidCount);
        } catch (const std::exception& e) {
            spdlog::warn("Resume mode: DB statistics recalculation failed (using in-memory counts): {}", e.what());
        }
    }

    // Update database statistics
    int totalItems = counts.cscaCount + counts.dscCount + counts.dscNcCount + counts.crlCount + counts.mlCount;
    common::updateUploadStatistics(uploadId, "COMPLETED",
                          counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
                          entries.size(), entries.size(), "");

    // Update validation statistics via ValidationRepository
    if (g_services->validationRepository()) {
        domain::models::ValidationStatistics valStats;
        valStats.validCount = enhancedStats.validCount;
        valStats.invalidCount = enhancedStats.invalidCount;
        valStats.pendingCount = enhancedStats.pendingCount;
        valStats.errorCount = enhancedStats.totalErrorCount;
        valStats.trustChainValidCount = enhancedStats.trustChainValidCount;
        valStats.trustChainInvalidCount = enhancedStats.trustChainInvalidCount;
        valStats.cscaNotFoundCount = enhancedStats.cscaNotFoundCount;
        valStats.expiredCount = enhancedStats.expiredCount;
        valStats.validPeriodCount = enhancedStats.validPeriodCount;
        valStats.revokedCount = enhancedStats.revokedCount;
        valStats.expiredValidCount = enhancedStats.expiredValidCount;
        valStats.icaoCompliantCount = enhancedStats.icaoCompliantCount;
        valStats.icaoNonCompliantCount = enhancedStats.icaoNonCompliantCount;
        valStats.icaoWarningCount = enhancedStats.icaoWarningCount;
        g_services->validationRepository()->updateStatistics(uploadId, valStats);
    }

    // Update ML and MLSC counts via repository
    if ((counts.mlCount > 0 || counts.mlscCount > 0) && g_services->uploadRepository()) {
        g_services->uploadRepository()->updateStatistics(uploadId,
            counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
            counts.mlscCount, counts.mlCount);
    }

    spdlog::info("AUTO mode: Completed - CSCA: {}, DSC: {}, DSC_NC: {}, CRL: {}, ML: {}, MLSC: {}, LDAP: {} certs, {} CRLs, {} MLs",
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount, counts.mlscCount,
                counts.ldapCertStoredCount, counts.ldapCrlStoredCount, counts.ldapMlStoredCount);
    spdlog::info("AUTO mode: Validation - {} valid, {} invalid, {} pending, {} CSCA not found, {} expired",
                enhancedStats.validCount, enhancedStats.invalidCount, enhancedStats.pendingCount, enhancedStats.cscaNotFoundCount, enhancedStats.expiredCount);

    // Auto re-validate PENDING DSCs when new CSCAs were uploaded
    if (!counts.newCscaCountries.empty() && g_services->validationService()) {
        spdlog::info("AUTO mode: {} new CSCA countries detected — triggering PENDING DSC re-validation",
                     counts.newCscaCountries.size());
        try {
            auto revalResult = g_services->validationService()->revalidatePendingDscForCountries(
                counts.newCscaCountries);
            if (revalResult.success && revalResult.totalProcessed > 0) {
                spdlog::info("AUTO mode: PENDING DSC re-validation complete — {} processed, {} valid, {} still pending ({}s)",
                    revalResult.totalProcessed, revalResult.validCount, revalResult.pendingCount,
                    revalResult.durationSeconds);
                // Update validation stats with re-validated results
                enhancedStats.validCount += revalResult.validCount;
                enhancedStats.expiredValidCount += revalResult.expiredValidCount;
                enhancedStats.pendingCount -= (revalResult.validCount + revalResult.invalidCount + revalResult.expiredValidCount);
                if (enhancedStats.pendingCount < 0) enhancedStats.pendingCount = 0;
                enhancedStats.invalidCount += revalResult.invalidCount;

                // Re-update validation statistics in DB
                if (g_services->validationRepository()) {
                    domain::models::ValidationStatistics valStats;
                    valStats.validCount = enhancedStats.validCount;
                    valStats.invalidCount = enhancedStats.invalidCount;
                    valStats.pendingCount = enhancedStats.pendingCount;
                    valStats.expiredValidCount = enhancedStats.expiredValidCount;
                    valStats.errorCount = enhancedStats.totalErrorCount;
                    valStats.trustChainValidCount = enhancedStats.trustChainValidCount + revalResult.validCount;
                    valStats.trustChainInvalidCount = enhancedStats.trustChainInvalidCount + revalResult.invalidCount;
                    valStats.cscaNotFoundCount = enhancedStats.cscaNotFoundCount - (revalResult.validCount + revalResult.invalidCount + revalResult.expiredValidCount);
                    if (valStats.cscaNotFoundCount < 0) valStats.cscaNotFoundCount = 0;
                    valStats.expiredCount = enhancedStats.expiredCount;
                    valStats.validPeriodCount = enhancedStats.validPeriodCount;
                    valStats.revokedCount = enhancedStats.revokedCount;
                    valStats.icaoCompliantCount = enhancedStats.icaoCompliantCount;
                    valStats.icaoNonCompliantCount = enhancedStats.icaoNonCompliantCount;
                    valStats.icaoWarningCount = enhancedStats.icaoWarningCount;
                    g_services->validationRepository()->updateStatistics(uploadId, valStats);
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("AUTO mode: PENDING DSC re-validation failed (non-critical): {}", e.what());
        }
    }

    // Send completion progress to frontend
    std::string completionMsg = "처리 완료: ";
    std::vector<std::string> parts;
    if (counts.cscaCount > 0) parts.push_back("CSCA " + std::to_string(counts.cscaCount) + "개");
    if (counts.dscCount > 0) parts.push_back("DSC " + std::to_string(counts.dscCount) + "개");
    if (counts.dscNcCount > 0) parts.push_back("DSC_NC " + std::to_string(counts.dscNcCount) + "개");
    if (counts.crlCount > 0) parts.push_back("CRL " + std::to_string(counts.crlCount) + "개");
    if (counts.mlCount > 0) parts.push_back("ML " + std::to_string(counts.mlCount) + "개");

    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) completionMsg += ", ";
        completionMsg += parts[i];
    }

    completionMsg += " (검증: " + std::to_string(stats.validCount) + " 성공, " +
                    std::to_string(stats.invalidCount) + " 실패, " +
                    std::to_string(stats.pendingCount) + " 보류)";

    common::sendCompletionProgress(uploadId, totalItems, completionMsg);
}

void AutoProcessingStrategy::processMasterListContent(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing Master List ({} bytes) for upload {}", content.size(), uploadId);

    MasterListStats stats;
    common::ValidationStatistics enhancedStats{};
    bool success = processMasterListFile(ld, uploadId, content, stats, &enhancedStats);

    if (!success) {
        if (enhancedStats.totalErrorCount > 0) {
            spdlog::warn("AUTO mode: Master List processing failed with {} errors", enhancedStats.totalErrorCount);
        }
        throw std::runtime_error("Failed to process Master List file");
    }

    if (enhancedStats.totalErrorCount > 0) {
        spdlog::warn("AUTO mode: Master List processing completed with {} errors (parse: {}, db: {}, ldap: {})",
                    enhancedStats.totalErrorCount, enhancedStats.parseErrorCount,
                    enhancedStats.dbSaveErrorCount, enhancedStats.ldapSaveErrorCount);
    }

    spdlog::info("AUTO mode: Master List processing completed - {} MLSC, {} CSCA/LC extracted ({} new, {} duplicate)",
                stats.mlCount, stats.cscaExtractedCount, stats.cscaNewCount, stats.cscaDuplicateCount);

    // Update uploaded_file table with final statistics
    common::updateUploadStatistics(uploadId, "COMPLETED",
                          stats.cscaNewCount, 0, 0, 0, 0, 0, "");

    // Update all statistics via repository
    if (g_services->uploadRepository()) {
        g_services->uploadRepository()->updateStatistics(uploadId,
            stats.cscaNewCount, 0, 0, 0,
            stats.mlscCount, stats.mlCount);
        g_services->uploadRepository()->updateProgress(uploadId,
            stats.cscaExtractedCount, stats.cscaNewCount);
    }

    // Save validation statistics from enhancedStats
    if (g_services->validationRepository()) {
        domain::models::ValidationStatistics valStats;
        valStats.validCount = enhancedStats.validCount;
        valStats.invalidCount = enhancedStats.invalidCount;
        valStats.pendingCount = enhancedStats.pendingCount;
        valStats.errorCount = enhancedStats.totalErrorCount;
        valStats.trustChainValidCount = enhancedStats.trustChainValidCount;
        valStats.trustChainInvalidCount = enhancedStats.trustChainInvalidCount;
        valStats.cscaNotFoundCount = enhancedStats.cscaNotFoundCount;
        valStats.expiredCount = enhancedStats.expiredCount;
        valStats.validPeriodCount = enhancedStats.validPeriodCount;
        valStats.revokedCount = enhancedStats.revokedCount;
        valStats.expiredValidCount = enhancedStats.expiredValidCount;
        valStats.icaoCompliantCount = enhancedStats.icaoCompliantCount;
        valStats.icaoNonCompliantCount = enhancedStats.icaoNonCompliantCount;
        valStats.icaoWarningCount = enhancedStats.icaoWarningCount;
        g_services->validationRepository()->updateStatistics(uploadId, valStats);
    }

    spdlog::info("AUTO mode: Statistics updated - status=COMPLETED, csca_count={}, mlsc_count={}, total_entries={}, processed_entries={}",
                stats.cscaNewCount, stats.mlscCount, stats.cscaExtractedCount, stats.cscaNewCount);
    spdlog::info("AUTO mode: Validation - {} valid, {} invalid, {} pending, {} expired",
                enhancedStats.validCount, enhancedStats.invalidCount, enhancedStats.pendingCount, enhancedStats.expiredCount);

    // Auto re-validate PENDING DSCs when new CSCAs were extracted from Master List
    if (stats.cscaNewCount > 0 && g_services->validationService()) {
        spdlog::info("AUTO mode: {} new CSCAs from Master List — triggering full PENDING DSC re-validation",
                     stats.cscaNewCount);
        try {
            auto revalResult = g_services->validationService()->revalidateDscCertificates();
            if (revalResult.success && revalResult.totalProcessed > 0) {
                spdlog::info("AUTO mode: PENDING DSC re-validation complete — {} processed, {} valid, {} still pending ({}s)",
                    revalResult.totalProcessed, revalResult.validCount, revalResult.pendingCount,
                    revalResult.durationSeconds);
            }
        } catch (const std::exception& e) {
            spdlog::warn("AUTO mode: PENDING DSC re-validation failed (non-critical): {}", e.what());
        }
    }
}

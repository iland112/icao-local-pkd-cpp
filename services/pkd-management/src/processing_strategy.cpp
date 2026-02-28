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
#include "repositories/upload_repository.h"
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
}

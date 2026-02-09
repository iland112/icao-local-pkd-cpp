#include "ldif_processor.h"
#include "common.h"
#include "common/masterlist_processor.h"  // v2.0.7: For parseMasterListEntryV2
#include <spdlog/spdlog.h>
#include <libpq-fe.h>
#include <ldap.h>
#include <sstream>
#include <algorithm>

// Note: This file contains extracted logic from main.cpp
// The actual implementation will call existing functions from main.cpp
// until we fully extract all helper functions.

// Forward declarations of functions that still exist in main.cpp (Phase 6.1 - Repository Pattern)
// These will be moved here gradually
extern std::vector<LdifEntry> parseLdifContent(const std::string& content);
extern bool parseCertificateEntry(LDAP* ld, const std::string& uploadId,
                                  const LdifEntry& entry, const std::string& attrName,
                                  int& cscaCount, int& dscCount, int& dscNcCount,
                                  int& ldapStoredCount, ValidationStats& validationStats,
                                  common::ValidationStatistics& enhancedStats);
extern bool parseCrlEntry(LDAP* ld, const std::string& uploadId,
                         const LdifEntry& entry, int& crlCount, int& ldapCrlStoredCount);
extern bool parseMasterListEntry(LDAP* ld, const std::string& uploadId,
                                const LdifEntry& entry, int& mlCount, int& ldapMlStoredCount);

// Forward declaration for sendDbSavingProgress helper function (from main.cpp)
extern void sendDbSavingProgress(const std::string& uploadId, int processedCount, int totalCount, const std::string& message);

// Forward declaration for sendProgressWithMetadata helper function (from main.cpp) - Phase 4.4
extern void sendProgressWithMetadata(
    const std::string& uploadId,
    common::ProcessingStage stage,
    int processedCount,
    int totalCount,
    const std::string& message,
    const std::optional<common::CertificateMetadata>& metadata,
    const std::optional<common::IcaoComplianceStatus>& compliance,
    const std::optional<common::ValidationStatistics>& stats
);

std::vector<LdifEntry> LdifProcessor::parseLdifContent(const std::string& content) {
    // Call the existing function from main.cpp
    return ::parseLdifContent(content);
}

LdifProcessor::ProcessingCounts LdifProcessor::processEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    LDAP* ld,
    ValidationStats& stats,
    common::ValidationStatistics& enhancedStats,
    const TotalCounts* totalCounts
) {
    ProcessingCounts counts;
    int processedEntries = 0;
    int totalEntries = static_cast<int>(entries.size());

    spdlog::info("Processing {} LDIF entries for upload {}", totalEntries, uploadId);

    // Process each entry
    for (const auto& entry : entries) {
        try {
            // Check for userCertificate;binary
            if (entry.hasAttribute("userCertificate;binary")) {
                parseCertificateEntry(ld, uploadId, entry, "userCertificate;binary",
                                    counts.cscaCount, counts.dscCount, counts.dscNcCount,
                                    counts.ldapCertStoredCount, stats, enhancedStats);
            }
            // Check for cACertificate;binary
            else if (entry.hasAttribute("cACertificate;binary")) {
                parseCertificateEntry(ld, uploadId, entry, "cACertificate;binary",
                                    counts.cscaCount, counts.dscCount, counts.dscNcCount,
                                    counts.ldapCertStoredCount, stats, enhancedStats);
            }

            // Check for CRL
            if (entry.hasAttribute("certificateRevocationList;binary")) {
                parseCrlEntry(ld, uploadId, entry, counts.crlCount, counts.ldapCrlStoredCount);
            }

            // Check for Master List (v2.0.7: Use new CSCA extraction processor)
            if (entry.hasAttribute("pkdMasterListContent;binary") ||
                entry.hasAttribute("pkdMasterListContent")) {
                MasterListStats mlStats;
                parseMasterListEntryV2(ld, uploadId, entry, mlStats);
                // Track Master List file count (v2.1.1)
                counts.mlCount++;
                // Track MLSC count (v2.1.1)
                counts.mlscCount += mlStats.mlscCount;
                counts.ldapMlStoredCount += mlStats.ldapMlStoredCount;
                // Add extracted CSCAs to counts
                counts.cscaCount += mlStats.cscaNewCount;
                counts.ldapCertStoredCount += mlStats.ldapCscaStoredCount;
            }

        } catch (const std::exception& e) {
            spdlog::warn("Error processing entry {}: {}", entry.dn, e.what());
        }

        processedEntries++;

        // v1.5.2: Send progress update to frontend every 50 entries
        if (processedEntries % 50 == 0 || processedEntries == totalEntries) {
            // Build detailed progress message with X/Total format if totalCounts provided
            std::string progressMsg = "처리 중: ";
            std::vector<std::string> parts;

            int totalCerts = totalCounts ? totalCounts->totalCerts : 0;
            int totalCrl = totalCounts ? totalCounts->totalCrl : 0;
            int totalMl = totalCounts ? totalCounts->totalMl : 0;

            // v1.5.4: Show individual cert types (CSCA/DSC/DSC_NC) separately
            // Only display items with count > 0
            if (counts.cscaCount > 0) {
                if (totalCerts > 0) {
                    parts.push_back("CSCA " + std::to_string(counts.cscaCount) + "/" + std::to_string(totalCerts));
                } else {
                    parts.push_back("CSCA " + std::to_string(counts.cscaCount));
                }
            }

            if (counts.dscCount > 0) {
                if (totalCerts > 0) {
                    parts.push_back("DSC " + std::to_string(counts.dscCount) + "/" + std::to_string(totalCerts));
                } else {
                    parts.push_back("DSC " + std::to_string(counts.dscCount));
                }
            }

            if (counts.dscNcCount > 0) {
                if (totalCerts > 0) {
                    parts.push_back("DSC_NC " + std::to_string(counts.dscNcCount) + "/" + std::to_string(totalCerts));
                } else {
                    parts.push_back("DSC_NC " + std::to_string(counts.dscNcCount));
                }
            }

            if (counts.crlCount > 0) {
                if (totalCrl > 0) {
                    parts.push_back("CRL " + std::to_string(counts.crlCount) + "/" + std::to_string(totalCrl));
                } else {
                    parts.push_back("CRL " + std::to_string(counts.crlCount));
                }
            }

            if (counts.mlCount > 0) {
                if (totalMl > 0) {
                    parts.push_back("ML " + std::to_string(counts.mlCount) + "/" + std::to_string(totalMl));
                } else {
                    parts.push_back("ML " + std::to_string(counts.mlCount));
                }
            }

            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) progressMsg += ", ";
                progressMsg += parts[i];
            }

            // Phase 4.4: Update processed count in statistics
            enhancedStats.processedCount = counts.cscaCount + counts.dscCount + counts.dscNcCount;

            // Phase 4.4: Send enhanced progress with validation statistics via SSE
            sendProgressWithMetadata(
                uploadId,
                common::ProcessingStage::VALIDATION_IN_PROGRESS,
                processedEntries,
                totalEntries,
                progressMsg,
                std::nullopt,  // No current certificate metadata (batch update)
                std::nullopt,  // No current compliance status (batch update)
                enhancedStats  // Include accumulated validation statistics
            );

            spdlog::info("Processing progress: {}/{} entries, {} certs ({} LDAP), {} CRLs ({} LDAP), {} MLs ({} LDAP)",
                        processedEntries, totalEntries,
                        counts.cscaCount + counts.dscCount, counts.ldapCertStoredCount,
                        counts.crlCount, counts.ldapCrlStoredCount,
                        counts.mlCount, counts.ldapMlStoredCount);
        }
    }

    spdlog::info("LDIF processing completed: {} CSCA, {} DSC, {} DSC_NC, {} CRLs, {} MLs",
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount);

    // Phase 4.4: Send final progress with complete validation statistics
    enhancedStats.processedCount = counts.cscaCount + counts.dscCount + counts.dscNcCount;
    sendProgressWithMetadata(
        uploadId,
        common::ProcessingStage::VALIDATION_COMPLETED,
        totalEntries,
        totalEntries,
        "검증 완료: " + std::to_string(enhancedStats.processedCount) + "개 인증서 처리됨",
        std::nullopt,  // No current certificate
        std::nullopt,  // No current compliance
        enhancedStats  // Final validation statistics
    );

    return counts;
}

int LdifProcessor::uploadToLdap(
    const std::string& uploadId,
    LDAP* ld
) {
    if (!ld) {
        spdlog::warn("LDAP connection not available for upload {}", uploadId);
        return 0;
    }

    spdlog::info("Uploading certificates from DB to LDAP for upload {}", uploadId);

    // TODO Phase 6.1: Replace with CertificateRepository::findNotStoredInLdapByUploadId()
    // For now, this is a stub implementation
    // The actual LDAP upload logic needs:
    // 1. certificateRepository->findNotStoredInLdapByUploadId(uploadId)
    // 2. For each certificate: saveCertificateToLdap() from main.cpp
    // 3. certificateRepository->updateCertificateLdapStatus(certificateId, ldapDn)

    int uploadedCount = 0;

    spdlog::warn("uploadToLdap stub: Needs CertificateRepository::findNotStoredInLdapByUploadId() method");

    return uploadedCount;
}

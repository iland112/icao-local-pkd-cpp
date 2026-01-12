#include "ldif_processor.h"
#include "common.h"
#include <spdlog/spdlog.h>
#include <libpq-fe.h>
#include <ldap.h>
#include <sstream>
#include <algorithm>

// Note: This file contains extracted logic from main.cpp
// The actual implementation will call existing functions from main.cpp
// until we fully extract all helper functions.

// Forward declarations of functions that still exist in main.cpp
// These will be moved here gradually
extern std::vector<LdifEntry> parseLdifContent(const std::string& content);
extern bool parseCertificateEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                                  const LdifEntry& entry, const std::string& attrName,
                                  int& cscaCount, int& dscCount, int& dscNcCount,
                                  int& ldapStoredCount, ValidationStats& validationStats);
extern bool parseCrlEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                         const LdifEntry& entry, int& crlCount, int& ldapCrlStoredCount);
extern bool parseMasterListEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                                const LdifEntry& entry, int& mlCount, int& ldapMlStoredCount);

// Forward declaration for sendDbSavingProgress helper function (from main.cpp)
extern void sendDbSavingProgress(const std::string& uploadId, int processedCount, int totalCount, const std::string& message);

std::vector<LdifEntry> LdifProcessor::parseLdifContent(const std::string& content) {
    // Call the existing function from main.cpp
    return ::parseLdifContent(content);
}

LdifProcessor::ProcessingCounts LdifProcessor::processEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    PGconn* conn,
    LDAP* ld,
    ValidationStats& stats
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
                parseCertificateEntry(conn, ld, uploadId, entry, "userCertificate;binary",
                                    counts.cscaCount, counts.dscCount, counts.dscNcCount,
                                    counts.ldapCertStoredCount, stats);
            }
            // Check for cACertificate;binary
            else if (entry.hasAttribute("cACertificate;binary")) {
                parseCertificateEntry(conn, ld, uploadId, entry, "cACertificate;binary",
                                    counts.cscaCount, counts.dscCount, counts.dscNcCount,
                                    counts.ldapCertStoredCount, stats);
            }

            // Check for CRL
            if (entry.hasAttribute("certificateRevocationList;binary")) {
                parseCrlEntry(conn, ld, uploadId, entry, counts.crlCount, counts.ldapCrlStoredCount);
            }

            // Check for Master List
            if (entry.hasAttribute("pkdMasterListContent;binary") ||
                entry.hasAttribute("pkdMasterListContent")) {
                parseMasterListEntry(conn, ld, uploadId, entry, counts.mlCount, counts.ldapMlStoredCount);
            }

        } catch (const std::exception& e) {
            spdlog::warn("Error processing entry {}: {}", entry.dn, e.what());
        }

        processedEntries++;

        // v1.5.1: Send progress update to frontend every 50 entries
        if (processedEntries % 50 == 0 || processedEntries == totalEntries) {
            // Build detailed progress message (only show non-zero counts)
            std::string progressMsg = "처리 중: ";
            std::vector<std::string> parts;
            if (counts.cscaCount > 0) parts.push_back("CSCA " + std::to_string(counts.cscaCount));
            if (counts.dscCount > 0) parts.push_back("DSC " + std::to_string(counts.dscCount));
            if (counts.dscNcCount > 0) parts.push_back("DSC_NC " + std::to_string(counts.dscNcCount));
            if (counts.crlCount > 0) parts.push_back("CRL " + std::to_string(counts.crlCount));
            if (counts.mlCount > 0) parts.push_back("ML " + std::to_string(counts.mlCount));

            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) progressMsg += ", ";
                progressMsg += parts[i];
            }

            // Send progress to frontend via SSE (call helper function from main.cpp)
            sendDbSavingProgress(uploadId, processedEntries, totalEntries, progressMsg);

            spdlog::info("Processing progress: {}/{} entries, {} certs ({} LDAP), {} CRLs ({} LDAP), {} MLs ({} LDAP)",
                        processedEntries, totalEntries,
                        counts.cscaCount + counts.dscCount, counts.ldapCertStoredCount,
                        counts.crlCount, counts.ldapCrlStoredCount,
                        counts.mlCount, counts.ldapMlStoredCount);
        }
    }

    spdlog::info("LDIF processing completed: {} CSCA, {} DSC, {} DSC_NC, {} CRLs, {} MLs",
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount);

    return counts;
}

int LdifProcessor::uploadToLdap(
    const std::string& uploadId,
    PGconn* conn,
    LDAP* ld
) {
    if (!ld) {
        spdlog::warn("LDAP connection not available for upload {}", uploadId);
        return 0;
    }

    spdlog::info("Uploading certificates from DB to LDAP for upload {}", uploadId);

    // Query certificates that are not yet uploaded to LDAP
    std::string query = "SELECT id, certificate_type, country_code, subject_dn, "
                       "issuer_dn, serial_number, fingerprint_sha256, certificate_binary "
                       "FROM certificate "
                       "WHERE upload_id = '" + uploadId + "' "
                       "AND (stored_in_ldap = FALSE OR stored_in_ldap IS NULL)";

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Failed to query certificates for LDAP upload: {}", PQerrorMessage(conn));
        PQclear(res);
        return 0;
    }

    int uploadedCount = 0;
    int totalCerts = PQntuples(res);

    spdlog::info("Found {} certificates to upload to LDAP", totalCerts);

    // For now, return the count
    // The actual LDAP upload logic needs saveCertificateToLdap function
    // which is still in main.cpp

    PQclear(res);
    return uploadedCount;
}

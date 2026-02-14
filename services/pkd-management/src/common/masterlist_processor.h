/**
 * @file masterlist_processor.h
 * @brief ICAO PKD Master List processing and CSCA certificate extraction
 */

#pragma once

#include <string>
#include <vector>
#include <libpq-fe.h>
#include <ldap.h>
#include "progress_manager.h"  // For ValidationStatistics, addProcessingError

// Forward declaration
struct LdifEntry;

/**
 * @brief Statistics for Master List processing
 */
struct MasterListStats {
    int mlCount = 0;                    // Number of Master List entries processed
    int mlscCount = 0;                  // Number of MLSCs extracted from all MLs (v2.1.1)
    int ldapMlStoredCount = 0;          // Number of ML stored in LDAP o=ml
    int cscaExtractedCount = 0;         // Number of CSCAs extracted from all MLs
    int cscaDuplicateCount = 0;         // Number of duplicate CSCAs detected
    int cscaNewCount = 0;               // Number of new (non-duplicate) CSCAs
    int ldapCscaStoredCount = 0;        // Number of CSCAs stored in LDAP o=csca
};

/**
 * @brief Parse Collection 002 Master List entry using the Repository Pattern
 *
 * Current behavior:
 * - Extracts individual CSCAs from each Master List CMS
 * - Saves CSCAs to o=csca (primary, included in stats)
 * - Saves original ML CMS to o=ml (backup, excluded from stats)
 * - Detects and tracks duplicates comprehensively
 *
 * Processing Steps:
 * 1. Parse pkdMasterListContent CMS structure
 * 2. Extract each CSCA certificate (loop sk_X509_value)
 * 3. For each CSCA:
 *    a. Check duplicate (by fingerprint_sha256)
 *    b. If NEW: Insert to DB + Save to LDAP o=csca
 *    c. If DUPLICATE: Skip LDAP, increment duplicate_count
 *    d. Track source in certificate_duplicates table
 *    e. Log detailed status
 * 4. Save original Master List CMS to o=ml (backup)
 * 5. Update statistics
 *
 * @param ld LDAP connection (can be nullptr to skip LDAP operations)
 * @param uploadId Current upload UUID
 * @param entry LDIF entry containing pkdMasterListContent
 * @param stats Statistics counters (updated by reference)
 * @return bool Success status
 * @note Uses global certificateRepository for database operations
 *
 * Logging Format:
 * [ML] CSCA 1/12 - NEW - fingerprint: abc123..., subject: C=KR,O=MOFA,CN=CSCA-KOREA
 * [ML] CSCA 2/12 - DUPLICATE - fingerprint: def456..., cert_id: 42
 * [ML] Master List saved to o=ml: c=KR,dc=data,...
 */
bool parseMasterListEntryV2(
    LDAP* ld,
    const std::string& uploadId,
    const LdifEntry& entry,
    MasterListStats& stats,
    common::ValidationStatistics* enhancedStats = nullptr
);

/**
 * @brief Process Master List file (.ml format) using the Repository Pattern
 *
 * Behavior:
 * - Extracts MLSC from CMS SignerInfo (1-2 certificates, saves to o=mlsc)
 * - Extracts CSCAs from pkiData (self-signed, saves to o=csca)
 * - Extracts Link Certificates from pkiData (cross-signed, saves to o=lc)
 * - Saves original Master List CMS to master_list table
 *
 * Processing Steps:
 * 1. Parse Master List CMS structure
 * 2. Extract MLSC from SignerInfo (typically 1 certificate: ICAO ML Signer)
 *    - Save to o=mlsc in LDAP
 * 3. Extract certificates from pkiData
 *    - Self-signed (Subject DN = Issuer DN) → o=csca
 *    - Cross-signed (Subject DN ≠ Issuer DN) → o=lc
 * 4. Save original Master List CMS to master_list table
 * 5. Update statistics
 *
 * @param ld LDAP connection (can be nullptr to skip LDAP operations)
 * @param uploadId Current upload UUID
 * @param content Master List file binary content
 * @param stats Statistics counters (updated by reference)
 * @return bool Success status
 * @note Uses global certificateRepository for database operations
 *
 * Logging Format:
 * [ML] Found 1 SignerInfo(s) - extracting MLSC certificates
 * [ML] MLSC 1/1 - NEW - fingerprint: abc123..., subject: CN=ICAO Master List Signer,C=UN
 * [ML] CSCA (Self-signed) 1/536 - NEW - fingerprint: def456..., subject: C=KR,O=MOFA,CN=CSCA
 * [ML] LC (Link Certificate) 2/536 - NEW - fingerprint: ghi789..., subject: C=LV,O=NSA,CN=CSCA Latvia
 */
bool processMasterListFile(
    LDAP* ld,
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    MasterListStats& stats,
    common::ValidationStatistics* enhancedStats = nullptr
);

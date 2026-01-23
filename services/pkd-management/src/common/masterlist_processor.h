#pragma once

#include <string>
#include <vector>
#include <libpq-fe.h>
#include <ldap.h>

// Forward declaration
struct LdifEntry;

/**
 * @brief Statistics for Master List processing
 */
struct MasterListStats {
    int mlCount = 0;                    // Number of Master List entries processed
    int ldapMlStoredCount = 0;          // Number of ML stored in LDAP o=ml
    int cscaExtractedCount = 0;         // Number of CSCAs extracted from all MLs
    int cscaDuplicateCount = 0;         // Number of duplicate CSCAs detected
    int cscaNewCount = 0;               // Number of new (non-duplicate) CSCAs
    int ldapCscaStoredCount = 0;        // Number of CSCAs stored in LDAP o=csca
};

/**
 * @brief Parse Collection 002 Master List entry (v2.0.0)
 *
 * NEW BEHAVIOR (v2.0.0):
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
 * @param conn PostgreSQL connection
 * @param ld LDAP connection (can be nullptr to skip LDAP operations)
 * @param uploadId Current upload UUID
 * @param entry LDIF entry containing pkdMasterListContent
 * @param stats Statistics counters (updated by reference)
 * @return bool Success status
 *
 * Logging Format:
 * [ML] CSCA 1/12 - NEW - fingerprint: abc123..., subject: C=KR,O=MOFA,CN=CSCA-KOREA
 * [ML] CSCA 2/12 - DUPLICATE - fingerprint: def456..., cert_id: 42
 * [ML] Master List saved to o=ml: c=KR,dc=data,...
 */
bool parseMasterListEntryV2(
    PGconn* conn,
    LDAP* ld,
    const std::string& uploadId,
    const LdifEntry& entry,
    MasterListStats& stats
);

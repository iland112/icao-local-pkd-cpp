#pragma once

#include <string>
#include <vector>
#include <map>

namespace icao {
namespace sync {

// =============================================================================
// Database Statistics
// =============================================================================
struct DbStats {
    int cscaCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int storedInLdapCount = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};

// =============================================================================
// LDAP Statistics
// =============================================================================
struct LdapStats {
    int cscaCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int totalEntries = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};

// =============================================================================
// Sync Result
// =============================================================================
struct SyncResult {
    std::string status;  // SYNCED, DISCREPANCY, ERROR
    DbStats dbStats;
    LdapStats ldapStats;
    int cscaDiscrepancy = 0;
    int dscDiscrepancy = 0;
    int dscNcDiscrepancy = 0;
    int crlDiscrepancy = 0;
    int totalDiscrepancy = 0;
    int checkDurationMs = 0;
    std::string errorMessage;
    int syncStatusId = 0;
};

// =============================================================================
// Certificate Information
// =============================================================================
struct CertificateInfo {
    std::string id;             // UUID or integer ID as string
    std::string certType;       // CSCA, DSC, DSC_NC, CRL
    std::string countryCode;
    std::string subject;
    std::string issuer;
    std::vector<unsigned char> certData;
    std::string ldapDn;         // LDAP Distinguished Name
};

// =============================================================================
// Reconciliation Failure
// =============================================================================
struct ReconciliationFailure {
    std::string certType;
    std::string operation;      // ADD, DELETE
    std::string countryCode;
    std::string subject;
    std::string error;
};

// =============================================================================
// Reconciliation Result
// =============================================================================
struct ReconciliationResult {
    bool success = false;
    int totalProcessed = 0;
    int cscaAdded = 0;
    int cscaDeleted = 0;
    int dscAdded = 0;
    int dscDeleted = 0;
    int dscNcAdded = 0;
    int dscNcDeleted = 0;
    int crlAdded = 0;
    int crlDeleted = 0;
    int successCount = 0;
    int failedCount = 0;
    int durationMs = 0;
    std::string status;         // COMPLETED, PARTIAL, FAILED
    std::string errorMessage;
    std::vector<ReconciliationFailure> failures;
};

} // namespace sync
} // namespace icao

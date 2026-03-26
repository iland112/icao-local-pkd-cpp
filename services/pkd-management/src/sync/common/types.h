/**
 * @file types.h
 * @brief Data types for DB-LDAP synchronization operations
 */
#pragma once

#include <string>
#include <vector>
#include <map>

namespace icao {
namespace relay {

/// @name Database Statistics
/// @{

/** @brief Database certificate and CRL count statistics */
struct DbStats {
    int cscaCount = 0;
    int mlscCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int storedInLdapCount = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};
/// @}

/// @name LDAP Statistics
/// @{

/** @brief LDAP directory certificate and CRL count statistics */
struct LdapStats {
    int cscaCount = 0;
    int mlscCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int totalEntries = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};
/// @}

/// @name Sync Result
/// @{

/** @brief Result of a DB-LDAP sync check */
struct SyncResult {
    std::string status;  // SYNCED, DISCREPANCY, ERROR
    DbStats dbStats;
    LdapStats ldapStats;
    int cscaDiscrepancy = 0;
    int mlscDiscrepancy = 0;
    int dscDiscrepancy = 0;
    int dscNcDiscrepancy = 0;
    int crlDiscrepancy = 0;
    int totalDiscrepancy = 0;
    int checkDurationMs = 0;
    std::string errorMessage;
    int syncStatusId = 0;
};
/// @}

/// @name Certificate Information
/// @{

/** @brief Certificate data for LDAP operations */
struct CertificateInfo {
    std::string id;             // UUID or integer ID as string
    std::string certType;       // CSCA, DSC, DSC_NC, CRL
    std::string countryCode;
    std::string subject;
    std::string issuer;
    std::string fingerprint;    // SHA-256 fingerprint (hex) for DN
    std::vector<unsigned char> certData;
    std::string ldapDn;         // LDAP Distinguished Name
};
/// @}

/// @name CRL Information
/// @{

/** @brief CRL data for LDAP operations */
struct CrlInfo {
    std::string id;             // UUID
    std::string countryCode;
    std::string issuerDn;
    std::string fingerprint;    // SHA-256 fingerprint (hex) for DN
    std::vector<unsigned char> crlData;
    std::string ldapDn;         // LDAP Distinguished Name
};
/// @}

/// @name Reconciliation Types
/// @{

/** @brief A single reconciliation failure record */
struct ReconciliationFailure {
    std::string certType;
    std::string operation;      // ADD, DELETE
    std::string countryCode;
    std::string subject;
    std::string error;
};

/** @brief Aggregate result of a reconciliation run */
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
/// @}

} // namespace relay
} // namespace icao

/**
 * @file icao_ldap_types.h
 * @brief Types for ICAO PKD LDAP synchronization
 */
#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace icao {
namespace relay {

/// Certificate entry retrieved from ICAO PKD LDAP
struct IcaoLdapCertEntry {
    std::string dn;                    // Full DN in ICAO LDAP
    std::string cn;                    // CN (issuer DN or fingerprint)
    std::string countryCode;           // ISO 3166-1 alpha-2
    std::string certType;              // csca, dsc, crl, ml, dsc_nc
    std::vector<uint8_t> binaryData;   // DER certificate or CRL binary
    int pkdVersion = 0;                // ICAO PKD version number
    // NC-specific
    std::string conformanceCode;
    std::string conformanceText;
};

/// Result of an ICAO LDAP sync operation
struct IcaoLdapSyncResult {
    std::string syncType;     // FULL or INCREMENTAL
    std::string triggeredBy;  // MANUAL or SCHEDULED
    std::string status;       // RUNNING, COMPLETED, FAILED

    int totalRemoteCount = 0;
    int newCertificates = 0;
    int existingSkipped = 0;
    int failedCount = 0;
    int durationMs = 0;

    std::string errorMessage;

    std::chrono::system_clock::time_point startedAt;
    std::chrono::system_clock::time_point completedAt;
};

/// ICAO LDAP sync configuration (runtime-modifiable)
struct IcaoLdapSyncConfig {
    bool enabled = false;
    std::string host;
    int port = 389;
    std::string bindDn;
    std::string baseDn;
    int syncIntervalMinutes = 60;
};

} // namespace relay
} // namespace icao

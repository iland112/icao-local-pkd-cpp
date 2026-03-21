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

/// Real-time sync progress (broadcast via SSE)
struct IcaoLdapSyncProgress {
    std::string phase;            // CONNECTING, SEARCHING, PROCESSING, COMPLETED, FAILED
    std::string currentType;      // CSCA, DSC, CRL, DSC_NC
    int totalTypes = 4;           // Total certificate types to process
    int completedTypes = 0;       // Types fully processed so far

    int currentTypeTotal = 0;     // Entries found for current type
    int currentTypeProcessed = 0; // Entries processed for current type
    int currentTypeNew = 0;       // New entries saved for current type
    int currentTypeSkipped = 0;   // Existing entries skipped for current type

    // Cumulative
    int totalNew = 0;
    int totalSkipped = 0;
    int totalFailed = 0;
    int totalRemoteCount = 0;

    std::string message;          // Human-readable status message
    int elapsedMs = 0;
};

/// ICAO LDAP connection test result
struct IcaoLdapConnectionTestResult {
    bool success = false;
    int latencyMs = 0;
    int entryCount = 0;
    std::string serverInfo;
    std::string tlsMode;          // "Simple Bind" or "TLS Mutual Auth (SASL EXTERNAL)"
    std::string errorMessage;
};

/// ICAO LDAP sync configuration (runtime-modifiable)
struct IcaoLdapSyncConfig {
    bool enabled = false;
    std::string host;
    int port = 389;
    std::string bindDn;
    std::string baseDn;
    int syncIntervalMinutes = 60;
    bool useTls = false;
    std::string tlsCertFile;
    std::string tlsKeyFile;
    std::string tlsCaCertFile;
};

} // namespace relay
} // namespace icao

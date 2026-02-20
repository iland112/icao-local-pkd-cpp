#pragma once

#include <string>
#include <vector>
#include <json/json.h>

// Forward declaration for X509 certificate (OpenSSL)
typedef struct x509_st X509;

/**
 * @file doc9303_checklist.h
 * @brief ICAO Doc 9303 Compliance Checklist
 *
 * Performs detailed per-item compliance checks against ICAO Doc 9303
 * specifications for PKD-uploaded certificates (CSCA, DSC, MLSC).
 *
 * Reference: "Checks Against Doc9303 Applied to PKD Uploads" (docs/)
 *
 * @date 2026-02-20
 */

namespace common {

/**
 * @brief Single Doc 9303 compliance check item result
 */
struct Doc9303CheckItem {
    std::string id;          ///< Check ID (e.g., "version_v3", "key_usage_critical")
    std::string category;    ///< Category (한국어: "기본", "서명", "Key Usage", etc.)
    std::string label;       ///< Check label (한국어)
    std::string status;      ///< "PASS", "FAIL", "WARNING", "NA"
    std::string message;     ///< Detail message (failure reason, actual value, etc.)
    std::string requirement; ///< Doc 9303 requirement summary

    Json::Value toJson() const;
};

/**
 * @brief Doc 9303 compliance checklist result (all items)
 */
struct Doc9303ChecklistResult {
    std::string certificateType;  ///< "CSCA", "DSC", "DSC_NC", "MLSC"
    int totalChecks = 0;
    int passCount = 0;
    int failCount = 0;
    int warningCount = 0;
    int naCount = 0;
    std::string overallStatus;    ///< "CONFORMANT", "NON_CONFORMANT", "WARNING"
    std::vector<Doc9303CheckItem> items;

    Json::Value toJson() const;
};

/**
 * @brief Run Doc 9303 compliance checklist against an X.509 certificate
 *
 * Performs ~28 individual checks based on certificate type (CSCA/DSC/MLSC).
 * Each check returns PASS/FAIL/WARNING/NA status with detailed messages.
 *
 * Uses OpenSSL X509 API directly for precise checks including:
 * - Extension criticality flags
 * - Unique Identifier absence
 * - Serial number octet length
 * - Signature algorithm OID matching
 *
 * @param cert OpenSSL X509 certificate pointer
 * @param certType Certificate type: "CSCA", "DSC", "DSC_NC", "MLSC"
 * @return Doc9303ChecklistResult with per-item results
 */
Doc9303ChecklistResult runDoc9303Checklist(X509* cert, const std::string& certType);

} // namespace common

/**
 * @file certificate_chain_validation.h
 * @brief Domain model for certificate chain validation result
 *
 * Represents the result of DSC → CSCA trust chain validation with CRL checking.
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>

namespace domain {
namespace models {

/**
 * @brief CRL check status enum
 */
enum class CrlStatus {
    VALID,              // Certificate not revoked, CRL valid
    REVOKED,            // Certificate is revoked
    CRL_UNAVAILABLE,    // CRL not found in LDAP
    CRL_EXPIRED,        // CRL is expired
    CRL_INVALID,        // CRL signature invalid
    NOT_CHECKED         // CRL check skipped
};

/**
 * @brief Convert CRL status to string
 */
inline std::string crlStatusToString(CrlStatus status) {
    switch (status) {
        case CrlStatus::VALID: return "VALID";
        case CrlStatus::REVOKED: return "REVOKED";
        case CrlStatus::CRL_UNAVAILABLE: return "CRL_UNAVAILABLE";
        case CrlStatus::CRL_EXPIRED: return "CRL_EXPIRED";
        case CrlStatus::CRL_INVALID: return "CRL_INVALID";
        case CrlStatus::NOT_CHECKED: return "NOT_CHECKED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Certificate chain validation result domain model
 *
 * Contains complete validation result for DSC certificate including:
 * - Trust chain validation (DSC → Link Cert → Root CSCA)
 * - Certificate expiration status
 * - CRL revocation checking
 * - Detailed error messages
 */
struct CertificateChainValidation {
    // Overall validation result
    bool valid = false;

    // DSC certificate information
    std::string dscSubject;
    std::string dscSerialNumber;
    std::string dscIssuer;
    std::optional<std::string> dscNotBefore;
    std::optional<std::string> dscNotAfter;
    bool dscExpired = false;

    // CSCA certificate information (root or link)
    std::string cscaSubject;
    std::string cscaSerialNumber;
    std::optional<std::string> cscaNotBefore;
    std::optional<std::string> cscaNotAfter;
    bool cscaExpired = false;

    // Trust chain path (for display)
    // Example: "DSC → Link Cert → Root CSCA"
    std::string trustChainPath;
    int trustChainDepth = 0;  // Number of certificates in chain

    // Certificate expiration status (ICAO 9303 - point-in-time validation)
    bool validAtSigningTime = true;  // Was valid at document signing time
    std::string expirationStatus;    // "VALID", "WARNING", "EXPIRED"
    std::optional<std::string> expirationMessage;

    // CRL checking
    bool crlChecked = false;
    bool revoked = false;
    CrlStatus crlStatus = CrlStatus::NOT_CHECKED;
    std::optional<std::string> crlMessage;
    std::optional<std::string> crlStatusDescription;
    std::optional<std::string> crlStatusDetailedDescription;
    std::string crlStatusSeverity;  // "INFO", "WARNING", "CRITICAL"

    // Validation errors
    std::optional<std::string> validationErrors;

    // Signature verification details
    bool signatureVerified = false;
    std::optional<std::string> signatureAlgorithm;

    /**
     * @brief Convert to JSON for API response
     * @return Json::Value representation
     */
    Json::Value toJson() const;

    /**
     * @brief Create validation result for VALID case
     * @param dscSubject DSC subject DN
     * @param dscSerial DSC serial number
     * @param cscaSubject CSCA subject DN
     * @param cscaSerial CSCA serial number
     * @return CertificateChainValidation instance
     */
    static CertificateChainValidation createValid(
        const std::string& dscSubject,
        const std::string& dscSerial,
        const std::string& cscaSubject,
        const std::string& cscaSerial
    );

    /**
     * @brief Create validation result for INVALID case
     * @param errorMessage Error description
     * @return CertificateChainValidation instance
     */
    static CertificateChainValidation createInvalid(
        const std::string& errorMessage
    );

    /**
     * @brief Check if certificate chain is valid (including CRL)
     * @return true if all validations pass
     */
    bool isFullyValid() const {
        return valid &&
               !revoked &&
               !dscExpired &&
               !cscaExpired &&
               signatureVerified;
    }

    /**
     * @brief Get validation status string
     * @return "VALID", "INVALID", "WARNING"
     */
    std::string getValidationStatus() const {
        if (!valid) return "INVALID";
        if (dscExpired || cscaExpired || revoked) return "WARNING";
        return "VALID";
    }
};

} // namespace models
} // namespace domain

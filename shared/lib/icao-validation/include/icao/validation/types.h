/**
 * @file types.h
 * @brief Common types for ICAO validation library
 *
 * Shared enums and result structs used across all validation modules.
 * ICAO Doc 9303 Part 10/11/12 compliant.
 */

#pragma once

#include <string>
#include <vector>

namespace icao::validation {

/// @brief Certificate validation status
enum class ValidationStatus {
    VALID,          ///< Trust chain valid, not expired
    EXPIRED_VALID,  ///< Trust chain valid, but certificate expired (ICAO hybrid model)
    INVALID,        ///< Trust chain or signature verification failed
    PENDING,        ///< Validation not yet completed (CSCA not found)
    ERROR           ///< Internal error during validation
};

/// @brief CRL check status (RFC 5280 Section 5.3.1)
enum class CrlCheckStatus {
    VALID,            ///< Certificate not revoked, CRL valid
    REVOKED,          ///< Certificate is revoked
    CRL_UNAVAILABLE,  ///< CRL not found
    CRL_EXPIRED,      ///< CRL nextUpdate is in the past
    CRL_INVALID,      ///< CRL signature invalid
    NOT_CHECKED       ///< CRL check was not performed
};

/// @brief Trust chain build + validation result
struct TrustChainResult {
    bool valid = false;         ///< True if all signatures in chain are valid
    std::string path;           ///< Human-readable path (e.g., "DSC -> Link -> Root")
    int depth = 0;              ///< Number of certificates in chain
    bool cscaExpired = false;   ///< True if any CSCA in chain is expired (informational)
    bool dscExpired = false;    ///< True if leaf DSC is expired
    std::string message;        ///< Error or info message
    std::string cscaSubjectDn;  ///< Root CSCA subject DN
    std::string cscaFingerprint;///< Root CSCA fingerprint
};

/// @brief CRL revocation check result
struct CrlCheckResult {
    CrlCheckStatus status = CrlCheckStatus::NOT_CHECKED;
    std::string thisUpdate;         ///< CRL issued date (ISO 8601)
    std::string nextUpdate;         ///< CRL next update date (ISO 8601)
    std::string revocationReason;   ///< RFC 5280 CRLReason (e.g., "keyCompromise")
    std::string message;
};

/// @brief ICAO algorithm compliance result (Part 12 Appendix A)
struct AlgorithmComplianceResult {
    bool compliant = true;
    std::string algorithm;  ///< Signature algorithm name
    std::string warning;    ///< Non-empty if deprecated algorithm
    int keyBits = 0;        ///< Public key size in bits
};

/// @brief Extension validation result (Part 12 Section 4.6 / RFC 5280 Section 4.2)
struct ExtensionValidationResult {
    bool valid = true;
    std::vector<std::string> warnings;

    std::string warningsAsString() const {
        std::string result;
        for (size_t i = 0; i < warnings.size(); i++) {
            if (i > 0) result += "; ";
            result += warnings[i];
        }
        return result;
    }
};

/// @brief Convert ValidationStatus to string
inline std::string validationStatusToString(ValidationStatus s) {
    switch (s) {
        case ValidationStatus::VALID:         return "VALID";
        case ValidationStatus::EXPIRED_VALID: return "EXPIRED_VALID";
        case ValidationStatus::INVALID:       return "INVALID";
        case ValidationStatus::PENDING:       return "PENDING";
        case ValidationStatus::ERROR:         return "ERROR";
    }
    return "UNKNOWN";
}

/// @brief Convert CrlCheckStatus to string
inline std::string crlCheckStatusToString(CrlCheckStatus s) {
    switch (s) {
        case CrlCheckStatus::VALID:           return "VALID";
        case CrlCheckStatus::REVOKED:         return "REVOKED";
        case CrlCheckStatus::CRL_UNAVAILABLE: return "CRL_UNAVAILABLE";
        case CrlCheckStatus::CRL_EXPIRED:     return "CRL_EXPIRED";
        case CrlCheckStatus::CRL_INVALID:     return "CRL_INVALID";
        case CrlCheckStatus::NOT_CHECKED:     return "NOT_CHECKED";
    }
    return "UNKNOWN";
}

} // namespace icao::validation

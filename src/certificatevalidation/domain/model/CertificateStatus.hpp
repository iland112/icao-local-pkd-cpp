/**
 * @file CertificateStatus.hpp
 * @brief Enum for certificate validation status
 */

#pragma once

#include <string>

namespace certificatevalidation::domain::model {

/**
 * @brief Certificate validation status
 *
 * Represents the overall validation status of a certificate.
 */
enum class CertificateStatus {
    VALID,          ///< Certificate is valid (all checks passed)
    EXPIRED,        ///< Certificate has expired (notAfter < now)
    NOT_YET_VALID,  ///< Certificate is not yet valid (notBefore > now)
    REVOKED,        ///< Certificate has been revoked (in CRL)
    INVALID,        ///< Certificate failed validation (signature, chain, etc.)
    UNKNOWN         ///< Validation status unknown (not yet validated)
};

/**
 * @brief Convert CertificateStatus to display string
 */
inline std::string toString(CertificateStatus status) {
    switch (status) {
        case CertificateStatus::VALID:         return "Valid";
        case CertificateStatus::EXPIRED:       return "Expired";
        case CertificateStatus::NOT_YET_VALID: return "Not Yet Valid";
        case CertificateStatus::REVOKED:       return "Revoked";
        case CertificateStatus::INVALID:       return "Invalid";
        case CertificateStatus::UNKNOWN:       return "Unknown";
        default:                               return "Unknown";
    }
}

/**
 * @brief Convert CertificateStatus to string for serialization
 */
inline std::string toDbString(CertificateStatus status) {
    switch (status) {
        case CertificateStatus::VALID:         return "VALID";
        case CertificateStatus::EXPIRED:       return "EXPIRED";
        case CertificateStatus::NOT_YET_VALID: return "NOT_YET_VALID";
        case CertificateStatus::REVOKED:       return "REVOKED";
        case CertificateStatus::INVALID:       return "INVALID";
        case CertificateStatus::UNKNOWN:       return "UNKNOWN";
        default:                               return "UNKNOWN";
    }
}

/**
 * @brief Parse CertificateStatus from string
 */
inline CertificateStatus parseCertificateStatus(const std::string& str) {
    if (str == "VALID") return CertificateStatus::VALID;
    if (str == "EXPIRED") return CertificateStatus::EXPIRED;
    if (str == "NOT_YET_VALID") return CertificateStatus::NOT_YET_VALID;
    if (str == "REVOKED") return CertificateStatus::REVOKED;
    if (str == "INVALID") return CertificateStatus::INVALID;
    return CertificateStatus::UNKNOWN;
}

} // namespace certificatevalidation::domain::model

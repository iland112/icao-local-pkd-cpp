#pragma once

#include <string>

namespace pa::domain::model {

/**
 * CRL (Certificate Revocation List) check status.
 */
enum class CrlCheckStatus {
    VALID,           // Certificate is valid and not revoked
    REVOKED,         // Certificate has been revoked
    CRL_UNAVAILABLE, // CRL not available in LDAP
    CRL_EXPIRED,     // CRL has expired (nextUpdate passed)
    CRL_INVALID,     // CRL signature verification failed
    NOT_CHECKED      // CRL verification was not performed
};

/**
 * Convert status to string.
 */
inline std::string toString(CrlCheckStatus status) {
    switch (status) {
        case CrlCheckStatus::VALID:
            return "VALID";
        case CrlCheckStatus::REVOKED:
            return "REVOKED";
        case CrlCheckStatus::CRL_UNAVAILABLE:
            return "CRL_UNAVAILABLE";
        case CrlCheckStatus::CRL_EXPIRED:
            return "CRL_EXPIRED";
        case CrlCheckStatus::CRL_INVALID:
            return "CRL_INVALID";
        case CrlCheckStatus::NOT_CHECKED:
            return "NOT_CHECKED";
        default:
            return "UNKNOWN";
    }
}

/**
 * Get English description for CRL status.
 */
inline std::string getStatusDescription(CrlCheckStatus status) {
    switch (status) {
        case CrlCheckStatus::VALID:
            return "Certificate is valid and not revoked";
        case CrlCheckStatus::REVOKED:
            return "Certificate has been revoked";
        case CrlCheckStatus::CRL_UNAVAILABLE:
            return "CRL not available in LDAP";
        case CrlCheckStatus::CRL_EXPIRED:
            return "CRL has expired (nextUpdate passed)";
        case CrlCheckStatus::CRL_INVALID:
            return "CRL signature verification failed";
        case CrlCheckStatus::NOT_CHECKED:
            return "CRL verification was not performed";
        default:
            return "Unknown status";
    }
}

/**
 * Get severity level for CRL status.
 */
inline std::string getStatusSeverity(CrlCheckStatus status) {
    switch (status) {
        case CrlCheckStatus::VALID:
            return "SUCCESS";
        case CrlCheckStatus::REVOKED:
        case CrlCheckStatus::CRL_INVALID:
            return "FAILURE";
        case CrlCheckStatus::CRL_UNAVAILABLE:
        case CrlCheckStatus::CRL_EXPIRED:
            return "WARNING";
        case CrlCheckStatus::NOT_CHECKED:
            return "INFO";
        default:
            return "UNKNOWN";
    }
}

} // namespace pa::domain::model

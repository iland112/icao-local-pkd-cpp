#pragma once

#include <string>

namespace pa::domain::model {

/**
 * Status of Passive Authentication verification.
 */
enum class PassiveAuthenticationStatus {
    VALID,      // All verifications passed
    INVALID,    // One or more verifications failed
    ERROR       // Unexpected error during verification
};

/**
 * Convert status to string representation.
 */
inline std::string toString(PassiveAuthenticationStatus status) {
    switch (status) {
        case PassiveAuthenticationStatus::VALID:
            return "VALID";
        case PassiveAuthenticationStatus::INVALID:
            return "INVALID";
        case PassiveAuthenticationStatus::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

/**
 * Parse status from string.
 */
inline PassiveAuthenticationStatus statusFromString(const std::string& str) {
    if (str == "VALID") return PassiveAuthenticationStatus::VALID;
    if (str == "INVALID") return PassiveAuthenticationStatus::INVALID;
    if (str == "ERROR") return PassiveAuthenticationStatus::ERROR;
    return PassiveAuthenticationStatus::ERROR;
}

} // namespace pa::domain::model

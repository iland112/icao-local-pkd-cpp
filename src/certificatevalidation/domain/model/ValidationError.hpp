/**
 * @file ValidationError.hpp
 * @brief Value Object for validation error details
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <string>
#include <chrono>

namespace certificatevalidation::domain::model {

/**
 * @brief Severity level of validation error
 */
enum class ErrorSeverity {
    WARNING,    ///< Non-critical issue
    ERROR       ///< Critical validation failure
};

inline std::string toString(ErrorSeverity severity) {
    switch (severity) {
        case ErrorSeverity::WARNING: return "WARNING";
        case ErrorSeverity::ERROR:   return "ERROR";
        default:                     return "UNKNOWN";
    }
}

/**
 * @brief Validation error Value Object
 *
 * Represents a specific validation error that occurred during certificate validation.
 */
class ValidationError : public shared::domain::ValueObject {
private:
    std::string errorCode_;
    std::string errorMessage_;
    std::string description_;
    ErrorSeverity severity_;
    std::chrono::system_clock::time_point occurredAt_;

    ValidationError(
        std::string errorCode,
        std::string errorMessage,
        std::string description,
        ErrorSeverity severity
    ) : errorCode_(std::move(errorCode)),
        errorMessage_(std::move(errorMessage)),
        description_(std::move(description)),
        severity_(severity),
        occurredAt_(std::chrono::system_clock::now()) {}

public:
    /**
     * @brief Create ValidationError with ERROR severity
     */
    static ValidationError of(
        const std::string& errorCode,
        const std::string& errorMessage,
        const std::string& description = ""
    ) {
        return ValidationError(errorCode, errorMessage, description, ErrorSeverity::ERROR);
    }

    /**
     * @brief Create ValidationError with WARNING severity
     */
    static ValidationError warning(
        const std::string& errorCode,
        const std::string& errorMessage,
        const std::string& description = ""
    ) {
        return ValidationError(errorCode, errorMessage, description, ErrorSeverity::WARNING);
    }

    // Common error factory methods
    static ValidationError signatureInvalid() {
        return of("SIGNATURE_INVALID", "Signature validation failed",
                  "The certificate's digital signature is not valid");
    }

    static ValidationError certificateExpired() {
        return of("CERTIFICATE_EXPIRED", "Certificate has expired",
                  "The certificate's validity period has ended");
    }

    static ValidationError certificateNotYetValid() {
        return of("CERTIFICATE_NOT_YET_VALID", "Certificate is not yet valid",
                  "The certificate's validity period has not yet started");
    }

    static ValidationError certificateRevoked() {
        return of("CERTIFICATE_REVOKED", "Certificate has been revoked",
                  "The certificate is listed in the CRL as revoked");
    }

    static ValidationError chainInvalid() {
        return of("CHAIN_INVALID", "Trust chain validation failed",
                  "Unable to build a valid trust chain to a trusted root");
    }

    static ValidationError issuerNotFound() {
        return of("ISSUER_NOT_FOUND", "Issuer certificate not found",
                  "The issuer certificate could not be located in the trust store");
    }

    static ValidationError basicConstraintsInvalid() {
        return of("BASIC_CONSTRAINTS_INVALID", "Basic Constraints validation failed",
                  "The certificate's Basic Constraints extension is not valid for its type");
    }

    static ValidationError keyUsageInvalid() {
        return of("KEY_USAGE_INVALID", "Key Usage validation failed",
                  "The certificate does not have the required Key Usage bits set");
    }

    static ValidationError crlUnavailable() {
        return warning("CRL_UNAVAILABLE", "CRL not available",
                       "Could not retrieve CRL to check revocation status");
    }

    static ValidationError crlExpired() {
        return warning("CRL_EXPIRED", "CRL has expired",
                       "The CRL's nextUpdate time has passed");
    }

    // Getters
    [[nodiscard]] const std::string& getErrorCode() const noexcept { return errorCode_; }
    [[nodiscard]] const std::string& getErrorMessage() const noexcept { return errorMessage_; }
    [[nodiscard]] const std::string& getDescription() const noexcept { return description_; }
    [[nodiscard]] ErrorSeverity getSeverity() const noexcept { return severity_; }
    [[nodiscard]] std::chrono::system_clock::time_point getOccurredAt() const noexcept { return occurredAt_; }

    [[nodiscard]] bool isCritical() const noexcept {
        return severity_ == ErrorSeverity::ERROR;
    }

    [[nodiscard]] bool isWarning() const noexcept {
        return severity_ == ErrorSeverity::WARNING;
    }

    bool operator==(const ValidationError& other) const noexcept {
        return errorCode_ == other.errorCode_ &&
               errorMessage_ == other.errorMessage_ &&
               severity_ == other.severity_;
    }

    [[nodiscard]] std::string toString() const {
        return "[" + certificatevalidation::domain::model::toString(severity_) + "] " +
               errorCode_ + ": " + errorMessage_;
    }
};

} // namespace certificatevalidation::domain::model

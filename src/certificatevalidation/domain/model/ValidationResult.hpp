/**
 * @file ValidationResult.hpp
 * @brief Value Object for certificate validation result
 */

#pragma once

#include "CertificateStatus.hpp"
#include "shared/domain/ValueObject.hpp"
#include <chrono>
#include <string>
#include <stdexcept>

namespace certificatevalidation::domain::model {

/**
 * @brief Certificate validation result Value Object
 *
 * Represents the result of a certificate validation operation.
 * Immutable after creation.
 *
 * Contains:
 * - Overall status (VALID, EXPIRED, REVOKED, INVALID)
 * - Individual check results (signature, chain, revocation, validity, constraints)
 * - Validation timestamp and duration
 */
class ValidationResult : public shared::domain::ValueObject {
private:
    CertificateStatus overallStatus_;
    bool signatureValid_;
    bool chainValid_;
    bool notRevoked_;
    bool validityValid_;
    bool constraintsValid_;
    std::chrono::system_clock::time_point validatedAt_;
    long validationDurationMillis_;

    ValidationResult(
        CertificateStatus overallStatus,
        bool signatureValid,
        bool chainValid,
        bool notRevoked,
        bool validityValid,
        bool constraintsValid,
        long durationMillis
    ) : overallStatus_(overallStatus),
        signatureValid_(signatureValid),
        chainValid_(chainValid),
        notRevoked_(notRevoked),
        validityValid_(validityValid),
        constraintsValid_(constraintsValid),
        validatedAt_(std::chrono::system_clock::now()),
        validationDurationMillis_(durationMillis) {
        validate();
    }

    void validate() const {
        // Business rule: if VALID, all checks must pass
        if (overallStatus_ == CertificateStatus::VALID) {
            if (!signatureValid_ || !chainValid_ || !notRevoked_ ||
                !validityValid_ || !constraintsValid_) {
                throw std::invalid_argument(
                    "If overall status is VALID, all validations must pass"
                );
            }
        }
        if (validationDurationMillis_ < 0) {
            throw std::invalid_argument("Validation duration cannot be negative");
        }
    }

public:
    /**
     * @brief Create ValidationResult
     */
    static ValidationResult of(
        CertificateStatus overallStatus,
        bool signatureValid,
        bool chainValid,
        bool notRevoked,
        bool validityValid,
        bool constraintsValid,
        long durationMillis = 0
    ) {
        return ValidationResult(
            overallStatus, signatureValid, chainValid, notRevoked,
            validityValid, constraintsValid, durationMillis
        );
    }

    /**
     * @brief Create successful validation result
     */
    static ValidationResult valid(long durationMillis = 0) {
        return ValidationResult(
            CertificateStatus::VALID,
            true, true, true, true, true,
            durationMillis
        );
    }

    /**
     * @brief Create expired validation result
     */
    static ValidationResult expired(long durationMillis = 0) {
        return ValidationResult(
            CertificateStatus::EXPIRED,
            true, true, true, false, true,
            durationMillis
        );
    }

    /**
     * @brief Create revoked validation result
     */
    static ValidationResult revoked(long durationMillis = 0) {
        return ValidationResult(
            CertificateStatus::REVOKED,
            true, true, false, true, true,
            durationMillis
        );
    }

    /**
     * @brief Create invalid (signature failed) validation result
     */
    static ValidationResult signatureInvalid(long durationMillis = 0) {
        return ValidationResult(
            CertificateStatus::INVALID,
            false, false, true, true, true,
            durationMillis
        );
    }

    // Getters
    [[nodiscard]] CertificateStatus getOverallStatus() const noexcept { return overallStatus_; }
    [[nodiscard]] bool isSignatureValid() const noexcept { return signatureValid_; }
    [[nodiscard]] bool isChainValid() const noexcept { return chainValid_; }
    [[nodiscard]] bool isNotRevoked() const noexcept { return notRevoked_; }
    [[nodiscard]] bool isValidityValid() const noexcept { return validityValid_; }
    [[nodiscard]] bool isConstraintsValid() const noexcept { return constraintsValid_; }
    [[nodiscard]] std::chrono::system_clock::time_point getValidatedAt() const noexcept { return validatedAt_; }
    [[nodiscard]] long getValidationDurationMillis() const noexcept { return validationDurationMillis_; }

    // Business logic methods
    [[nodiscard]] bool isValid() const noexcept {
        return overallStatus_ == CertificateStatus::VALID;
    }

    [[nodiscard]] bool isExpired() const noexcept {
        return overallStatus_ == CertificateStatus::EXPIRED;
    }

    [[nodiscard]] bool isRevoked() const noexcept {
        return overallStatus_ == CertificateStatus::REVOKED;
    }

    [[nodiscard]] bool isNotYetValid() const noexcept {
        return overallStatus_ == CertificateStatus::NOT_YET_VALID;
    }

    [[nodiscard]] bool allValidationsPass() const noexcept {
        return signatureValid_ && chainValid_ && notRevoked_ &&
               validityValid_ && constraintsValid_;
    }

    [[nodiscard]] int passedChecksCount() const noexcept {
        int count = 0;
        if (signatureValid_) count++;
        if (chainValid_) count++;
        if (notRevoked_) count++;
        if (validityValid_) count++;
        if (constraintsValid_) count++;
        return count;
    }

    [[nodiscard]] std::string getSummary() const {
        return toString(overallStatus_) + " (" +
               std::to_string(passedChecksCount()) + "/5 checks passed, " +
               std::to_string(validationDurationMillis_) + "ms)";
    }

    bool operator==(const ValidationResult& other) const noexcept {
        return overallStatus_ == other.overallStatus_ &&
               signatureValid_ == other.signatureValid_ &&
               chainValid_ == other.chainValid_ &&
               notRevoked_ == other.notRevoked_ &&
               validityValid_ == other.validityValid_ &&
               constraintsValid_ == other.constraintsValid_;
    }
};

} // namespace certificatevalidation::domain::model

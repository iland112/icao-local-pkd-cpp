#pragma once

#include "CrlCheckStatus.hpp"
#include <string>
#include <optional>
#include <chrono>

namespace pa::domain::model {

/**
 * Result of CRL (Certificate Revocation List) check.
 */
class CrlCheckResult {
private:
    CrlCheckStatus status_;
    std::optional<std::chrono::system_clock::time_point> revocationDate_;
    std::optional<int> revocationReason_;
    std::optional<std::string> errorMessage_;

    CrlCheckResult(
        CrlCheckStatus status,
        std::optional<std::chrono::system_clock::time_point> revocationDate,
        std::optional<int> revocationReason,
        std::optional<std::string> errorMessage
    ) : status_(status),
        revocationDate_(std::move(revocationDate)),
        revocationReason_(revocationReason),
        errorMessage_(std::move(errorMessage)) {}

public:
    CrlCheckResult() : status_(CrlCheckStatus::NOT_CHECKED) {}

    /**
     * Create a VALID result (certificate not revoked).
     */
    static CrlCheckResult valid() {
        return CrlCheckResult(CrlCheckStatus::VALID, std::nullopt, std::nullopt, std::nullopt);
    }

    /**
     * Create a REVOKED result.
     */
    static CrlCheckResult revoked(
        const std::chrono::system_clock::time_point& revocationDate,
        int reason
    ) {
        return CrlCheckResult(
            CrlCheckStatus::REVOKED,
            revocationDate,
            reason,
            std::nullopt
        );
    }

    /**
     * Create a CRL_UNAVAILABLE result.
     */
    static CrlCheckResult unavailable(const std::string& message) {
        return CrlCheckResult(
            CrlCheckStatus::CRL_UNAVAILABLE,
            std::nullopt,
            std::nullopt,
            message
        );
    }

    /**
     * Create a CRL_EXPIRED result.
     */
    static CrlCheckResult expired(const std::string& message) {
        return CrlCheckResult(
            CrlCheckStatus::CRL_EXPIRED,
            std::nullopt,
            std::nullopt,
            message
        );
    }

    /**
     * Create a CRL_INVALID result.
     */
    static CrlCheckResult invalid(const std::string& message) {
        return CrlCheckResult(
            CrlCheckStatus::CRL_INVALID,
            std::nullopt,
            std::nullopt,
            message
        );
    }

    /**
     * Create a NOT_CHECKED result.
     */
    static CrlCheckResult notChecked() {
        return CrlCheckResult(CrlCheckStatus::NOT_CHECKED, std::nullopt, std::nullopt, std::nullopt);
    }

    CrlCheckStatus getStatus() const { return status_; }
    const std::optional<std::chrono::system_clock::time_point>& getRevocationDate() const { return revocationDate_; }
    const std::optional<int>& getRevocationReason() const { return revocationReason_; }
    const std::optional<std::string>& getErrorMessage() const { return errorMessage_; }

    std::string getStatusDescription() const {
        return pa::domain::model::getStatusDescription(status_);
    }

    std::string getStatusSeverity() const {
        return pa::domain::model::getStatusSeverity(status_);
    }

    /**
     * Get revocation reason as text.
     */
    std::string getRevocationReasonText() const {
        if (!revocationReason_.has_value()) {
            return "Unknown";
        }

        // RFC 5280 CRLReason values
        switch (revocationReason_.value()) {
            case 0: return "unspecified";
            case 1: return "keyCompromise";
            case 2: return "cACompromise";
            case 3: return "affiliationChanged";
            case 4: return "superseded";
            case 5: return "cessationOfOperation";
            case 6: return "certificateHold";
            case 8: return "removeFromCRL";
            case 9: return "privilegeWithdrawn";
            case 10: return "aACompromise";
            default: return "Unknown (" + std::to_string(revocationReason_.value()) + ")";
        }
    }

    bool isCertificateRevoked() const {
        return status_ == CrlCheckStatus::REVOKED;
    }

    bool hasCrlVerificationFailed() const {
        return status_ == CrlCheckStatus::CRL_INVALID ||
               status_ == CrlCheckStatus::CRL_UNAVAILABLE ||
               status_ == CrlCheckStatus::CRL_EXPIRED;
    }
};

} // namespace pa::domain::model

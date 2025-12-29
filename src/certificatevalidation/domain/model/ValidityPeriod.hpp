/**
 * @file ValidityPeriod.hpp
 * @brief Value Object for certificate validity period
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <chrono>
#include <stdexcept>

namespace certificatevalidation::domain::model {

/**
 * @brief Certificate validity period Value Object
 *
 * Contains the certificate validity period (notBefore, notAfter).
 */
class ValidityPeriod : public shared::domain::ValueObject {
private:
    std::chrono::system_clock::time_point notBefore_;
    std::chrono::system_clock::time_point notAfter_;

    ValidityPeriod(
        std::chrono::system_clock::time_point notBefore,
        std::chrono::system_clock::time_point notAfter
    ) : notBefore_(notBefore), notAfter_(notAfter) {
        validate();
    }

    void validate() const {
        if (notAfter_ < notBefore_) {
            throw std::invalid_argument("notAfter cannot be before notBefore");
        }
    }

public:
    /**
     * @brief Create ValidityPeriod
     */
    static ValidityPeriod of(
        std::chrono::system_clock::time_point notBefore,
        std::chrono::system_clock::time_point notAfter
    ) {
        return ValidityPeriod(notBefore, notAfter);
    }

    // Getters
    [[nodiscard]] std::chrono::system_clock::time_point getNotBefore() const noexcept {
        return notBefore_;
    }

    [[nodiscard]] std::chrono::system_clock::time_point getNotAfter() const noexcept {
        return notAfter_;
    }

    /**
     * @brief Check if certificate is currently valid
     * @return true if notBefore <= now <= notAfter
     */
    [[nodiscard]] bool isCurrentlyValid() const noexcept {
        auto now = std::chrono::system_clock::now();
        return now >= notBefore_ && now <= notAfter_;
    }

    /**
     * @brief Check if certificate has expired
     * @return true if now > notAfter
     */
    [[nodiscard]] bool isExpired() const noexcept {
        return std::chrono::system_clock::now() > notAfter_;
    }

    /**
     * @brief Check if certificate is not yet valid
     * @return true if now < notBefore
     */
    [[nodiscard]] bool isNotYetValid() const noexcept {
        return std::chrono::system_clock::now() < notBefore_;
    }

    /**
     * @brief Calculate days until expiration
     * @return Number of days (negative if expired)
     */
    [[nodiscard]] long daysUntilExpiration() const noexcept {
        auto now = std::chrono::system_clock::now();
        auto duration = notAfter_ - now;
        return std::chrono::duration_cast<std::chrono::hours>(duration).count() / 24;
    }

    /**
     * @brief Check if certificate is expiring soon (within 30 days)
     */
    [[nodiscard]] bool isExpiringSoon() const noexcept {
        return isExpiringSoon(30);
    }

    /**
     * @brief Check if certificate is expiring within given days
     */
    [[nodiscard]] bool isExpiringSoon(int daysThreshold) const noexcept {
        if (isExpired()) return false;
        return daysUntilExpiration() <= daysThreshold;
    }

    /**
     * @brief Calculate validity period duration in days
     */
    [[nodiscard]] long validityDurationDays() const noexcept {
        auto duration = notAfter_ - notBefore_;
        return std::chrono::duration_cast<std::chrono::hours>(duration).count() / 24;
    }

    bool operator==(const ValidityPeriod& other) const noexcept {
        return notBefore_ == other.notBefore_ && notAfter_ == other.notAfter_;
    }
};

} // namespace certificatevalidation::domain::model

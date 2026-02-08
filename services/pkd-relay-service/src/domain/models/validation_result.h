#pragma once

#include <string>
#include <chrono>

namespace icao::relay::domain {

/**
 * @brief ValidationResult domain model for certificate validation
 *
 * Represents a certificate validation result with expiration tracking.
 * Used for certificate re-validation operations.
 */
class ValidationResult {
public:
    ValidationResult(
        const std::string& id,
        const std::string& certificate_id,
        const std::string& certificate_type,
        const std::string& country_code,
        bool validity_period_valid,
        const std::string& validation_status,
        const std::string& not_after
    )
        : id_(id),
          certificate_id_(certificate_id),
          certificate_type_(certificate_type),
          country_code_(country_code),
          validity_period_valid_(validity_period_valid),
          validation_status_(validation_status),
          not_after_(not_after)
    {}

    // Getters
    const std::string& getId() const { return id_; }
    const std::string& getCertificateId() const { return certificate_id_; }
    const std::string& getCertificateType() const { return certificate_type_; }
    const std::string& getCountryCode() const { return country_code_; }
    bool isValidityPeriodValid() const { return validity_period_valid_; }
    const std::string& getValidationStatus() const { return validation_status_; }
    const std::string& getNotAfter() const { return not_after_; }

    // Check if certificate is expired (based on current time)
    bool isExpired() const;

private:
    std::string id_;
    std::string certificate_id_;
    std::string certificate_type_;
    std::string country_code_;
    bool validity_period_valid_;        // TRUE = valid, FALSE = expired
    std::string validation_status_;     // VALID, INVALID, PENDING
    std::string not_after_;             // Expiration date (ISO 8601 format)
};

/**
 * @brief Result of certificate revalidation operation
 */
struct RevalidationResult {
    int totalProcessed = 0;
    int newlyExpired = 0;
    int newlyValid = 0;
    int unchanged = 0;
    int errors = 0;
    int durationMs = 0;
};

} // namespace icao::relay::domain

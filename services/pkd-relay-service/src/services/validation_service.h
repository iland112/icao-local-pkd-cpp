/**
 * @file validation_service.h
 * @brief Service for certificate validation and revalidation
 */
#pragma once

#include "../repositories/validation_repository.h"
#include "../domain/models/validation_result.h"
#include <memory>
#include <json/json.h>

namespace icao::relay::services {

/**
 * @brief Service for certificate validation and revalidation operations
 *
 * Encapsulates business logic for certificate expiration checking and
 * validation status updates. Uses ValidationRepository for data access.
 */
class ValidationService {
public:
    /**
     * @brief Constructor with Repository dependency injection
     * @param validationRepo Validation repository for data access
     */
    explicit ValidationService(repositories::ValidationRepository* validationRepo);

    /**
     * @brief Revalidate all certificates with expiration information
     *
     * Checks all certificates for expiration status changes:
     * - Newly expired: validity_period_valid TRUE → FALSE
     * - Newly valid: validity_period_valid FALSE → TRUE
     * - Unchanged: no status change
     *
     * Updates validation_status field accordingly:
     * - Expired → INVALID
     * - Valid → VALID (if other checks passed) or PENDING
     *
     * @return JSON response with revalidation results
     */
    Json::Value revalidateAll();

private:
    repositories::ValidationRepository* validationRepo_;

    /**
     * @brief Determine validation status based on expiration and previous status
     * @param isExpired Current expiration status
     * @param currentStatus Current validation status
     * @return New validation status (VALID/INVALID/PENDING)
     */
    std::string determineValidationStatus(bool isExpired, const std::string& currentStatus);
};

} // namespace icao::relay::services

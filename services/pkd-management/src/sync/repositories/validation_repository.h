/**
 * @file validation_repository.h
 * @brief Repository for certificate validation operations
 */
#pragma once

#include "../domain/models/validation_result.h"
#include "i_query_executor.h"
#include <memory>
#include <vector>
#include <optional>

namespace icao::relay::repositories {

/**
 * @brief Repository for certificate validation operations
 *
 * Database-agnostic repository using Query Executor pattern.
 * Supports both PostgreSQL and Oracle databases.
 */
class ValidationRepository {
public:
    /**
     * @brief Constructor with Query Executor dependency injection
     * @param queryExecutor Database query executor (PostgreSQL or Oracle)
     */
    explicit ValidationRepository(common::IQueryExecutor* queryExecutor);

    /**
     * @brief Find all validation results with expiration info
     * @return Vector of ValidationResult objects
     */
    std::vector<domain::ValidationResult> findAllWithExpirationInfo();

    /**
     * @brief Update validity period status for a validation result
     * @param id Validation result ID
     * @param validityPeriodValid TRUE if valid, FALSE if expired
     * @param newStatus New validation status (VALID/INVALID)
     * @return True if update successful
     */
    bool updateValidityStatus(const std::string& id, bool validityPeriodValid, const std::string& newStatus);

    /**
     * @brief Count expired certificates by upload ID
     * @param uploadId Upload file ID
     * @return Number of expired certificates
     */
    int countExpiredByUploadId(const std::string& uploadId);

    /**
     * @brief Update expired count for all upload files
     * @return Number of upload files updated
     */
    int updateAllUploadExpiredCounts();

    /**
     * @brief Save revalidation result to history
     * @param totalProcessed Number of validation results processed
     * @param newlyExpired Number of newly expired certificates
     * @param newlyValid Number of newly valid certificates
     * @param unchanged Number of unchanged certificates
     * @param errors Number of errors encountered
     * @param durationMs Duration in milliseconds
     * @return True if save successful
     */
    bool saveRevalidationHistory(
        int totalProcessed,
        int newlyExpired,
        int newlyValid,
        int unchanged,
        int errors,
        int durationMs
    );

    /**
     * @brief Save extended revalidation history with trust chain and CRL results
     */
    bool saveRevalidationHistoryExtended(
        int totalProcessed, int newlyExpired, int newlyValid,
        int unchanged, int errors, int durationMs,
        int tcProcessed, int tcNewlyValid, int tcStillPending, int tcErrors,
        int crlChecked, int crlRevoked, int crlUnavailable, int crlExpired, int crlErrors
    );

    /**
     * @brief Find DSCs for trust chain re-validation
     * Returns DSC validation_result rows where csca_found = FALSE (PENDING/INVALID)
     * @return JSON array with id, certificate_id, country_code, certificate_data
     */
    Json::Value findDscsForTrustChainRevalidation();

    /**
     * @brief Find DSCs for CRL re-check
     * Returns VALID/EXPIRED_VALID DSC validation_result rows
     * @return JSON array with id, certificate_id, country_code, certificate_data
     */
    Json::Value findDscsForCrlRecheck();

    /**
     * @brief Update trust chain status after re-validation
     */
    bool updateTrustChainStatus(const std::string& id, const std::string& newStatus,
                                 bool cscaFound, const std::string& trustChainMessage);

    /**
     * @brief Update CRL check status (mark as INVALID if revoked)
     */
    bool updateCrlStatus(const std::string& id, const std::string& newStatus,
                          const std::string& crlMessage);

private:
    common::IQueryExecutor* queryExecutor_;
};

} // namespace icao::relay::repositories

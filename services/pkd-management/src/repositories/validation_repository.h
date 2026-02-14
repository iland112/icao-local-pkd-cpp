#pragma once

#include <string>
#include <vector>
#include <json/json.h>
#include "i_query_executor.h"
#include "../domain/models/validation_result.h"
#include "../domain/models/validation_statistics.h"

/**
 * @file validation_repository.h
 * @brief Validation Repository - Database Access Layer for validation_result table
 *
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @note Part of Oracle migration Phase 3: Query Executor Pattern
 * @date 2026-02-04
 */

namespace repositories {

class ValidationRepository {
public:
    /**
     * @brief Constructor
     * @param queryExecutor Query executor (PostgreSQL or Oracle, non-owning pointer)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit ValidationRepository(common::IQueryExecutor* queryExecutor);
    ~ValidationRepository() = default;

    /**
     * @brief Save validation result (Phase 4.4)
     * @param result ValidationResult domain model
     * @return true if save successful, false otherwise
     *
     * Saves complete validation result with 22 fields including trust chain,
     * signature verification, validity period, and CRL check status.
     */
    bool save(const domain::models::ValidationResult& result);

    /**
     * @brief Update validation statistics for an upload
     * @param uploadId Upload UUID
     * @param stats ValidationStatistics with aggregated counts
     * @return true if update successful, false otherwise
     *
     * Updates the uploaded_file table with validation statistics including
     * validation status counts, trust chain counts, and specific failure counts.
     */
    bool updateStatistics(const std::string& uploadId,
                         const domain::models::ValidationStatistics& stats);

    /**
     * @brief Find validation by fingerprint
     */
    Json::Value findByFingerprint(const std::string& fingerprint);

    /**
     * @brief Find validations by upload ID with pagination
     * @param uploadId Upload UUID
     * @param limit Maximum results
     * @param offset Pagination offset
     * @param statusFilter Filter by validation_status (VALID/INVALID/PENDING)
     * @param certTypeFilter Filter by certificate_type (DSC/DSC_NC)
     * @return JSON object with count, total, limit, offset, validations array
     */
    Json::Value findByUploadId(
        const std::string& uploadId,
        int limit,
        int offset,
        const std::string& statusFilter = "",
        const std::string& certTypeFilter = ""
    );

    /**
     * @brief Count validations by status
     */
    int countByStatus(const std::string& status);

    /**
     * @brief Get validation statistics for an upload
     * @param uploadId Upload UUID
     * @return JSON object with validation statistics
     *
     * Response format:
     * {
     *   "totalCount": 29838,
     *   "validCount": 16788,
     *   "invalidCount": 6696,
     *   "pendingCount": 6354,
     *   "errorCount": 0,
     *   "trustChainValidCount": 16788,
     *   "trustChainInvalidCount": 12050,
     *   "trustChainSuccessRate": 56.2
     * }
     */
    Json::Value getStatisticsByUploadId(const std::string& uploadId);

    /**
     * @brief Get validation reason breakdown (GROUP BY validation_status, trust_chain_message)
     * @return JSON object with grouped reason counts
     *
     * Response format:
     * {
     *   "reasons": [
     *     { "status": "INVALID", "reason": "Trust chain signature verification failed", "count": 103, "countryCode": "..." },
     *     { "status": "PENDING", "reason": "CSCA not found in database", "count": 7, "countryCode": "LU" },
     *     ...
     *   ]
     * }
     */
    Json::Value getReasonBreakdown();

    /**
     * @brief Update validation result for revalidation
     * @param certificateId Certificate UUID
     * @param validationStatus New validation status (VALID/INVALID)
     * @param trustChainValid Whether trust chain is valid
     * @param cscaFound Whether CSCA was found
     * @param signatureValid Whether signature is valid
     * @param trustChainMessage Trust chain path or error message
     * @param cscaSubjectDn CSCA subject DN if found
     * @return true if update successful
     */
    bool updateRevalidation(const std::string& certificateId,
                           const std::string& validationStatus,
                           bool trustChainValid,
                           bool cscaFound,
                           bool signatureValid,
                           const std::string& trustChainMessage,
                           const std::string& cscaSubjectDn);

private:
    common::IQueryExecutor* queryExecutor_;  // Query executor (non-owning)
};

} // namespace repositories

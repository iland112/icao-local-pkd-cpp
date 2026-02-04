#pragma once

#include <string>
#include <vector>
#include <libpq-fe.h>
#include <json/json.h>
#include "db_connection_pool.h"
#include "../domain/models/validation_result.h"
#include "../domain/models/validation_statistics.h"

/**
 * @file validation_repository.h
 * @brief Validation Repository - Database Access Layer for validation_result table
 *
 * @note Part of main.cpp refactoring Phase 1.5 - Phase 4.4
 * @date 2026-01-29 (Updated: 2026-01-30)
 */

namespace repositories {

class ValidationRepository {
public:
    explicit ValidationRepository(common::DbConnectionPool* dbPool);
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

private:
    common::DbConnectionPool* dbPool_;  // Database connection pool (non-owning)

    PGresult* executeParamQuery(const std::string& query, const std::vector<std::string>& params);
    PGresult* executeQuery(const std::string& query);
    Json::Value pgResultToJson(PGresult* res);
};

} // namespace repositories

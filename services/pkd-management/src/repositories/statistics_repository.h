#pragma once

#include <string>
#include <vector>
#include <json/json.h>
#include "i_query_executor.h"

/**
 * @file statistics_repository.h
 * @brief Statistics Repository - Database Access Layer for statistical queries
 *
 * Handles complex aggregation queries for statistics and analytics.
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @note Part of Oracle migration Phase 3: Query Executor Pattern
 * @date 2026-02-04
 */

namespace repositories {

class StatisticsRepository {
public:
    /**
     * @brief Constructor
     * @param queryExecutor Query executor (PostgreSQL or Oracle, non-owning pointer)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit StatisticsRepository(common::IQueryExecutor* queryExecutor);
    ~StatisticsRepository() = default;

    /**
     * @brief Get upload statistics summary
     */
    Json::Value getUploadStatistics();

    /**
     * @brief Get certificate statistics by type
     */
    Json::Value getCertificateStatistics();

    /**
     * @brief Get country statistics (summary)
     */
    Json::Value getCountryStatistics();

    /**
     * @brief Get detailed country statistics with breakdown
     * @param limit Maximum countries to return (0 = all)
     */
    Json::Value getDetailedCountryStatistics(int limit);

    /**
     * @brief Get validation statistics
     */
    Json::Value getValidationStatistics();

    /**
     * @brief Get system-wide statistics
     */
    Json::Value getSystemStatistics();

private:
    common::IQueryExecutor* queryExecutor_;  // Query executor (non-owning)
};

} // namespace repositories

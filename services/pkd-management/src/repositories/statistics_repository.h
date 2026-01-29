#pragma once

#include <string>
#include <vector>
#include <libpq-fe.h>
#include <json/json.h>

/**
 * @file statistics_repository.h
 * @brief Statistics Repository - Database Access Layer for statistical queries
 *
 * Handles complex aggregation queries for statistics and analytics.
 * Database-agnostic interface (currently PostgreSQL, future: Oracle support).
 *
 * @note Part of main.cpp refactoring Phase 1.5
 * @date 2026-01-29
 */

namespace repositories {

class StatisticsRepository {
public:
    explicit StatisticsRepository(PGconn* dbConn);
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
    PGconn* dbConn_;

    PGresult* executeQuery(const std::string& query);
    Json::Value pgResultToJson(PGresult* res);
};

} // namespace repositories

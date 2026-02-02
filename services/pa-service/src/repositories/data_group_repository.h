/**
 * @file data_group_repository.h
 * @brief Repository for Data Group database operations
 *
 * Handles CRUD operations for pa_data_group table in PostgreSQL.
 * Uses 100% parameterized queries for security.
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <vector>
#include <json/json.h>
#include <libpq-fe.h>
#include "../domain/models/data_group.h"

namespace repositories {

/**
 * @brief Data Group Repository
 *
 * Responsibilities:
 * - CRUD operations for pa_data_group table
 * - Retrieve data groups by verification ID
 * - 100% parameterized queries
 * - Exception-based error handling
 */
class DataGroupRepository {
private:
    PGconn* dbConn_;  // Database connection (non-owning)

public:
    /**
     * @brief Constructor
     * @param conn PostgreSQL connection (must be open and valid)
     */
    explicit DataGroupRepository(PGconn* conn);

    /**
     * @brief Destructor
     */
    ~DataGroupRepository() = default;

    // Delete copy constructor and assignment
    DataGroupRepository(const DataGroupRepository&) = delete;
    DataGroupRepository& operator=(const DataGroupRepository&) = delete;

    // ==========================================================================
    // Query Methods
    // ==========================================================================

    /**
     * @brief Find all data groups for a verification ID
     * @param verificationId PA verification UUID
     * @return JSON array of data groups
     */
    Json::Value findByVerificationId(const std::string& verificationId);

    /**
     * @brief Find single data group by ID
     * @param id Data group UUID
     * @return JSON object or null if not found
     */
    Json::Value findById(const std::string& id);

    /**
     * @brief Insert data group
     * @param dg DataGroup domain model
     * @param verificationId PA verification UUID
     * @return Inserted data group ID (UUID)
     */
    std::string insert(const domain::models::DataGroup& dg, const std::string& verificationId);

    /**
     * @brief Delete all data groups for a verification
     * @param verificationId PA verification UUID
     * @return Number of deleted rows
     */
    int deleteByVerificationId(const std::string& verificationId);

    // ==========================================================================
    // Helper Methods
    // ==========================================================================

private:
    /**
     * @brief Execute parameterized query
     * @param query SQL query with $1, $2, ... placeholders
     * @param params Parameter values
     * @return PGresult* (caller must PQclear)
     */
    PGresult* executeParamQuery(
        const std::string& query,
        const std::vector<std::string>& params
    );

    /**
     * @brief Convert PostgreSQL result row to DataGroup JSON
     * @param res PostgreSQL result
     * @param row Row index
     * @return JSON object representing a data group
     */
    Json::Value resultToDataGroupJson(PGresult* res, int row);
};

} // namespace repositories

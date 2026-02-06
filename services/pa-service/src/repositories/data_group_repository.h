/**
 * @file data_group_repository.h
 * @brief Repository for Data Group database operations (Database-agnostic)
 *
 * Handles CRUD operations for pa_data_group table.
 * Uses Query Executor Pattern for database independence (PostgreSQL/Oracle).
 * Uses 100% parameterized queries for security.
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 * @updated 2026-02-05 (Phase 5.1.3: Query Executor Pattern)
 */

#pragma once

#include <string>
#include <vector>
#include <json/json.h>
#include <models/data_group.h>
#include "i_query_executor.h"

namespace repositories {

/**
 * @brief Data Group Repository (Database-agnostic)
 *
 * Responsibilities:
 * - CRUD operations for pa_data_group table
 * - Retrieve data groups by verification ID
 * - 100% parameterized queries
 * - Exception-based error handling
 * - Database independence via Query Executor interface
 */
class DataGroupRepository {
private:
    common::IQueryExecutor* queryExecutor_;  // Not owned - do not free

public:
    /**
     * @brief Constructor with Query Executor injection
     * @param executor Query Executor (PostgreSQL or Oracle, must remain valid during repository lifetime)
     * @throws std::invalid_argument if executor is nullptr
     */
    explicit DataGroupRepository(common::IQueryExecutor* executor);

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
    std::string insert(const icao::models::DataGroup& dg, const std::string& verificationId);

    /**
     * @brief Delete all data groups for a verification
     * @param verificationId PA verification UUID
     * @return Number of deleted rows
     */
    int deleteByVerificationId(const std::string& verificationId);

private:
    /**
     * @brief Convert database row (snake_case) to camelCase JSON for frontend
     * @param dbRow Database result row
     * @return Camel case JSON object
     */
    Json::Value toCamelCase(const Json::Value& dbRow);
};

} // namespace repositories

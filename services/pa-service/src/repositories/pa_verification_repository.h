/**
 * @file pa_verification_repository.h
 * @brief Repository for PA verification records (Database-agnostic)
 *
 * Handles all database access for pa_verification table.
 * Follows Repository Pattern with Query Executor abstraction.
 * Supports both PostgreSQL and Oracle.
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 * @updated 2026-02-05 (Phase 5.1: Query Executor Pattern)
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "../domain/models/pa_verification.h"
#include "i_query_executor.h"

namespace repositories {

/**
 * @brief PA Verification Repository
 *
 * Responsibilities:
 * - CRUD operations on pa_verification table
 * - Parameterized SQL queries (100% SQL injection protection)
 * - JSON response formatting for API
 * - Database-agnostic via Query Executor interface
 */
class PaVerificationRepository {
private:
    common::IQueryExecutor* queryExecutor_;  // Not owned - do not free

public:
    /**
     * @brief Constructor with Query Executor injection
     * @param executor Query Executor (PostgreSQL or Oracle, must remain valid during repository lifetime)
     * @throws std::invalid_argument if executor is nullptr
     */
    explicit PaVerificationRepository(common::IQueryExecutor* executor);

    /**
     * @brief Destructor
     */
    ~PaVerificationRepository() = default;

    // Prevent copying (connection should not be shared via copy)
    PaVerificationRepository(const PaVerificationRepository&) = delete;
    PaVerificationRepository& operator=(const PaVerificationRepository&) = delete;

    // ==========================================================================
    // CRUD Operations
    // ==========================================================================

    /**
     * @brief Insert new PA verification record
     * @param verification PaVerification domain model
     * @return UUID of inserted record
     * @throws std::runtime_error on database error
     */
    std::string insert(const domain::models::PaVerification& verification);

    /**
     * @brief Find PA verification by ID
     * @param id UUID
     * @return JSON representation or null if not found
     */
    Json::Value findById(const std::string& id);

    /**
     * @brief Find all PA verifications with filtering and pagination
     * @param limit Maximum results to return
     * @param offset Offset for pagination
     * @param status Filter by status ("VALID", "INVALID", "ERROR") - empty for all
     * @param countryCode Filter by country code - empty for all
     * @return JSON with {"success", "data", "total", "page", "size"}
     */
    Json::Value findAll(
        int limit = 50,
        int offset = 0,
        const std::string& status = "",
        const std::string& countryCode = ""
    );

    /**
     * @brief Get PA verification statistics
     * @return JSON with total counts by status, country, etc.
     */
    Json::Value getStatistics();

    /**
     * @brief Delete PA verification by ID
     * @param id UUID
     * @return true if deleted, false if not found
     */
    bool deleteById(const std::string& id);

    /**
     * @brief Update verification status
     * @param id UUID
     * @param status New status
     * @return true if updated
     */
    bool updateStatus(const std::string& id, const std::string& status);

private:
    /**
     * @brief Build WHERE clause from filters
     * @param status Status filter
     * @param countryCode Country filter
     * @param params Output parameter vector
     * @return WHERE clause string (without "WHERE" keyword)
     */
    std::string buildWhereClause(
        const std::string& status,
        const std::string& countryCode,
        std::vector<std::string>& params
    );

    /**
     * @brief Convert database row (snake_case) to camelCase JSON for frontend
     * @param dbRow Database result row
     * @return Camel case JSON object
     */
    Json::Value toCamelCase(const Json::Value& dbRow);
};

} // namespace repositories

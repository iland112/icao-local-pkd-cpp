/**
 * @file pa_verification_repository.h
 * @brief Repository for PA verification records in PostgreSQL
 *
 * Handles all database access for pa_verification table.
 * Follows Repository Pattern with constructor-based dependency injection.
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include <libpq-fe.h>
#include "../domain/models/pa_verification.h"
#include "db_connection_pool.h"

namespace repositories {

/**
 * @brief PA Verification Repository
 *
 * Responsibilities:
 * - CRUD operations on pa_verification table
 * - Parameterized SQL queries (100% SQL injection protection)
 * - JSON response formatting for API
 * - Thread-safe database access via connection pool
 */
class PaVerificationRepository {
private:
    common::DbConnectionPool* dbPool_;  // Not owned - do not free

public:
    /**
     * @brief Constructor with database connection pool injection
     * @param pool Database connection pool (must remain valid during repository lifetime)
     * @throws std::invalid_argument if pool is nullptr
     */
    explicit PaVerificationRepository(common::DbConnectionPool* pool);

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

    // ==========================================================================
    // Helper Methods
    // ==========================================================================

    /**
     * @brief Execute parameterized query
     * @param query SQL query with $1, $2, ... placeholders
     * @param params Parameter values
     * @return PGresult* (caller must PQclear)
     * @throws std::runtime_error on query error
     */
    PGresult* executeParamQuery(
        const std::string& query,
        const std::vector<std::string>& params
    );

    /**
     * @brief Execute simple query (no parameters)
     * @param query SQL query
     * @return PGresult* (caller must PQclear)
     * @throws std::runtime_error on query error
     */
    PGresult* executeQuery(const std::string& query);

    /**
     * @brief Convert PostgreSQL result to JSON array
     * @param res PGresult from query
     * @return JSON array of objects
     */
    Json::Value pgResultToJson(PGresult* res);

    /**
     * @brief Convert single row to PaVerification domain model
     * @param res PGresult
     * @param row Row number
     * @return PaVerification instance
     */
    domain::models::PaVerification resultToVerification(PGresult* res, int row);

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
};

} // namespace repositories

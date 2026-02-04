#pragma once

#include "i_query_executor.h"
#include "db_connection_pool.h"
#include <libpq-fe.h>

/**
 * @file postgresql_query_executor.h
 * @brief PostgreSQL Query Executor - libpq-based implementation
 *
 * Implements IQueryExecutor using PostgreSQL libpq API.
 * Handles connection acquisition from pool, query execution,
 * result parsing, and JSON conversion.
 *
 * @note Part of Oracle migration Phase 3
 * @date 2026-02-04
 */

namespace common {

/**
 * @brief PostgreSQL-specific query executor
 *
 * Uses DbConnectionPool for connection management and libpq for query execution.
 * Converts PGresult* to Json::Value for database-agnostic Repository code.
 */
class PostgreSQLQueryExecutor : public IQueryExecutor {
public:
    /**
     * @brief Construct PostgreSQL query executor
     * @param pool PostgreSQL connection pool
     * @throws std::invalid_argument if pool is nullptr
     */
    explicit PostgreSQLQueryExecutor(DbConnectionPool* pool);

    ~PostgreSQLQueryExecutor() override = default;

    /**
     * @brief Execute SELECT query with parameterized binding
     *
     * Uses PQexecParams for safe parameter binding ($1, $2, etc.).
     * Converts PGresult* to JSON array.
     *
     * @param query SQL query with $1, $2 placeholders
     * @param params Query parameters
     * @return JSON array of result rows
     * @throws std::runtime_error on execution or conversion failure
     */
    Json::Value executeQuery(
        const std::string& query,
        const std::vector<std::string>& params = {}
    ) override;

    /**
     * @brief Execute INSERT/UPDATE/DELETE command
     *
     * @param query SQL command with $1, $2 placeholders
     * @param params Query parameters
     * @return Number of affected rows (from PQcmdTuples)
     * @throws std::runtime_error on execution failure
     */
    int executeCommand(
        const std::string& query,
        const std::vector<std::string>& params
    ) override;

    /**
     * @brief Execute scalar query (single value)
     *
     * @param query SQL query returning single value
     * @param params Query parameters
     * @return Single JSON value
     * @throws std::runtime_error if result is empty or has multiple columns
     */
    Json::Value executeScalar(
        const std::string& query,
        const std::vector<std::string>& params = {}
    ) override;

    /**
     * @brief Get database type
     * @return "postgres"
     */
    std::string getDatabaseType() const override { return "postgres"; }

private:
    DbConnectionPool* pool_;  ///< PostgreSQL connection pool

    /**
     * @brief Convert PGresult to JSON array
     *
     * Handles type conversion:
     * - INT4, INT8 → JSON number
     * - FLOAT4, FLOAT8 → JSON number
     * - BOOL → JSON boolean
     * - NULL → JSON null
     * - Others → JSON string
     *
     * @param res PostgreSQL result set
     * @return JSON array of rows
     */
    Json::Value pgResultToJson(PGresult* res);

    /**
     * @brief Execute raw parameterized query
     *
     * Internal helper that returns PGresult*.
     * Caller must PQclear() the result.
     *
     * @param query SQL query
     * @param params Query parameters
     * @return PGresult* (caller owns, must clear)
     * @throws std::runtime_error on execution failure
     */
    PGresult* executeRawQuery(
        const std::string& query,
        const std::vector<std::string>& params
    );
};

} // namespace common

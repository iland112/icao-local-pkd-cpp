#pragma once

#include <string>
#include <vector>
#include <memory>
#include <json/json.h>

/**
 * @file i_query_executor.h
 * @brief Query Executor Interface - Database-agnostic query execution
 *
 * Provides abstraction for executing SQL queries across different database systems.
 * Implementations handle database-specific APIs (PostgreSQL libpq, Oracle OTL, etc.)
 * and return results in a standardized JSON format.
 *
 * @note Part of Oracle migration Phase 3 - Repository Layer Abstraction
 * @date 2026-02-04
 */

namespace common {

/**
 * @brief Query Executor Interface
 *
 * Abstracts database-specific query execution. Repository classes use this interface
 * instead of directly calling database APIs (PQexec, OTL, etc.).
 *
 * Benefits:
 * - Database-agnostic Repository code
 * - Easy testing with mock executors
 * - Centralized query execution logic
 * - Consistent error handling
 */
class IQueryExecutor {
public:
    virtual ~IQueryExecutor() = default;

    /**
     * @brief Execute SELECT query and return results as JSON array
     *
     * @param query SQL query string (with $1, $2 placeholders for PostgreSQL, :param for Oracle)
     * @param params Query parameters (optional)
     * @return Json::Value Array of result rows, each row is a JSON object with column name-value pairs
     *
     * Example result:
     * [
     *   {"id": "123", "name": "John", "age": 30},
     *   {"id": "456", "name": "Jane", "age": 25}
     * ]
     *
     * @throws std::runtime_error on query execution failure
     */
    virtual Json::Value executeQuery(
        const std::string& query,
        const std::vector<std::string>& params = {}
    ) = 0;

    /**
     * @brief Execute INSERT/UPDATE/DELETE command
     *
     * @param query SQL command string
     * @param params Query parameters
     * @return Number of affected rows
     *
     * @throws std::runtime_error on command execution failure
     */
    virtual int executeCommand(
        const std::string& query,
        const std::vector<std::string>& params
    ) = 0;

    /**
     * @brief Execute query and return single scalar value
     *
     * Convenience method for queries that return a single value (e.g., COUNT, SUM).
     *
     * @param query SQL query string
     * @param params Query parameters (optional)
     * @return Single value as JSON (string, number, bool, or null)
     *
     * Example: executeScalar("SELECT COUNT(*) FROM users") -> 42
     *
     * @throws std::runtime_error if query returns no rows or multiple columns
     */
    virtual Json::Value executeScalar(
        const std::string& query,
        const std::vector<std::string>& params = {}
    ) = 0;

    /**
     * @brief Get database type (for diagnostic purposes)
     * @return "postgres" or "oracle"
     */
    virtual std::string getDatabaseType() const = 0;
};

/**
 * @brief Factory function to create appropriate QueryExecutor
 *
 * Creates PostgreSQLQueryExecutor or OracleQueryExecutor based on
 * the connection pool type.
 *
 * @param pool Database connection pool (IDbConnectionPool)
 * @return Unique pointer to IQueryExecutor implementation
 * @throws std::runtime_error if pool type is unsupported
 */
std::unique_ptr<IQueryExecutor> createQueryExecutor(class IDbConnectionPool* pool);

} // namespace common

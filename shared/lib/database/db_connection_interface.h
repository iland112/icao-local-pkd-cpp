/**
 * @file db_connection_interface.h
 * @brief Database-agnostic connection pool interfaces
 *
 * Provides abstract interfaces for database connections and connection pools
 * to support multiple database backends (PostgreSQL, Oracle, etc.)
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include <string>
#include <memory>
#include <chrono>

namespace common {

/**
 * @brief Abstract database connection interface
 *
 * Provides database-agnostic access to connections
 */
class IDbConnection {
public:
    virtual ~IDbConnection() = default;

    /**
     * @brief Check if connection is valid
     */
    virtual bool isValid() const = 0;

    /**
     * @brief Get database type identifier
     * @return "postgres", "oracle", etc.
     */
    virtual std::string getDatabaseType() const = 0;

    /**
     * @brief Execute raw SQL query
     * @param sql SQL query string
     * @return true if successful
     *
     * Note: For type-safe queries, use Repository pattern
     */
    virtual bool execute(const std::string& sql) = 0;

    /**
     * @brief Manually release connection back to pool
     */
    virtual void release() = 0;

protected:
    // Protected to prevent direct instantiation
    IDbConnection() = default;
};

/**
 * @brief Abstract database connection pool interface
 *
 * Provides database-agnostic connection pooling
 */
class IDbConnectionPool {
public:
    virtual ~IDbConnectionPool() = default;

    /**
     * @brief Initialize connection pool
     * @return true if successfully created minimum connections
     */
    virtual bool initialize() = 0;

    /**
     * @brief Acquire connection from pool
     * @return Connection wrapper (may be invalid on timeout)
     */
    virtual std::unique_ptr<IDbConnection> acquireGeneric() = 0;

    /**
     * @brief Get pool statistics
     */
    struct Stats {
        size_t availableConnections;
        size_t totalConnections;
        size_t maxConnections;
    };

    virtual Stats getStats() const = 0;

    /**
     * @brief Shutdown pool and close all connections
     */
    virtual void shutdown() = 0;

    /**
     * @brief Get database type
     */
    virtual std::string getDatabaseType() const = 0;

protected:
    // Protected to prevent direct instantiation
    IDbConnectionPool() = default;
};

} // namespace common

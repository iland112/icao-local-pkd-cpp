/**
 * @file oracle_connection_pool.h
 * @brief Oracle Connection Pool Manager using OTL
 *
 * Thread-safe connection pooling for Oracle database using OTL (Oracle Template Library)
 * Features:
 * - Configurable pool size (min/max connections)
 * - Connection timeout handling
 * - Automatic connection health checking
 * - Connection recycling
 * - Thread-safe acquire/release
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include "db_connection_interface.h"
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>
#include <atomic>

// Forward declaration (OTL will be included in .cpp)
namespace otl_internal {
    class otl_connect;
}

namespace common {

// Forward declaration
class OracleConnectionPool;

/**
 * @brief RAII wrapper for Oracle connection
 *
 * Automatically returns connection to pool when destroyed
 */
class OracleConnection : public IDbConnection {
private:
    void* conn_;  // otl_connect* (void* to avoid exposing OTL in header)
    OracleConnectionPool* pool_;  // Non-owning pointer to pool
    bool released_;

public:
    OracleConnection(void* conn, OracleConnectionPool* pool);
    ~OracleConnection() override;

    // Delete copy constructor and assignment
    OracleConnection(const OracleConnection&) = delete;
    OracleConnection& operator=(const OracleConnection&) = delete;

    // Move constructor and assignment
    OracleConnection(OracleConnection&& other) noexcept;
    OracleConnection& operator=(OracleConnection&& other) noexcept;

    /**
     * @brief Get raw Oracle connection (as void*)
     * Cast to otl_connect* when using
     */
    void* get() const { return conn_; }

    /**
     * @brief Check if connection is valid
     */
    bool isValid() const override;

    /**
     * @brief Get database type
     */
    std::string getDatabaseType() const override { return "oracle"; }

    /**
     * @brief Execute raw SQL query
     */
    bool execute(const std::string& sql) override;

    /**
     * @brief Manually release connection back to pool
     */
    void release() override;
};

/**
 * @brief Oracle Connection Pool using OTL
 *
 * Thread-safe connection pool with configurable size and timeout
 */
class OracleConnectionPool : public IDbConnectionPool {
private:
    std::string connString_;
    size_t minSize_;
    size_t maxSize_;
    std::chrono::seconds acquireTimeout_;

    std::queue<void*> availableConnections_;  // Queue of otl_connect*
    std::atomic<size_t> totalConnections_;
    mutable std::mutex mutex_;  // mutable to allow locking in const methods
    std::condition_variable cv_;

    bool shutdown_;
    bool otlInitialized_;

    friend class OracleConnection;

public:
    /**
     * @brief Constructor
     * @param connString Oracle connection string
     *        Format: "user/password@host:port/service_name"
     * @param minSize Minimum number of connections to maintain
     * @param maxSize Maximum number of connections allowed
     * @param acquireTimeoutSec Timeout for acquiring connection (seconds)
     */
    explicit OracleConnectionPool(
        const std::string& connString,
        size_t minSize = 2,
        size_t maxSize = 10,
        int acquireTimeoutSec = 5
    );

    /**
     * @brief Destructor - closes all connections
     */
    ~OracleConnectionPool() override;

    // Delete copy constructor and assignment
    OracleConnectionPool(const OracleConnectionPool&) = delete;
    OracleConnectionPool& operator=(const OracleConnectionPool&) = delete;

    /**
     * @brief Initialize connection pool
     * @return true if successfully created minimum connections
     */
    bool initialize() override;

    /**
     * @brief Acquire connection from pool (OracleConnection type)
     * @return OracleConnection RAII wrapper (nullptr on timeout)
     * @throws std::runtime_error on pool shutdown or critical error
     */
    OracleConnection acquire();

    /**
     * @brief Acquire connection (generic interface)
     */
    std::unique_ptr<IDbConnection> acquireGeneric() override;

    /**
     * @brief Get pool statistics
     */
    Stats getStats() const override;

    /**
     * @brief Shutdown pool and close all connections
     */
    void shutdown() override;

    /**
     * @brief Get database type
     */
    std::string getDatabaseType() const override { return "oracle"; }

private:
    /**
     * @brief Initialize OTL library (one-time setup)
     */
    void initializeOTL();

    /**
     * @brief Create new Oracle connection
     */
    void* createConnection();  // Returns otl_connect*

    /**
     * @brief Check if connection is healthy
     */
    bool isConnectionHealthy(void* conn);  // otl_connect*

    /**
     * @brief Return connection to pool (called by OracleConnection)
     */
    void releaseConnection(void* conn);  // otl_connect*
};

} // namespace common

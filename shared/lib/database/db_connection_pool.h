/**
 * @file db_connection_pool.h
 * @brief PostgreSQL Connection Pool Manager
 *
 * Thread-safe connection pooling for PostgreSQL database
 * Features:
 * - Configurable pool size (min/max connections)
 * - Connection timeout handling
 * - Automatic connection health checking
 * - Connection recycling
 * - Thread-safe acquire/release
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#pragma once

#include "db_connection_interface.h"
#include <libpq-fe.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>
#include <atomic>

namespace common {

/**
 * @brief RAII wrapper for PostgreSQL connection
 *
 * Automatically returns connection to pool when destroyed
 */
class DbConnection : public IDbConnection {
private:
    PGconn* conn_;
    class DbConnectionPool* pool_;  // Non-owning pointer to pool
    bool released_;

public:
    DbConnection(PGconn* conn, DbConnectionPool* pool)
        : conn_(conn), pool_(pool), released_(false) {}

    ~DbConnection();

    // Delete copy constructor and assignment
    DbConnection(const DbConnection&) = delete;
    DbConnection& operator=(const DbConnection&) = delete;

    // Move constructor and assignment
    DbConnection(DbConnection&& other) noexcept
        : conn_(other.conn_), pool_(other.pool_), released_(other.released_) {
        other.conn_ = nullptr;
        other.released_ = true;
    }

    DbConnection& operator=(DbConnection&& other) noexcept {
        if (this != &other) {
            if (!released_ && conn_) {
                release();
            }
            conn_ = other.conn_;
            pool_ = other.pool_;
            released_ = other.released_;
            other.conn_ = nullptr;
            other.released_ = true;
        }
        return *this;
    }

    /**
     * @brief Get raw PostgreSQL connection
     */
    PGconn* get() const { return conn_; }

    /**
     * @brief Check if connection is valid (IDbConnection interface)
     */
    bool isValid() const override {
        return conn_ != nullptr && !released_;
    }

    /**
     * @brief Get database type (IDbConnection interface)
     */
    std::string getDatabaseType() const override {
        return "postgres";
    }

    /**
     * @brief Execute raw SQL query (IDbConnection interface)
     */
    bool execute(const std::string& sql) override;

    /**
     * @brief Manually release connection back to pool (IDbConnection interface)
     */
    void release() override;
};

/**
 * @brief PostgreSQL Connection Pool
 *
 * Thread-safe connection pool with configurable size and timeout
 */
class DbConnectionPool : public IDbConnectionPool {
private:
    std::string connString_;
    size_t minSize_;
    size_t maxSize_;
    std::chrono::seconds acquireTimeout_;

    std::queue<PGconn*> availableConnections_;
    std::atomic<size_t> totalConnections_;
    mutable std::mutex mutex_;  // mutable to allow locking in const methods
    std::condition_variable cv_;

    bool shutdown_;

    friend class DbConnection;

public:
    /**
     * @brief Constructor
     * @param connString PostgreSQL connection string
     * @param minSize Minimum number of connections to maintain
     * @param maxSize Maximum number of connections allowed
     * @param acquireTimeoutSec Timeout for acquiring connection (seconds)
     */
    explicit DbConnectionPool(
        const std::string& connString,
        size_t minSize = 2,
        size_t maxSize = 10,
        int acquireTimeoutSec = 5
    );

    /**
     * @brief Destructor - closes all connections
     */
    ~DbConnectionPool();

    // Delete copy constructor and assignment
    DbConnectionPool(const DbConnectionPool&) = delete;
    DbConnectionPool& operator=(const DbConnectionPool&) = delete;

    /**
     * @brief Initialize connection pool (IDbConnectionPool interface)
     * @return true if successfully created minimum connections
     */
    bool initialize() override;

    /**
     * @brief Acquire connection from pool (PostgreSQL specific)
     * @return DbConnection RAII wrapper (nullptr on timeout)
     * @throws std::runtime_error on pool shutdown or critical error
     */
    DbConnection acquire();

    /**
     * @brief Acquire connection (IDbConnectionPool interface)
     */
    std::unique_ptr<IDbConnection> acquireGeneric() override;

    /**
     * @brief Get pool statistics (IDbConnectionPool interface)
     */
    Stats getStats() const override;

    /**
     * @brief Shutdown pool and close all connections (IDbConnectionPool interface)
     */
    void shutdown() override;

    /**
     * @brief Get database type (IDbConnectionPool interface)
     */
    std::string getDatabaseType() const override {
        return "postgres";
    }

private:
    /**
     * @brief Create new PostgreSQL connection
     */
    PGconn* createConnection();

    /**
     * @brief Check if connection is healthy
     */
    bool isConnectionHealthy(PGconn* conn);

    /**
     * @brief Return connection to pool (called by DbConnection)
     */
    void releaseConnection(PGconn* conn);
};

} // namespace common

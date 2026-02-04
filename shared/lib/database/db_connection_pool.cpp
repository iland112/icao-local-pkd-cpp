/**
 * @file db_connection_pool.cpp
 * @brief Implementation of PostgreSQL Connection Pool
 */

#include "db_connection_pool.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace common {

// =============================================================================
// DbConnection Implementation
// =============================================================================

DbConnection::~DbConnection() {
    if (!released_ && conn_) {
        release();
    }
}

bool DbConnection::execute(const std::string& sql) {
    if (!isValid()) {
        return false;
    }

    PGresult* res = PQexec(conn_, sql.c_str());
    if (!res) {
        return false;
    }

    ExecStatusType status = PQresultStatus(res);
    PQclear(res);

    return (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
}

void DbConnection::release() {
    if (released_ || !conn_) {
        return;
    }

    if (pool_) {
        pool_->releaseConnection(conn_);
    }

    conn_ = nullptr;
    released_ = true;
}

// =============================================================================
// DbConnectionPool Implementation
// =============================================================================

DbConnectionPool::DbConnectionPool(
    const std::string& connString,
    size_t minSize,
    size_t maxSize,
    int acquireTimeoutSec)
    : connString_(connString)
    , minSize_(minSize)
    , maxSize_(maxSize)
    , acquireTimeout_(acquireTimeoutSec)
    , totalConnections_(0)
    , shutdown_(false)
{
    if (minSize > maxSize) {
        throw std::invalid_argument("minSize cannot exceed maxSize");
    }

    spdlog::info("DbConnectionPool created: minSize={}, maxSize={}, timeout={}s",
                 minSize_, maxSize_, acquireTimeoutSec);
}

DbConnectionPool::~DbConnectionPool() {
    shutdown();
}

bool DbConnectionPool::initialize() {
    spdlog::info("Initializing DbConnectionPool with {} minimum connections", minSize_);

    std::lock_guard<std::mutex> lock(mutex_);

    // Create minimum connections
    for (size_t i = 0; i < minSize_; i++) {
        PGconn* conn = createConnection();
        if (!conn) {
            spdlog::error("Failed to create minimum connection {}/{}", i + 1, minSize_);
            return false;
        }

        availableConnections_.push(conn);
        totalConnections_++;
    }

    spdlog::info("DbConnectionPool initialized with {} connections", totalConnections_.load());
    return true;
}

DbConnection DbConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    auto deadline = std::chrono::steady_clock::now() + acquireTimeout_;

    while (true) {
        // Check if pool is shutdown
        if (shutdown_) {
            throw std::runtime_error("Connection pool is shutdown");
        }

        // Check if connection is available
        if (!availableConnections_.empty()) {
            PGconn* conn = availableConnections_.front();
            availableConnections_.pop();

            // Verify connection health
            if (isConnectionHealthy(conn)) {
                spdlog::debug("Acquired connection from pool (available: {})", availableConnections_.size());
                return DbConnection(conn, this);
            } else {
                spdlog::warn("Connection from pool is unhealthy, closing and retrying");
                PQfinish(conn);
                totalConnections_--;
                continue;  // Try to get another connection
            }
        }

        // No available connections - try to create new one if under max
        if (totalConnections_ < maxSize_) {
            lock.unlock();
            PGconn* conn = createConnection();
            lock.lock();

            if (conn) {
                totalConnections_++;
                spdlog::info("Created new connection (total: {})", totalConnections_.load());
                return DbConnection(conn, this);
            } else {
                spdlog::error("Failed to create new connection");
                throw std::runtime_error("Failed to create database connection");
            }
        }

        // Wait for connection to become available or timeout
        if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            spdlog::warn("Timeout waiting for database connection (timeout: {}s)", acquireTimeout_.count());
            throw std::runtime_error("Timeout acquiring database connection");
        }
    }
}

std::unique_ptr<IDbConnection> DbConnectionPool::acquireGeneric() {
    DbConnection conn = acquire();
    return std::make_unique<DbConnection>(std::move(conn));
}

DbConnectionPool::Stats DbConnectionPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return Stats{
        availableConnections_.size(),
        totalConnections_.load(),
        maxSize_
    };
}

void DbConnectionPool::shutdown() {
    spdlog::info("Shutting down DbConnectionPool");

    std::lock_guard<std::mutex> lock(mutex_);

    if (shutdown_) {
        return;
    }

    shutdown_ = true;

    // Close all available connections
    while (!availableConnections_.empty()) {
        PGconn* conn = availableConnections_.front();
        availableConnections_.pop();
        PQfinish(conn);
    }

    totalConnections_ = 0;

    // Notify all waiting threads
    cv_.notify_all();

    spdlog::info("DbConnectionPool shutdown complete");
}

PGconn* DbConnectionPool::createConnection() {
    spdlog::debug("Creating new PostgreSQL connection");

    PGconn* conn = PQconnectdb(connString_.c_str());

    if (PQstatus(conn) != CONNECTION_OK) {
        std::string error = PQerrorMessage(conn);
        spdlog::error("Failed to create PostgreSQL connection: {}", error);
        PQfinish(conn);
        return nullptr;
    }

    spdlog::debug("PostgreSQL connection created successfully");
    return conn;
}

bool DbConnectionPool::isConnectionHealthy(PGconn* conn) {
    if (!conn) {
        return false;
    }

    // Check connection status
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::debug("Connection status is not OK");
        return false;
    }

    // Send a simple ping query
    PGresult* res = PQexec(conn, "SELECT 1");
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) {
            PQclear(res);
        }
        spdlog::debug("Connection health check query failed");
        return false;
    }

    PQclear(res);
    return true;
}

void DbConnectionPool::releaseConnection(PGconn* conn) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (shutdown_) {
        // Pool is shutdown, close connection immediately
        PQfinish(conn);
        totalConnections_--;
        return;
    }

    // Verify connection is still healthy before returning to pool
    if (isConnectionHealthy(conn)) {
        availableConnections_.push(conn);
        spdlog::debug("Connection returned to pool (available: {})", availableConnections_.size());
    } else {
        // Connection is unhealthy, close it
        spdlog::warn("Released connection is unhealthy, closing");
        PQfinish(conn);
        totalConnections_--;
    }

    // Notify one waiting thread
    cv_.notify_one();
}

} // namespace common

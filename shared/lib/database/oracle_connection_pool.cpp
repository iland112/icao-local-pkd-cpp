/**
 * @file oracle_connection_pool.cpp
 * @brief Oracle Connection Pool Implementation using OTL
 */

#include "oracle_connection_pool.h"
#include <stdexcept>
#include <iostream>
#include <sstream>

// OTL configuration - must be defined before including otlv4.h
#define OTL_ORA11G_R2   // Oracle 11g Release 2 and higher
#define OTL_ANSI_CPP    // Enable ANSI C++ compliance
#define OTL_STL         // Enable STL string support
#define OTL_BIGINT long long  // 64-bit integer support

// Include OTL header (will be downloaded separately)
#include <otlv4.h>

namespace common {

// --- OracleConnection Implementation ---

OracleConnection::OracleConnection(void* conn, OracleConnectionPool* pool)
    : conn_(conn), pool_(pool), released_(false) {}

OracleConnection::~OracleConnection() {
    if (!released_ && conn_) {
        release();
    }
}

OracleConnection::OracleConnection(OracleConnection&& other) noexcept
    : conn_(other.conn_), pool_(other.pool_), released_(other.released_) {
    other.conn_ = nullptr;
    other.released_ = true;
}

OracleConnection& OracleConnection::operator=(OracleConnection&& other) noexcept {
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

bool OracleConnection::isValid() const {
    if (conn_ == nullptr || released_) {
        return false;
    }

    try {
        otl_connect* oc = static_cast<otl_connect*>(conn_);
        // Check if connection is active
        return oc->connected != 0;
    } catch (...) {
        return false;
    }
}

bool OracleConnection::execute(const std::string& sql) {
    if (!isValid()) {
        return false;
    }

    try {
        otl_connect* oc = static_cast<otl_connect*>(conn_);
        otl_cursor::direct_exec(*oc, sql.c_str());
        return true;
    } catch (otl_exception& e) {
        std::cerr << "Oracle execute error: " << e.msg << std::endl;
        return false;
    } catch (...) {
        return false;
    }
}

void OracleConnection::release() {
    if (!released_ && conn_ && pool_) {
        pool_->releaseConnection(conn_);
        released_ = true;
        conn_ = nullptr;
    }
}

// --- OracleConnectionPool Implementation ---

OracleConnectionPool::OracleConnectionPool(
    const std::string& connString,
    size_t minSize,
    size_t maxSize,
    int acquireTimeoutSec
) : connString_(connString),
    minSize_(minSize),
    maxSize_(maxSize),
    acquireTimeout_(acquireTimeoutSec),
    totalConnections_(0),
    shutdown_(false),
    otlInitialized_(false) {

    std::cout << "[info] OracleConnectionPool created: minSize=" << minSize_
              << ", maxSize=" << maxSize_
              << ", timeout=" << acquireTimeoutSec << "s" << std::endl;
}

OracleConnectionPool::~OracleConnectionPool() {
    shutdown();
}

void OracleConnectionPool::initializeOTL() {
    if (otlInitialized_) {
        return;
    }

    try {
        // Initialize OTL environment
        otl_connect::otl_initialize();
        otlInitialized_ = true;
        std::cout << "[info] OTL initialized successfully" << std::endl;
    } catch (otl_exception& e) {
        std::cerr << "[error] OTL initialization failed: " << e.msg << std::endl;
        throw std::runtime_error(std::string("OTL initialization failed: ") + reinterpret_cast<const char*>(e.msg));
    }
}

bool OracleConnectionPool::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (shutdown_) {
        std::cerr << "[error] Cannot initialize: pool is shut down" << std::endl;
        return false;
    }

    // Initialize OTL library
    initializeOTL();

    // Create minimum number of connections
    for (size_t i = 0; i < minSize_; ++i) {
        try {
            void* conn = createConnection();
            if (conn) {
                availableConnections_.push(conn);
                totalConnections_++;
                std::cout << "[info] Created Oracle connection " << (i + 1) << "/" << minSize_ << std::endl;
            } else {
                std::cerr << "[error] Failed to create connection " << (i + 1) << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "[error] Connection creation failed: " << e.what() << std::endl;
            return false;
        }
    }

    std::cout << "[info] ✅ Oracle connection pool initialized (min=" << minSize_
              << ", max=" << maxSize_ << ")" << std::endl;
    return true;
}

void* OracleConnectionPool::createConnection() {
    try {
        otl_connect* conn = new otl_connect();

        // Connect to Oracle
        // Connection string format: "user/password@host:port/service_name"
        conn->rlogon(connString_.c_str());

        std::cout << "[info] Created new Oracle connection (total=" << (totalConnections_ + 1) << ")" << std::endl;
        return conn;
    } catch (otl_exception& e) {
        std::cerr << "[error] Failed to create Oracle connection: " << e.msg << std::endl;
        std::cerr << "[error] Connection string: " << connString_ << std::endl;
        throw std::runtime_error(std::string("Oracle connection failed: ") + reinterpret_cast<const char*>(e.msg));
    }
}

bool OracleConnectionPool::isConnectionHealthy(void* conn) {
    if (!conn) {
        return false;
    }

    try {
        otl_connect* oc = static_cast<otl_connect*>(conn);

        // Check if connection is still connected
        if (oc->connected == 0) {
            return false;
        }

        // Simple health check query
        otl_stream stream(1, "SELECT 1 FROM DUAL", *oc);
        int dummy;
        if (!stream.eof()) {
            stream >> dummy;
        }

        return true;
    } catch (otl_exception& e) {
        std::cerr << "[warning] Connection health check failed: " << e.msg << std::endl;
        return false;
    } catch (...) {
        return false;
    }
}

OracleConnection OracleConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    if (shutdown_) {
        throw std::runtime_error("Cannot acquire connection: pool is shut down");
    }

    auto deadline = std::chrono::steady_clock::now() + acquireTimeout_;

    while (true) {
        // Try to get connection from available pool
        if (!availableConnections_.empty()) {
            void* conn = availableConnections_.front();
            availableConnections_.pop();

            // Health check
            if (isConnectionHealthy(conn)) {
                return OracleConnection(conn, this);
            } else {
                // Connection is unhealthy, close it and try again
                std::cerr << "[warning] Removing unhealthy Oracle connection" << std::endl;
                otl_connect* oc = static_cast<otl_connect*>(conn);
                oc->logoff();
                delete oc;
                totalConnections_--;
                continue;
            }
        }

        // No available connections, try to create new one
        if (totalConnections_ < maxSize_) {
            try {
                void* conn = createConnection();
                totalConnections_++;
                return OracleConnection(conn, this);
            } catch (const std::exception& e) {
                std::cerr << "[error] Failed to create new connection: " << e.what() << std::endl;
                // Fall through to wait
            }
        }

        // Wait for connection to become available
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            std::cerr << "[error] Connection acquisition timeout after "
                      << acquireTimeout_.count() << " seconds" << std::endl;
            throw std::runtime_error("Connection pool timeout");
        }

        cv_.wait_until(lock, deadline);

        if (shutdown_) {
            throw std::runtime_error("Pool shut down while waiting for connection");
        }
    }
}

std::unique_ptr<IDbConnection> OracleConnectionPool::acquireGeneric() {
    OracleConnection conn = acquire();
    return std::make_unique<OracleConnection>(std::move(conn));
}

void OracleConnectionPool::releaseConnection(void* conn) {
    if (!conn) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (shutdown_) {
        // Pool is shutting down, close connection
        otl_connect* oc = static_cast<otl_connect*>(conn);
        oc->logoff();
        delete oc;
        totalConnections_--;
        return;
    }

    // Health check before returning to pool
    if (isConnectionHealthy(conn)) {
        availableConnections_.push(conn);
        cv_.notify_one();
    } else {
        // Connection is unhealthy, close it
        std::cerr << "[warning] Closing unhealthy connection on release" << std::endl;
        otl_connect* oc = static_cast<otl_connect*>(conn);
        oc->logoff();
        delete oc;
        totalConnections_--;
    }
}

IDbConnectionPool::Stats OracleConnectionPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats;
    stats.availableConnections = availableConnections_.size();
    stats.totalConnections = totalConnections_;
    stats.maxConnections = maxSize_;

    return stats;
}

void OracleConnectionPool::shutdown() {
    std::unique_lock<std::mutex> lock(mutex_);

    if (shutdown_) {
        return;
    }

    std::cout << "[info] Shutting down Oracle connection pool..." << std::endl;
    shutdown_ = true;

    // Notify all waiting threads
    cv_.notify_all();

    // Close all available connections
    while (!availableConnections_.empty()) {
        void* conn = availableConnections_.front();
        availableConnections_.pop();

        try {
            otl_connect* oc = static_cast<otl_connect*>(conn);
            oc->logoff();
            delete oc;
            totalConnections_--;
        } catch (otl_exception& e) {
            std::cerr << "[error] Error closing connection: " << e.msg << std::endl;
        }
    }

    std::cout << "[info] Oracle connection pool shut down complete (total=" << totalConnections_ << ")" << std::endl;
}

} // namespace common

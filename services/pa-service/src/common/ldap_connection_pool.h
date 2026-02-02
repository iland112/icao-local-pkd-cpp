/**
 * @file ldap_connection_pool.h
 * @brief LDAP Connection Pool Manager
 *
 * Thread-safe connection pooling for LDAP server
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

#include <ldap.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>
#include <atomic>

namespace common {

/**
 * @brief RAII wrapper for LDAP connection
 *
 * Automatically returns connection to pool when destroyed
 */
class LdapConnection {
private:
    LDAP* ldap_;
    class LdapConnectionPool* pool_;  // Non-owning pointer to pool
    bool released_;

public:
    LdapConnection(LDAP* ldap, LdapConnectionPool* pool)
        : ldap_(ldap), pool_(pool), released_(false) {}

    ~LdapConnection();

    // Delete copy constructor and assignment
    LdapConnection(const LdapConnection&) = delete;
    LdapConnection& operator=(const LdapConnection&) = delete;

    // Move constructor and assignment
    LdapConnection(LdapConnection&& other) noexcept
        : ldap_(other.ldap_), pool_(other.pool_), released_(other.released_) {
        other.ldap_ = nullptr;
        other.released_ = true;
    }

    LdapConnection& operator=(LdapConnection&& other) noexcept {
        if (this != &other) {
            if (!released_ && ldap_) {
                release();
            }
            ldap_ = other.ldap_;
            pool_ = other.pool_;
            released_ = other.released_;
            other.ldap_ = nullptr;
            other.released_ = true;
        }
        return *this;
    }

    /**
     * @brief Get raw LDAP connection
     */
    LDAP* get() const { return ldap_; }

    /**
     * @brief Check if connection is valid
     */
    bool isValid() const {
        return ldap_ != nullptr && !released_;
    }

    /**
     * @brief Manually release connection back to pool
     */
    void release();
};

/**
 * @brief LDAP Connection Pool
 *
 * Thread-safe connection pool with configurable size and timeout
 */
class LdapConnectionPool {
private:
    std::string ldapUrl_;
    std::string bindDn_;
    std::string bindPassword_;
    size_t minSize_;
    size_t maxSize_;
    std::chrono::seconds acquireTimeout_;

    std::queue<LDAP*> availableConnections_;
    std::atomic<size_t> totalConnections_;
    mutable std::mutex mutex_;  // mutable to allow locking in const methods
    std::condition_variable cv_;

    bool shutdown_;

    friend class LdapConnection;

public:
    /**
     * @brief Constructor
     * @param ldapUrl LDAP server URL (e.g., "ldap://localhost:389")
     * @param bindDn Bind DN for authentication
     * @param bindPassword Bind password
     * @param minSize Minimum number of connections to maintain
     * @param maxSize Maximum number of connections allowed
     * @param acquireTimeoutSec Timeout for acquiring connection (seconds)
     */
    explicit LdapConnectionPool(
        const std::string& ldapUrl,
        const std::string& bindDn,
        const std::string& bindPassword,
        size_t minSize = 2,
        size_t maxSize = 10,
        int acquireTimeoutSec = 5
    );

    /**
     * @brief Destructor - closes all connections
     */
    ~LdapConnectionPool();

    // Delete copy constructor and assignment
    LdapConnectionPool(const LdapConnectionPool&) = delete;
    LdapConnectionPool& operator=(const LdapConnectionPool&) = delete;

    /**
     * @brief Initialize connection pool
     * @return true if successfully created minimum connections
     */
    bool initialize();

    /**
     * @brief Acquire connection from pool
     * @return LdapConnection RAII wrapper (nullptr on timeout)
     * @throws std::runtime_error on pool shutdown or critical error
     */
    LdapConnection acquire();

    /**
     * @brief Get pool statistics
     */
    struct Stats {
        size_t availableConnections;
        size_t totalConnections;
        size_t maxConnections;
    };

    Stats getStats() const;

    /**
     * @brief Shutdown pool and close all connections
     */
    void shutdown();

private:
    /**
     * @brief Create new LDAP connection
     */
    LDAP* createConnection();

    /**
     * @brief Check if connection is healthy
     */
    bool isConnectionHealthy(LDAP* ldap);

    /**
     * @brief Return connection to pool (called by LdapConnection)
     */
    void releaseConnection(LDAP* ldap);
};

} // namespace common

/**
 * @file ldap_connection_pool.h
 * @brief LDAP Connection Pool Manager
 *
 * Thread-safe connection pooling for LDAP servers
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
    LDAP* ld_;
    class LdapConnectionPool* pool_;  // Non-owning pointer to pool
    bool released_;

public:
    LdapConnection(LDAP* ld, LdapConnectionPool* pool)
        : ld_(ld), pool_(pool), released_(false) {}

    ~LdapConnection();

    // Delete copy constructor and assignment
    LdapConnection(const LdapConnection&) = delete;
    LdapConnection& operator=(const LdapConnection&) = delete;

    // Move constructor and assignment
    LdapConnection(LdapConnection&& other) noexcept
        : ld_(other.ld_), pool_(other.pool_), released_(other.released_) {
        other.ld_ = nullptr;
        other.released_ = true;
    }

    LdapConnection& operator=(LdapConnection&& other) noexcept {
        if (this != &other) {
            if (!released_ && ld_) {
                release();
            }
            ld_ = other.ld_;
            pool_ = other.pool_;
            released_ = other.released_;
            other.ld_ = nullptr;
            other.released_ = true;
        }
        return *this;
    }

    /**
     * @brief Get raw LDAP connection
     */
    LDAP* get() const { return ld_; }

    /**
     * @brief Check if connection is valid
     */
    bool isValid() const {
        return ld_ != nullptr && !released_;
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
    std::string ldapUri_;
    std::string bindDn_;
    std::string bindPassword_;
    size_t minSize_;
    size_t maxSize_;
    std::chrono::seconds acquireTimeout_;
    int networkTimeout_;
    int healthCheckTimeout_;

    std::queue<LDAP*> availableConnections_;
    std::atomic<size_t> totalConnections_;
    mutable std::mutex mutex_;  // mutable to allow locking in const methods
    std::condition_variable cv_;

    bool shutdown_;

    friend class LdapConnection;

public:
    /**
     * @brief Constructor
     * @param ldapUri LDAP server URI (e.g., "ldap://openldap1:389")
     * @param bindDn Bind DN for authentication
     * @param bindPassword Bind password
     * @param minSize Minimum number of connections to maintain
     * @param maxSize Maximum number of connections allowed
     * @param acquireTimeoutSec Timeout for acquiring connection (seconds)
     * @param networkTimeoutSec LDAP network operation timeout (seconds)
     * @param healthCheckTimeoutSec Health check query timeout (seconds)
     */
    explicit LdapConnectionPool(
        const std::string& ldapUri,
        const std::string& bindDn,
        const std::string& bindPassword,
        size_t minSize = 2,
        size_t maxSize = 10,
        int acquireTimeoutSec = 5,
        int networkTimeoutSec = 5,
        int healthCheckTimeoutSec = 2
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
    bool isConnectionHealthy(LDAP* ld);

    /**
     * @brief Return connection to pool (called by LdapConnection)
     */
    void releaseConnection(LDAP* ld);
};

} // namespace common

/**
 * @file ldap_connection_pool.cpp
 * @brief Implementation of LDAP Connection Pool
 */

#include "ldap_connection_pool.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace common {

// =============================================================================
// LdapConnection Implementation
// =============================================================================

LdapConnection::~LdapConnection() {
    if (!released_ && ldap_) {
        release();
    }
}

void LdapConnection::release() {
    if (released_ || !ldap_) {
        return;
    }

    if (pool_) {
        pool_->releaseConnection(ldap_);
    }

    ldap_ = nullptr;
    released_ = true;
}

// =============================================================================
// LdapConnectionPool Implementation
// =============================================================================

LdapConnectionPool::LdapConnectionPool(
    const std::string& ldapUrl,
    const std::string& bindDn,
    const std::string& bindPassword,
    size_t minSize,
    size_t maxSize,
    int acquireTimeoutSec)
    : ldapUrl_(ldapUrl)
    , bindDn_(bindDn)
    , bindPassword_(bindPassword)
    , minSize_(minSize)
    , maxSize_(maxSize)
    , acquireTimeout_(acquireTimeoutSec)
    , totalConnections_(0)
    , shutdown_(false)
{
    if (minSize > maxSize) {
        throw std::invalid_argument("minSize cannot exceed maxSize");
    }

    spdlog::info("LdapConnectionPool created: url={}, minSize={}, maxSize={}, timeout={}s",
                 ldapUrl_, minSize_, maxSize_, acquireTimeoutSec);
}

LdapConnectionPool::~LdapConnectionPool() {
    shutdown();
}

bool LdapConnectionPool::initialize() {
    spdlog::info("Initializing LdapConnectionPool with {} minimum connections", minSize_);

    std::lock_guard<std::mutex> lock(mutex_);

    // Create minimum connections
    for (size_t i = 0; i < minSize_; i++) {
        LDAP* ldap = createConnection();
        if (!ldap) {
            spdlog::error("Failed to create minimum LDAP connection {}/{}", i + 1, minSize_);
            return false;
        }

        availableConnections_.push(ldap);
        totalConnections_++;
    }

    spdlog::info("LdapConnectionPool initialized with {} connections", totalConnections_.load());
    return true;
}

LdapConnection LdapConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    auto deadline = std::chrono::steady_clock::now() + acquireTimeout_;

    while (true) {
        // Check if pool is shutdown
        if (shutdown_) {
            throw std::runtime_error("LDAP connection pool is shutdown");
        }

        // Check if connection is available
        if (!availableConnections_.empty()) {
            LDAP* ldap = availableConnections_.front();
            availableConnections_.pop();

            // Verify connection health
            if (isConnectionHealthy(ldap)) {
                spdlog::debug("Acquired LDAP connection from pool (available: {})", availableConnections_.size());
                return LdapConnection(ldap, this);
            } else {
                spdlog::warn("LDAP connection from pool is unhealthy, closing and retrying");
                ldap_unbind_ext_s(ldap, nullptr, nullptr);
                totalConnections_--;
                continue;  // Try to get another connection
            }
        }

        // No available connections - try to create new one if under max
        if (totalConnections_ < maxSize_) {
            lock.unlock();
            LDAP* ldap = createConnection();
            lock.lock();

            if (ldap) {
                totalConnections_++;
                spdlog::info("Created new LDAP connection (total: {})", totalConnections_.load());
                return LdapConnection(ldap, this);
            } else {
                spdlog::error("Failed to create new LDAP connection");
                throw std::runtime_error("Failed to create LDAP connection");
            }
        }

        // Wait for connection to become available or timeout
        if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            spdlog::warn("Timeout waiting for LDAP connection (timeout: {}s)", acquireTimeout_.count());
            throw std::runtime_error("Timeout acquiring LDAP connection");
        }
    }
}

LdapConnectionPool::Stats LdapConnectionPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return Stats{
        availableConnections_.size(),
        totalConnections_.load(),
        maxSize_
    };
}

void LdapConnectionPool::shutdown() {
    spdlog::info("Shutting down LdapConnectionPool");

    std::lock_guard<std::mutex> lock(mutex_);

    if (shutdown_) {
        return;
    }

    shutdown_ = true;

    // Close all available connections
    while (!availableConnections_.empty()) {
        LDAP* ldap = availableConnections_.front();
        availableConnections_.pop();
        ldap_unbind_ext_s(ldap, nullptr, nullptr);
    }

    totalConnections_ = 0;

    // Notify all waiting threads
    cv_.notify_all();

    spdlog::info("LdapConnectionPool shutdown complete");
}

LDAP* LdapConnectionPool::createConnection() {
    spdlog::debug("Creating new LDAP connection to {}", ldapUrl_);

    LDAP* ldap = nullptr;

    // Initialize LDAP connection
    int rc = ldap_initialize(&ldap, ldapUrl_.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("Failed to initialize LDAP: {}", ldap_err2string(rc));
        return nullptr;
    }

    // Set LDAP version to 3
    int version = LDAP_VERSION3;
    ldap_set_option(ldap, LDAP_OPT_PROTOCOL_VERSION, &version);

    // Set network timeout (5 seconds)
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    ldap_set_option(ldap, LDAP_OPT_NETWORK_TIMEOUT, &timeout);

    // Bind to LDAP server
    berval cred;
    cred.bv_val = const_cast<char*>(bindPassword_.c_str());
    cred.bv_len = bindPassword_.length();

    berval* servercredp = nullptr;
    rc = ldap_sasl_bind_s(
        ldap,
        bindDn_.c_str(),
        LDAP_SASL_SIMPLE,
        &cred,
        nullptr,
        nullptr,
        &servercredp
    );

    if (servercredp) {
        ber_bvfree(servercredp);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::error("Failed to bind to LDAP server: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ldap, nullptr, nullptr);
        return nullptr;
    }

    spdlog::debug("LDAP connection created and bound successfully");
    return ldap;
}

bool LdapConnectionPool::isConnectionHealthy(LDAP* ldap) {
    if (!ldap) {
        return false;
    }

    // Send a simple search query to check health (base DN only, no results expected)
    LDAPMessage* res = nullptr;
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    int rc = ldap_search_ext_s(
        ldap,
        "",  // Empty base DN
        LDAP_SCOPE_BASE,
        "(objectClass=*)",
        nullptr,
        0,
        nullptr,
        nullptr,
        &timeout,
        1,
        &res
    );

    if (res) {
        ldap_msgfree(res);
    }

    if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT) {
        spdlog::debug("LDAP connection health check failed: {}", ldap_err2string(rc));
        return false;
    }

    return true;
}

void LdapConnectionPool::releaseConnection(LDAP* ldap) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (shutdown_) {
        // Pool is shutdown, close connection immediately
        ldap_unbind_ext_s(ldap, nullptr, nullptr);
        totalConnections_--;
        return;
    }

    // Verify connection is still healthy before returning to pool
    if (isConnectionHealthy(ldap)) {
        availableConnections_.push(ldap);
        spdlog::debug("LDAP connection returned to pool (available: {})", availableConnections_.size());
    } else {
        // Connection is unhealthy, close it
        spdlog::warn("Released LDAP connection is unhealthy, closing");
        ldap_unbind_ext_s(ldap, nullptr, nullptr);
        totalConnections_--;
    }

    // Notify one waiting thread
    cv_.notify_one();
}

} // namespace common

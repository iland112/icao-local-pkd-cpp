/**
 * @file ldap_connection_pool.cpp
 * @brief Implementation of LDAP Connection Pool
 */

#include "ldap_connection_pool.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

namespace common {

// --- LdapConnection Implementation ---

LdapConnection::~LdapConnection() {
    if (!released_ && ld_) {
        release();
    }
}

void LdapConnection::release() {
    if (!released_ && ld_ && pool_) {
        pool_->releaseConnection(ld_);
        ld_ = nullptr;
        released_ = true;
    }
}

// --- LdapConnectionPool Implementation ---

LdapConnectionPool::LdapConnectionPool(
    const std::string& ldapUri,
    const std::string& bindDn,
    const std::string& bindPassword,
    size_t minSize,
    size_t maxSize,
    int acquireTimeoutSec)
    : ldapUri_(ldapUri),
      bindDn_(bindDn),
      bindPassword_(bindPassword),
      minSize_(minSize),
      maxSize_(maxSize),
      acquireTimeout_(acquireTimeoutSec),
      totalConnections_(0),
      shutdown_(false)
{
    spdlog::info("LdapConnectionPool created: uri={}, minSize={}, maxSize={}, timeout={}s",
                 ldapUri_, minSize_, maxSize_, acquireTimeoutSec);
}

LdapConnectionPool::~LdapConnectionPool() {
    shutdown();
}

bool LdapConnectionPool::initialize() {
    spdlog::info("Initializing LDAP connection pool (min={}, max={})", minSize_, maxSize_);

    std::lock_guard<std::mutex> lock(mutex_);

    // Create minimum connections
    for (size_t i = 0; i < minSize_; ++i) {
        LDAP* ld = createConnection();
        if (ld) {
            availableConnections_.push(ld);
            totalConnections_++;
        } else {
            spdlog::error("Failed to create initial LDAP connection {}/{}", i + 1, minSize_);
            return false;
        }
    }

    spdlog::info("LDAP connection pool initialized with {} connections", totalConnections_.load());
    return true;
}

LdapConnection LdapConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    auto deadline = std::chrono::steady_clock::now() + acquireTimeout_;

    while (true) {
        if (shutdown_) {
            throw std::runtime_error("LDAP pool is shutting down");
        }

        // Try to get connection from pool
        if (!availableConnections_.empty()) {
            LDAP* ld = availableConnections_.front();
            availableConnections_.pop();

            // Check if connection is still healthy
            if (isConnectionHealthy(ld)) {
                spdlog::debug("Acquired LDAP connection from pool (available={}, total={})",
                             availableConnections_.size(), totalConnections_.load());
                return LdapConnection(ld, this);
            } else {
                spdlog::warn("Unhealthy LDAP connection detected, creating new one");
                ldap_unbind_ext_s(ld, nullptr, nullptr);
                totalConnections_--;
            }
        }

        // Try to create new connection if under max limit
        if (totalConnections_ < maxSize_) {
            lock.unlock();
            LDAP* ld = createConnection();
            lock.lock();

            if (ld) {
                totalConnections_++;
                spdlog::info("Created new LDAP connection (total={})", totalConnections_.load());
                return LdapConnection(ld, this);
            } else {
                spdlog::error("Failed to create new LDAP connection");
            }
        }

        // Wait for connection to become available
        spdlog::debug("Waiting for LDAP connection (available={}, total={}, max={})",
                     availableConnections_.size(), totalConnections_.load(), maxSize_);

        if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            spdlog::error("Timeout waiting for LDAP connection");
            return LdapConnection(nullptr, this);
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
    spdlog::info("Shutting down LDAP connection pool");

    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;

    // Close all available connections
    while (!availableConnections_.empty()) {
        LDAP* ld = availableConnections_.front();
        availableConnections_.pop();
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        totalConnections_--;
    }

    cv_.notify_all();
    spdlog::info("LDAP connection pool shutdown complete");
}

LDAP* LdapConnectionPool::createConnection() {
    LDAP* ld = nullptr;
    int rc;

    // Initialize LDAP connection
    rc = ldap_initialize(&ld, ldapUri_.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("ldap_initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    // Set LDAP protocol version to 3
    int version = LDAP_VERSION3;
    rc = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("ldap_set_option PROTOCOL_VERSION failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    // Set network timeout (5 seconds)
    struct timeval timeout = {5, 0};
    rc = ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &timeout);
    if (rc != LDAP_SUCCESS) {
        spdlog::warn("ldap_set_option NETWORK_TIMEOUT failed: {}", ldap_err2string(rc));
    }

    // Perform simple bind with proper berval initialization
    // Use ber_str2bv() to safely create berval structure
    // Parameter 3 = 1 means duplicate the string (important for thread safety)
    struct berval* cred = ber_str2bv(bindPassword_.c_str(), 0, 1, nullptr);
    if (!cred) {
        spdlog::error("ber_str2bv failed to create berval");
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    rc = ldap_sasl_bind_s(ld, bindDn_.c_str(), LDAP_SASL_SIMPLE, cred,
                          nullptr, nullptr, nullptr);

    // Free the berval structure
    ber_bvfree(cred);

    if (rc != LDAP_SUCCESS) {
        spdlog::error("ldap_sasl_bind_s failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    spdlog::debug("Created new LDAP connection to {}", ldapUri_);
    return ld;
}

bool LdapConnectionPool::isConnectionHealthy(LDAP* ld) {
    if (!ld) return false;

    // Perform a simple search to check if connection is alive
    // Search for root DSE (empty base DN, scope base, filter objectClass=*)
    LDAPMessage* result = nullptr;
    struct timeval timeout = {2, 0};  // 2 second timeout

    int rc = ldap_search_ext_s(ld, "", LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, &timeout, 1, &result);

    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_SUCCESS) {
        return true;
    } else {
        spdlog::warn("LDAP connection health check failed: {}", ldap_err2string(rc));
        return false;
    }
}

void LdapConnectionPool::releaseConnection(LDAP* ld) {
    if (!ld) return;

    std::lock_guard<std::mutex> lock(mutex_);

    if (shutdown_) {
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        totalConnections_--;
        return;
    }

    // Return connection to pool
    availableConnections_.push(ld);
    spdlog::debug("Released LDAP connection to pool (available={}, total={})",
                 availableConnections_.size(), totalConnections_.load());
    cv_.notify_one();
}

} // namespace common

#pragma once

/**
 * @file service_container.h
 * @brief Centralized service container for PKD Relay Service dependency management
 *
 * Owns all connection pools, repositories, and services.
 * Replaces global shared_ptr variables with a single container instance.
 * Provides non-owning pointer accessors for dependency injection.
 *
 * @date 2026-02-17
 */

#include <memory>

namespace icao { namespace relay { struct Config; } }

// Forward declarations - Infrastructure
namespace common {
    class IDbConnectionPool;
    class IQueryExecutor;
    class LdapConnectionPool;
}

// Forward declarations - Repositories
namespace icao::relay::repositories {
    class SyncStatusRepository;
    class CertificateRepository;
    class CrlRepository;
    class ReconciliationRepository;
    class ValidationRepository;
}

// Forward declarations - Services
namespace icao::relay::services {
    class SyncService;
    class ReconciliationService;
    class ValidationService;
}

namespace infrastructure {

/**
 * @brief Centralized service container managing all PKD Relay Service dependencies
 *
 * Initialization order:
 * 1. Database connection pool (Factory Pattern based on DB_TYPE)
 * 2. Query Executor (PostgreSQL or Oracle)
 * 3. LDAP connection pool (write host for reconciliation)
 * 4. Repositories (all depend on QueryExecutor)
 * 5. Services (depend on repositories)
 */
class ServiceContainer {
public:
    ServiceContainer();
    ~ServiceContainer();

    // Non-copyable, non-movable
    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;

    /**
     * @brief Initialize all components in dependency order
     * @param config PKD Relay Service configuration
     * @return true on success, false on failure (details logged)
     */
    bool initialize(icao::relay::Config& config);

    /**
     * @brief Release all resources (called automatically by destructor)
     */
    void shutdown();

    // --- Connection Pool Accessors ---
    common::IDbConnectionPool* dbPool() const;
    common::IQueryExecutor* queryExecutor() const;
    common::LdapConnectionPool* ldapPool() const;

    // --- Repository Accessors ---
    icao::relay::repositories::SyncStatusRepository* syncStatusRepository() const;
    icao::relay::repositories::CertificateRepository* certificateRepository() const;
    icao::relay::repositories::CrlRepository* crlRepository() const;
    icao::relay::repositories::ReconciliationRepository* reconciliationRepository() const;
    icao::relay::repositories::ValidationRepository* validationRepository() const;

    // --- Service Accessors ---
    icao::relay::services::SyncService* syncService() const;
    icao::relay::services::ReconciliationService* reconciliationService() const;
    icao::relay::services::ValidationService* validationService() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infrastructure

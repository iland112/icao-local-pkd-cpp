#pragma once

/**
 * @file service_container.h
 * @brief Centralized service container for dependency management
 *
 * Owns all connection pools, repositories, services, and handlers.
 * Replaces global shared_ptr variables with a single container instance.
 * Provides non-owning pointer accessors for dependency injection.
 *
 * @date 2026-02-17
 */

#include <memory>

struct AppConfig;

// Forward declarations - Infrastructure
namespace common {
    class IDbConnectionPool;
    class IQueryExecutor;
    class LdapConnectionPool;
}

// Forward declarations - Repositories
namespace repositories {
    class UploadRepository;
    class CertificateRepository;
    class ValidationRepository;
    class AuditRepository;
    class LdifStructureRepository;
    class UserRepository;
    class AuthAuditRepository;
    class CrlRepository;
    class DeviationListRepository;
    class IcaoVersionRepository;
    class LdapCertificateRepository;
}

// Forward declarations - Services
namespace services {
    class UploadService;
    class ValidationService;
    class AuditService;
    class LdifStructureService;
    class CertificateService;
    class IcaoSyncService;
}

// Forward declarations - Handlers
namespace handlers {
    class IcaoHandler;
    class AuthHandler;
    class UploadHandler;
    class UploadStatsHandler;
    class CertificateHandler;
}

namespace infrastructure {

/**
 * @brief Centralized service container managing all application dependencies
 *
 * Initialization order:
 * 1. LDAP connection pool
 * 2. Certificate service (LDAP-based search)
 * 3. Database connection pool + Query Executor
 * 4. Repositories (all depend on QueryExecutor)
 * 5. ICAO sync module
 * 6. Business logic services
 * 7. Handlers
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
     * @param config Application configuration
     * @return true on success, false on failure (details logged)
     */
    bool initialize(const AppConfig& config);

    /**
     * @brief Release all resources (called automatically by destructor)
     */
    void shutdown();

    // --- Connection Pool Accessors ---
    common::IQueryExecutor* queryExecutor() const;
    common::LdapConnectionPool* ldapPool() const;
    common::IDbConnectionPool* dbPool() const;

    // --- Repository Accessors ---
    repositories::UploadRepository* uploadRepository() const;
    repositories::CertificateRepository* certificateRepository() const;
    repositories::ValidationRepository* validationRepository() const;
    repositories::AuditRepository* auditRepository() const;
    repositories::LdifStructureRepository* ldifStructureRepository() const;
    repositories::UserRepository* userRepository() const;
    repositories::AuthAuditRepository* authAuditRepository() const;
    repositories::CrlRepository* crlRepository() const;
    repositories::DeviationListRepository* deviationListRepository() const;

    // --- Service Accessors ---
    services::UploadService* uploadService() const;
    services::ValidationService* validationService() const;
    services::AuditService* auditService() const;
    services::LdifStructureService* ldifStructureService() const;
    services::CertificateService* certificateService() const;
    services::IcaoSyncService* icaoSyncService() const;

    // --- Handler Accessors ---
    handlers::IcaoHandler* icaoHandler() const;
    handlers::AuthHandler* authHandler() const;
    handlers::UploadHandler* uploadHandler() const;
    handlers::UploadStatsHandler* uploadStatsHandler() const;
    handlers::CertificateHandler* certificateHandler() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infrastructure

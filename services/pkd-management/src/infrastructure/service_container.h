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
namespace icao { namespace relay { struct Config; } }

// Forward declarations - Infrastructure
namespace common {
    class IDbConnectionPool;
    class IQueryExecutor;
    class LdapConnectionPool;
}

// Forward declarations - Sync module (moved from pkd-relay)
namespace icao::relay::repositories {
    class SyncStatusRepository;
    class ReconciliationRepository;
}
namespace icao::relay::services {
    class SyncService;
    class ReconciliationService;
    class ValidationService;  // Revalidation pipeline
}
namespace icao::relay::notification { class NotificationManager; }

namespace infrastructure { class SyncScheduler; }

// Forward declarations - Repositories
namespace repositories {
    class UploadRepository;
    class CertificateRepository;
    class ValidationRepository;
    class AuditRepository;
    class UserRepository;
    class AuthAuditRepository;
    class CrlRepository;
    class LdapCertificateRepository;
    class CodeMasterRepository;
    class ApiClientRepository;
    class ApiClientRequestRepository;
    class PendingDscRepository;
    class CsrRepository;
}

// Forward declarations - Services
namespace services {
    class UploadService;
    class ValidationService;
    class AuditService;
    class CertificateService;
    class LdapStorageService;
    class CsrService;
}

// Forward declarations - Handlers
namespace handlers {
    class AuthHandler;
    class UploadHandler;
    class CertificateHandler;
    class CodeMasterHandler;
    class ApiClientHandler;
    class ApiClientRequestHandler;
    class CsrHandler;
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

private:
    /**
     * @brief Ensure admin user exists on startup
     *
     * Creates admin user from ADMIN_INITIAL_PASSWORD environment variable
     * if no admin user exists in the database. Skips silently if env var not set
     * or admin already exists.
     */
    void ensureAdminUser();

public:
    // --- Connection Pool Accessors ---
    common::IQueryExecutor* queryExecutor() const;
    common::LdapConnectionPool* ldapPool() const;
    common::IDbConnectionPool* dbPool() const;

    // --- Repository Accessors ---
    repositories::UploadRepository* uploadRepository() const;
    repositories::CertificateRepository* certificateRepository() const;
    repositories::ValidationRepository* validationRepository() const;
    repositories::AuditRepository* auditRepository() const;
    repositories::UserRepository* userRepository() const;
    repositories::AuthAuditRepository* authAuditRepository() const;
    repositories::CrlRepository* crlRepository() const;
    repositories::CodeMasterRepository* codeMasterRepository() const;
    repositories::ApiClientRepository* apiClientRepository() const;
    repositories::ApiClientRequestRepository* apiClientRequestRepository() const;
    repositories::PendingDscRepository* pendingDscRepository() const;
    repositories::CsrRepository* csrRepository() const;

    // --- Service Accessors ---
    services::UploadService* uploadService() const;
    services::ValidationService* validationService() const;
    services::AuditService* auditService() const;
    services::CertificateService* certificateService() const;
    services::LdapStorageService* ldapStorageService() const;
    services::CsrService* csrService() const;

    // --- Handler Accessors ---
    handlers::AuthHandler* authHandler() const;
    handlers::UploadHandler* uploadHandler() const;
    handlers::CertificateHandler* certificateHandler() const;
    handlers::CodeMasterHandler* codeMasterHandler() const;
    handlers::ApiClientHandler* apiClientHandler() const;
    handlers::ApiClientRequestHandler* apiClientRequestHandler() const;
    handlers::CsrHandler* csrHandler() const;

    // --- Sync Module Accessors (moved from pkd-relay) ---
    icao::relay::repositories::SyncStatusRepository* syncStatusRepository() const;
    icao::relay::repositories::ReconciliationRepository* reconciliationRepository() const;
    icao::relay::services::SyncService* syncService() const;
    icao::relay::services::ReconciliationService* reconciliationService() const;
    icao::relay::services::ValidationService* syncValidationService() const;  // 3-step revalidation
    icao::relay::Config& syncConfig();
    infrastructure::SyncScheduler* syncScheduler() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infrastructure

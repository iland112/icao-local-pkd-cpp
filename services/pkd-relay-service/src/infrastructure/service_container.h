#pragma once

/**
 * @file service_container.h
 * @brief Centralized service container for PKD Relay Service dependency management
 *
 * Owns connection pools, shared repositories (ICAO LDAP sync), and ICAO LDAP sync service.
 * Sync/Reconciliation moved to pkd-management (v2.41.0).
 * Upload module has its own UploadServiceContainer.
 */

#include <memory>

namespace icao { namespace relay { struct Config; } }

namespace common {
    class IDbConnectionPool;
    class IQueryExecutor;
    class LdapConnectionPool;
}

// Shared repositories (used by ICAO LDAP sync + upload module)
namespace icao::relay::repositories {
    class CertificateRepository;
    class CrlRepository;
    class ValidationRepository;
}

namespace icao::relay::services {
    class ValidationService;
}

namespace icao::relay { class IcaoLdapSyncService; }

namespace infrastructure {

class ServiceContainer {
public:
    ServiceContainer();
    ~ServiceContainer();

    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;

    bool initialize(icao::relay::Config& config);
    void shutdown();

    // --- Connection Pool Accessors ---
    common::IDbConnectionPool* dbPool() const;
    common::IQueryExecutor* queryExecutor() const;
    common::LdapConnectionPool* ldapPool() const;

    // --- Shared Repository Accessors (ICAO LDAP sync + upload module) ---
    icao::relay::repositories::CertificateRepository* certificateRepository() const;
    icao::relay::repositories::CrlRepository* crlRepository() const;
    icao::relay::repositories::ValidationRepository* validationRepository() const;
    icao::relay::services::ValidationService* validationService() const;

    // --- ICAO LDAP Sync ---
    icao::relay::IcaoLdapSyncService* icaoLdapSyncService() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infrastructure

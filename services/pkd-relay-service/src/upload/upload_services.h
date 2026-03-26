/**
 * @file upload_services.h
 * @brief Upload module service locator
 *
 * The upload code (ported from pkd-management) references g_services->uploadRepository(),
 * g_services->certificateRepository(), etc. These use management's repository types
 * (different from relay's icao::relay::repositories::*).
 *
 * This class owns upload-specific repos/services and provides the same accessor
 * names that upload code expects.
 */
#pragma once

#include <memory>
#include <string>

// Forward declarations - shared infrastructure
namespace common {
    class IQueryExecutor;
    class LdapConnectionPool;
}

// Forward declarations - upload-module repositories (management types)
namespace repositories {
    class UploadRepository;
    class CertificateRepository;
    class CrlRepository;
    class ValidationRepository;
    class DeviationListRepository;
    class LdifStructureRepository;
    class IcaoVersionRepository;
}

// Forward declarations - upload-module services
namespace services {
    class UploadService;
    class ValidationService;
    class LdifStructureService;
    class LdapStorageService;
    class IcaoSyncService;
}

namespace handlers {
    class UploadHandler;
    class UploadStatsHandler;
    class IcaoHandler;
}

namespace infrastructure {

/**
 * @brief Upload module service container
 *
 * Owns upload-specific repos/services that use management's namespace types.
 * Initialized by relay's main() after the relay ServiceContainer is ready.
 * Provides the same accessor names that upload code expects via g_services.
 */
class UploadServiceContainer {
public:
    UploadServiceContainer();
    ~UploadServiceContainer();

    UploadServiceContainer(const UploadServiceContainer&) = delete;
    UploadServiceContainer& operator=(const UploadServiceContainer&) = delete;

    /**
     * @brief Initialize upload module components
     * @param queryExecutor Shared query executor from relay ServiceContainer
     * @param ldapPool Shared LDAP pool from relay ServiceContainer
     * @param ldapBaseDn LDAP base DN
     * @return true on success
     */
    bool initialize(common::IQueryExecutor* queryExecutor,
                    common::LdapConnectionPool* ldapPool,
                    const std::string& ldapBaseDn);

    void shutdown();

    // --- Connection Pool Accessors (delegated from relay SC) ---
    common::IQueryExecutor* queryExecutor() const;
    common::LdapConnectionPool* ldapPool() const;

    // --- Upload Repository Accessors ---
    repositories::UploadRepository* uploadRepository() const;
    repositories::CertificateRepository* certificateRepository() const;
    repositories::CrlRepository* crlRepository() const;
    repositories::ValidationRepository* validationRepository() const;
    repositories::DeviationListRepository* deviationListRepository() const;
    repositories::LdifStructureRepository* ldifStructureRepository() const;

    // --- Upload Service Accessors ---
    services::UploadService* uploadService() const;
    services::ValidationService* validationService() const;
    services::LdifStructureService* ldifStructureService() const;
    services::LdapStorageService* ldapStorageService() const;

    // --- Handler Accessors ---
    handlers::UploadHandler* uploadHandler() const;
    handlers::UploadStatsHandler* uploadStatsHandler() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infrastructure

// Upload module global service locator
extern infrastructure::UploadServiceContainer* g_uploadServices;

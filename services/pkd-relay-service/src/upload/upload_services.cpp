/**
 * @file upload_services.cpp
 * @brief UploadServiceContainer implementation
 */
#include "upload/upload_services.h"

#include "i_query_executor.h"
#include "db_connection_pool.h"
#include "db_connection_pool_factory.h"
#include <ldap_connection_pool.h>
#include <spdlog/spdlog.h>

// Upload-module repositories (management types)
#include "upload/repositories/upload_repository.h"
#include "upload/repositories/certificate_repository.h"
#include "upload/repositories/crl_repository.h"
#include "upload/repositories/validation_repository.h"
#include "upload/repositories/deviation_list_repository.h"
#include "upload/repositories/ldif_structure_repository.h"

// Upload-module services
#include "upload/services/upload_service.h"
#include "upload/services/validation_service.h"
#include "upload/services/ldif_structure_service.h"
#include "upload/services/ldap_storage_service.h"

// Global pointer (upload code references this)
infrastructure::UploadServiceContainer* g_uploadServices = nullptr;

namespace infrastructure {

struct UploadServiceContainer::Impl {
    // Own DB pool + queryExecutor (thread-safe, dedicated for upload module)
    std::shared_ptr<common::IDbConnectionPool> ownDbPool;
    std::unique_ptr<common::IQueryExecutor> ownQueryExecutor;

    // Active queryExecutor (own or shared from relay ServiceContainer)
    common::IQueryExecutor* queryExecutor = nullptr;
    common::LdapConnectionPool* ldapPool = nullptr;

    // Owned repositories
    std::shared_ptr<repositories::UploadRepository> uploadRepo;
    std::shared_ptr<repositories::CertificateRepository> certificateRepo;
    std::shared_ptr<repositories::CrlRepository> crlRepo;
    std::shared_ptr<repositories::ValidationRepository> validationRepo;
    std::shared_ptr<repositories::DeviationListRepository> deviationListRepo;
    std::shared_ptr<repositories::LdifStructureRepository> ldifStructureRepo;

    // Owned services
    std::shared_ptr<services::UploadService> uploadService;
    std::shared_ptr<services::ValidationService> validationService;
    std::shared_ptr<services::LdifStructureService> ldifStructureService;
    std::shared_ptr<services::LdapStorageService> ldapStorageService;

    // Handler references (created in main.cpp, just stored here)
    handlers::UploadHandler* uploadHandler = nullptr;
    handlers::UploadStatsHandler* uploadStatsHandler = nullptr;
};

UploadServiceContainer::UploadServiceContainer() : impl_(std::make_unique<Impl>()) {}

UploadServiceContainer::~UploadServiceContainer() { shutdown(); }

bool UploadServiceContainer::initialize(common::IQueryExecutor* queryExecutor,
                                         common::LdapConnectionPool* ldapPool,
                                         const std::string& ldapBaseDn) {
    spdlog::info("UploadServiceContainer initializing...");

    if (!queryExecutor) {
        spdlog::error("UploadServiceContainer::initialize: queryExecutor cannot be nullptr");
        return false;
    }

    // Create dedicated DB pool + queryExecutor for upload module
    // (shared queryExecutor causes OCI thread-safety issues in async upload threads)
    // Use minimal pool size to avoid Oracle XE session exhaustion
    try {
        // Temporarily override pool size env vars for minimal pool
        auto origMin = std::getenv("DB_POOL_MIN");
        auto origMax = std::getenv("DB_POOL_MAX");
        setenv("DB_POOL_MIN", "1", 1);
        setenv("DB_POOL_MAX", "3", 1);
        impl_->ownDbPool = common::DbConnectionPoolFactory::createFromEnv();
        // Restore original values
        if (origMin) setenv("DB_POOL_MIN", origMin, 1); else unsetenv("DB_POOL_MIN");
        if (origMax) setenv("DB_POOL_MAX", origMax, 1); else unsetenv("DB_POOL_MAX");
        if (impl_->ownDbPool && impl_->ownDbPool->initialize()) {
            impl_->ownQueryExecutor = common::createQueryExecutor(impl_->ownDbPool.get());
            if (impl_->ownQueryExecutor) {
                impl_->queryExecutor = impl_->ownQueryExecutor.get();
                spdlog::info("UploadServiceContainer: dedicated DB pool created (type={})",
                             impl_->ownDbPool->getDatabaseType());
            } else {
                spdlog::warn("UploadServiceContainer: failed to create own queryExecutor, using shared");
                impl_->queryExecutor = queryExecutor;
            }
        } else {
            spdlog::warn("UploadServiceContainer: failed to create own DB pool, using shared");
            impl_->queryExecutor = queryExecutor;
        }
    } catch (const std::exception& e) {
        spdlog::warn("UploadServiceContainer: own DB pool creation failed: {}, using shared", e.what());
        impl_->queryExecutor = queryExecutor;
    }

    impl_->ldapPool = ldapPool;

    auto* qe = impl_->queryExecutor;
    // Repositories
    impl_->uploadRepo = std::make_shared<repositories::UploadRepository>(qe);
    impl_->certificateRepo = std::make_shared<repositories::CertificateRepository>(qe);
    impl_->crlRepo = std::make_shared<repositories::CrlRepository>(qe);
    impl_->validationRepo = std::make_shared<repositories::ValidationRepository>(
        qe, std::shared_ptr<common::LdapConnectionPool>(ldapPool, [](auto*){}), ldapBaseDn);
    impl_->deviationListRepo = std::make_shared<repositories::DeviationListRepository>(qe);
    impl_->ldifStructureRepo = std::make_shared<repositories::LdifStructureRepository>(impl_->uploadRepo.get());

    // Services
    impl_->uploadService = std::make_shared<services::UploadService>(
        impl_->uploadRepo.get(),
        impl_->certificateRepo.get(),
        ldapPool,
        impl_->deviationListRepo.get());

    impl_->validationService = std::make_shared<services::ValidationService>(
        impl_->validationRepo.get(),
        impl_->certificateRepo.get(),
        impl_->crlRepo.get(),
        nullptr,  // ldapCscaProvider — upload uses DB-based providers, not LDAP
        nullptr   // ldapCrlProvider
    );

    impl_->ldifStructureService = std::make_shared<services::LdifStructureService>(
        impl_->ldifStructureRepo.get());

    // LdapStorageService needs config — will be initialized separately if needed

    spdlog::info("UploadServiceContainer initialized (repos: 6, services: 3)");
    return true;
}

void UploadServiceContainer::shutdown() {
    if (!impl_) return;
    impl_->ldapStorageService.reset();
    impl_->ldifStructureService.reset();
    impl_->validationService.reset();
    impl_->uploadService.reset();
    impl_->ldifStructureRepo.reset();
    impl_->deviationListRepo.reset();
    impl_->validationRepo.reset();
    impl_->crlRepo.reset();
    impl_->certificateRepo.reset();
    impl_->uploadRepo.reset();
    spdlog::info("UploadServiceContainer shut down");
}

// Accessors
common::IQueryExecutor* UploadServiceContainer::queryExecutor() const { return impl_->queryExecutor; }
common::LdapConnectionPool* UploadServiceContainer::ldapPool() const { return impl_->ldapPool; }

repositories::UploadRepository* UploadServiceContainer::uploadRepository() const { return impl_->uploadRepo.get(); }
repositories::CertificateRepository* UploadServiceContainer::certificateRepository() const { return impl_->certificateRepo.get(); }
repositories::CrlRepository* UploadServiceContainer::crlRepository() const { return impl_->crlRepo.get(); }
repositories::ValidationRepository* UploadServiceContainer::validationRepository() const { return impl_->validationRepo.get(); }
repositories::DeviationListRepository* UploadServiceContainer::deviationListRepository() const { return impl_->deviationListRepo.get(); }
repositories::LdifStructureRepository* UploadServiceContainer::ldifStructureRepository() const { return impl_->ldifStructureRepo.get(); }

services::UploadService* UploadServiceContainer::uploadService() const { return impl_->uploadService.get(); }
services::ValidationService* UploadServiceContainer::validationService() const { return impl_->validationService.get(); }
services::LdifStructureService* UploadServiceContainer::ldifStructureService() const { return impl_->ldifStructureService.get(); }
services::LdapStorageService* UploadServiceContainer::ldapStorageService() const { return impl_->ldapStorageService.get(); }

handlers::UploadHandler* UploadServiceContainer::uploadHandler() const { return impl_->uploadHandler; }
handlers::UploadStatsHandler* UploadServiceContainer::uploadStatsHandler() const { return impl_->uploadStatsHandler; }

void UploadServiceContainer::setUploadHandler(handlers::UploadHandler* handler) { impl_->uploadHandler = handler; }
void UploadServiceContainer::setLdapStorageService(std::shared_ptr<services::LdapStorageService> svc) { impl_->ldapStorageService = std::move(svc); }

} // namespace infrastructure

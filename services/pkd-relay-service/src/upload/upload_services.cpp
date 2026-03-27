/**
 * @file upload_services.cpp
 * @brief UploadServiceContainer implementation
 */
#include "upload/upload_services.h"

#include "i_query_executor.h"
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
    // Borrowed (non-owning) from relay ServiceContainer
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

    impl_->queryExecutor = queryExecutor;
    impl_->ldapPool = ldapPool;

    // Repositories
    impl_->uploadRepo = std::make_shared<repositories::UploadRepository>(queryExecutor);
    impl_->certificateRepo = std::make_shared<repositories::CertificateRepository>(queryExecutor);
    impl_->crlRepo = std::make_shared<repositories::CrlRepository>(queryExecutor);
    impl_->validationRepo = std::make_shared<repositories::ValidationRepository>(
        queryExecutor, std::shared_ptr<common::LdapConnectionPool>(ldapPool, [](auto*){}), ldapBaseDn);
    impl_->deviationListRepo = std::make_shared<repositories::DeviationListRepository>(queryExecutor);
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

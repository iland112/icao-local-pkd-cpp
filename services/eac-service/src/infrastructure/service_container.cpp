/**
 * @file service_container.cpp
 * @brief ServiceContainer implementation for EAC Service
 */

#include "infrastructure/service_container.h"
#include "infrastructure/app_config.h"

#include "repositories/cvc_certificate_repository.h"
#include "services/cvc_service.h"
#include "services/eac_chain_validator.h"

#include "db_connection_pool_factory.h"
#include "i_query_executor.h"
#include <spdlog/spdlog.h>

namespace eac::infrastructure {

struct ServiceContainer::Impl {
    // Phase 1: Database
    std::shared_ptr<common::IDbConnectionPool> dbPool;
    std::unique_ptr<common::IQueryExecutor> queryExecutor;

    // Phase 2: Repositories
    std::unique_ptr<repositories::CvcCertificateRepository> cvcCertRepo;

    // Phase 3: Services
    std::unique_ptr<services::CvcService> cvcService;
    std::unique_ptr<services::EacChainValidator> chainValidator;
};

ServiceContainer::ServiceContainer() : impl_(std::make_unique<Impl>()) {}
ServiceContainer::~ServiceContainer() = default;

bool ServiceContainer::initialize(const AppConfig& /*config*/) {
    try {
        // Phase 1: Database pool + query executor (reads from environment variables)
        spdlog::info("ServiceContainer Phase 1: Database connection pool");

        impl_->dbPool = common::DbConnectionPoolFactory::createFromEnv();
        if (!impl_->dbPool) {
            spdlog::error("Failed to create DB connection pool");
            return false;
        }
        if (!impl_->dbPool->initialize()) {
            spdlog::error("Failed to initialize DB connection pool");
            return false;
        }
        spdlog::info("DB pool initialized (type={})", impl_->dbPool->getDatabaseType());

        impl_->queryExecutor = common::createQueryExecutor(impl_->dbPool.get());
        if (!impl_->queryExecutor) {
            spdlog::error("Failed to create query executor");
            return false;
        }

        // Phase 2: Repositories
        spdlog::info("ServiceContainer Phase 2: Repositories");
        impl_->cvcCertRepo = std::make_unique<repositories::CvcCertificateRepository>(
            impl_->queryExecutor.get());

        // Phase 3: Services
        spdlog::info("ServiceContainer Phase 3: Services");
        impl_->cvcService = std::make_unique<services::CvcService>(
            impl_->cvcCertRepo.get());
        impl_->chainValidator = std::make_unique<services::EacChainValidator>(
            impl_->cvcCertRepo.get());

        spdlog::info("ServiceContainer initialization complete");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("ServiceContainer initialization failed: {}", e.what());
        return false;
    }
}

void ServiceContainer::shutdown() {
    spdlog::info("ServiceContainer shutting down");
    impl_->chainValidator.reset();
    impl_->cvcService.reset();
    impl_->cvcCertRepo.reset();
    impl_->queryExecutor.reset();
    impl_->dbPool.reset();
}

common::IDbConnectionPool* ServiceContainer::dbPool() const { return impl_->dbPool.get(); }
common::IQueryExecutor* ServiceContainer::queryExecutor() const { return impl_->queryExecutor.get(); }
repositories::CvcCertificateRepository* ServiceContainer::cvcCertificateRepository() const { return impl_->cvcCertRepo.get(); }
services::CvcService* ServiceContainer::cvcService() const { return impl_->cvcService.get(); }
services::EacChainValidator* ServiceContainer::eacChainValidator() const { return impl_->chainValidator.get(); }

} // namespace eac::infrastructure

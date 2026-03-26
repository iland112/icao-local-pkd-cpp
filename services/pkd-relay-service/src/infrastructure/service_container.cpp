/**
 * @file service_container.cpp
 * @brief PKD Relay Service ServiceContainer implementation
 *
 * Sync/Reconciliation moved to pkd-management (v2.41.0).
 * Remaining: connection pools, shared repos (ICAO LDAP sync), ICAO LDAP sync service.
 */

#include "service_container.h"
#include "relay/sync/common/config.h"

#include <spdlog/spdlog.h>

#include "db_connection_pool.h"
#include "db_connection_pool_factory.h"
#include <ldap_connection_pool.h>

// Shared repositories (ICAO LDAP sync + upload module)
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include "../repositories/validation_repository.h"
#include "../services/validation_service.h"

// ICAO LDAP Sync
#include "../relay/icao-ldap/icao_ldap_sync_service.h"

namespace infrastructure {

struct ServiceContainer::Impl {
    std::shared_ptr<common::IDbConnectionPool> dbPool;
    std::unique_ptr<common::IQueryExecutor> queryExecutor;
    std::shared_ptr<common::LdapConnectionPool> ldapPool;

    // Shared repositories
    std::shared_ptr<icao::relay::repositories::CertificateRepository> certificateRepo;
    std::shared_ptr<icao::relay::repositories::CrlRepository> crlRepo;
    std::shared_ptr<icao::relay::repositories::ValidationRepository> validationRepo;
    std::shared_ptr<icao::relay::services::ValidationService> validationService;

    // ICAO LDAP Sync
    std::shared_ptr<icao::relay::IcaoLdapSyncService> icaoLdapSyncService;
};

ServiceContainer::ServiceContainer() : impl_(std::make_unique<Impl>()) {}
ServiceContainer::~ServiceContainer() { shutdown(); }

bool ServiceContainer::initialize(icao::relay::Config& config) {
    spdlog::info("Initializing PKD Relay Service dependencies...");

    try {
        // Step 1: Database connection pool
        impl_->dbPool = common::DbConnectionPoolFactory::createFromEnv();
        if (!impl_->dbPool || !impl_->dbPool->initialize()) {
            spdlog::critical("Failed to initialize database connection pool");
            return false;
        }
        std::string dbType = impl_->dbPool->getDatabaseType();
        spdlog::info("Database connection pool initialized (type={})", dbType);

        // Step 2: Query Executor
        impl_->queryExecutor = common::createQueryExecutor(impl_->dbPool.get());
        if (!impl_->queryExecutor) {
            spdlog::critical("Failed to create Query Executor");
            return false;
        }
        spdlog::info("{} Query Executor created", dbType == "postgres" ? "PostgreSQL" : "Oracle");

        // Step 3: LDAP Connection Pool
        int ldapPoolMin = 2, ldapPoolMax = 10, ldapPoolTimeout = 5;
        int ldapNetworkTimeout = 5, ldapHealthCheckTimeout = 2;
        auto safeStoi = [](const char* v, int defaultVal) {
            try { return std::stoi(v); } catch (...) { return defaultVal; }
        };
        if (auto* v = std::getenv("LDAP_POOL_MIN")) ldapPoolMin = safeStoi(v, ldapPoolMin);
        if (auto* v = std::getenv("LDAP_POOL_MAX")) ldapPoolMax = safeStoi(v, ldapPoolMax);
        if (auto* v = std::getenv("LDAP_POOL_TIMEOUT")) ldapPoolTimeout = safeStoi(v, ldapPoolTimeout);
        if (auto* v = std::getenv("LDAP_NETWORK_TIMEOUT")) ldapNetworkTimeout = safeStoi(v, ldapNetworkTimeout);
        if (auto* v = std::getenv("LDAP_HEALTH_CHECK_TIMEOUT")) ldapHealthCheckTimeout = safeStoi(v, ldapHealthCheckTimeout);

        std::string ldapUri = "ldap://" + config.ldapWriteHost + ":" + std::to_string(config.ldapWritePort);
        impl_->ldapPool = std::make_shared<common::LdapConnectionPool>(
            ldapUri, config.ldapBindDn, config.ldapBindPassword,
            ldapPoolMin, ldapPoolMax, ldapPoolTimeout, ldapNetworkTimeout, ldapHealthCheckTimeout);
        if (!impl_->ldapPool->initialize()) {
            spdlog::critical("Failed to initialize LDAP connection pool");
            return false;
        }
        spdlog::info("LDAP connection pool initialized ({})", ldapUri);

        // Step 4: Shared repositories
        impl_->certificateRepo = std::make_shared<icao::relay::repositories::CertificateRepository>(impl_->queryExecutor.get());
        impl_->crlRepo = std::make_shared<icao::relay::repositories::CrlRepository>(impl_->queryExecutor.get());
        impl_->validationRepo = std::make_shared<icao::relay::repositories::ValidationRepository>(impl_->queryExecutor.get());
        impl_->validationService = std::make_shared<icao::relay::services::ValidationService>(
            impl_->validationRepo.get(), impl_->certificateRepo.get(), impl_->crlRepo.get());

        // Step 5: ICAO LDAP Sync Service (optional)
        if (config.icaoLdapSyncEnabled) {
            impl_->icaoLdapSyncService = std::make_shared<icao::relay::IcaoLdapSyncService>(
                config, impl_->queryExecutor.get(), impl_->ldapPool.get(),
                impl_->certificateRepo.get(), impl_->crlRepo.get(), impl_->validationRepo.get());
            spdlog::info("ICAO LDAP Sync Service created (host={}:{})", config.icaoLdapHost, config.icaoLdapPort);
        } else {
            spdlog::info("ICAO LDAP Sync disabled (ICAO_LDAP_SYNC_ENABLED=false)");
        }

        spdlog::info("PKD Relay Service dependencies initialized");
        return true;
    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize: {}", e.what());
        return false;
    }
}

void ServiceContainer::shutdown() {
    if (!impl_) return;
    impl_->icaoLdapSyncService.reset();
    impl_->validationService.reset();
    impl_->validationRepo.reset();
    impl_->crlRepo.reset();
    impl_->certificateRepo.reset();
    impl_->queryExecutor.reset();
    impl_->ldapPool.reset();
    impl_->dbPool.reset();
    spdlog::info("PKD Relay Service dependencies shut down");
}

// --- Accessors ---
common::IDbConnectionPool* ServiceContainer::dbPool() const { return impl_->dbPool.get(); }
common::IQueryExecutor* ServiceContainer::queryExecutor() const { return impl_->queryExecutor.get(); }
common::LdapConnectionPool* ServiceContainer::ldapPool() const { return impl_->ldapPool.get(); }
icao::relay::repositories::CertificateRepository* ServiceContainer::certificateRepository() const { return impl_->certificateRepo.get(); }
icao::relay::repositories::CrlRepository* ServiceContainer::crlRepository() const { return impl_->crlRepo.get(); }
icao::relay::repositories::ValidationRepository* ServiceContainer::validationRepository() const { return impl_->validationRepo.get(); }
icao::relay::services::ValidationService* ServiceContainer::validationService() const { return impl_->validationService.get(); }
icao::relay::IcaoLdapSyncService* ServiceContainer::icaoLdapSyncService() const { return impl_->icaoLdapSyncService.get(); }

} // namespace infrastructure

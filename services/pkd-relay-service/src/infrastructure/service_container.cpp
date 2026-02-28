/**
 * @file service_container.cpp
 * @brief PKD Relay Service ServiceContainer implementation
 *
 * Initializes all connection pools, repositories, and services
 * in the correct dependency order. Replaces the global variables
 * and initializeServices()/shutdownServices() from main.cpp.
 */

#include "service_container.h"
#include "relay/sync/common/config.h"

#include <spdlog/spdlog.h>

// Infrastructure
#include "db_connection_pool.h"
#include "db_connection_pool_factory.h"
#include <ldap_connection_pool.h>

// Repositories
#include "../repositories/sync_status_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include "../repositories/reconciliation_repository.h"
#include "../repositories/validation_repository.h"

// Services
#include "../services/sync_service.h"
#include "../services/reconciliation_service.h"
#include "../services/validation_service.h"

namespace infrastructure {

struct ServiceContainer::Impl {
    // Connection pools
    std::shared_ptr<common::IDbConnectionPool> dbPool;
    std::unique_ptr<common::IQueryExecutor> queryExecutor;
    std::shared_ptr<common::LdapConnectionPool> ldapPool;

    // Repositories
    std::shared_ptr<icao::relay::repositories::SyncStatusRepository> syncStatusRepo;
    std::shared_ptr<icao::relay::repositories::CertificateRepository> certificateRepo;
    std::shared_ptr<icao::relay::repositories::CrlRepository> crlRepo;
    std::shared_ptr<icao::relay::repositories::ReconciliationRepository> reconciliationRepo;
    std::shared_ptr<icao::relay::repositories::ValidationRepository> validationRepo;

    // Services
    std::shared_ptr<icao::relay::services::SyncService> syncService;
    std::shared_ptr<icao::relay::services::ReconciliationService> reconciliationService;
    std::shared_ptr<icao::relay::services::ValidationService> validationService;
};

ServiceContainer::ServiceContainer() : impl_(std::make_unique<Impl>()) {}

ServiceContainer::~ServiceContainer() {
    shutdown();
}

bool ServiceContainer::initialize(icao::relay::Config& config) {
    spdlog::info("Initializing PKD Relay Service dependencies...");

    try {
        // Step 1: Database connection pool (Factory Pattern)
        spdlog::info("Creating database connection pool using Factory Pattern...");
        impl_->dbPool = common::DbConnectionPoolFactory::createFromEnv();
        if (!impl_->dbPool) {
            spdlog::critical("Failed to create database connection pool from environment");
            return false;
        }
        if (!impl_->dbPool->initialize()) {
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
        // Read LDAP pool sizes from environment (default: min=2, max=10, timeout=5)
        int ldapPoolMin = 2, ldapPoolMax = 10, ldapPoolTimeout = 5;
        int ldapNetworkTimeout = 5, ldapHealthCheckTimeout = 2;
        if (auto* v = std::getenv("LDAP_POOL_MIN")) ldapPoolMin = std::stoi(v);
        if (auto* v = std::getenv("LDAP_POOL_MAX")) ldapPoolMax = std::stoi(v);
        if (auto* v = std::getenv("LDAP_POOL_TIMEOUT")) ldapPoolTimeout = std::stoi(v);
        if (auto* v = std::getenv("LDAP_NETWORK_TIMEOUT")) ldapNetworkTimeout = std::stoi(v);
        if (auto* v = std::getenv("LDAP_HEALTH_CHECK_TIMEOUT")) ldapHealthCheckTimeout = std::stoi(v);

        spdlog::info("Creating LDAP connection pool (min={}, max={}, networkTimeout={}s, healthCheckTimeout={}s)...",
                     ldapPoolMin, ldapPoolMax, ldapNetworkTimeout, ldapHealthCheckTimeout);
        std::string ldapUri = "ldap://" + config.ldapWriteHost + ":" +
                             std::to_string(config.ldapWritePort);
        impl_->ldapPool = std::make_shared<common::LdapConnectionPool>(
            ldapUri,
            config.ldapBindDn,
            config.ldapBindPassword,
            ldapPoolMin,
            ldapPoolMax,
            ldapPoolTimeout,
            ldapNetworkTimeout,
            ldapHealthCheckTimeout
        );
        if (!impl_->ldapPool->initialize()) {
            spdlog::critical("Failed to initialize LDAP connection pool");
            return false;
        }
        spdlog::info("LDAP connection pool initialized ({})", ldapUri);

        // Step 4: Repositories (all depend on QueryExecutor)
        spdlog::info("Creating repository instances with Query Executor...");
        impl_->syncStatusRepo = std::make_shared<icao::relay::repositories::SyncStatusRepository>(
            impl_->queryExecutor.get());
        impl_->certificateRepo = std::make_shared<icao::relay::repositories::CertificateRepository>(
            impl_->queryExecutor.get());
        impl_->crlRepo = std::make_shared<icao::relay::repositories::CrlRepository>(
            impl_->queryExecutor.get());
        impl_->reconciliationRepo = std::make_shared<icao::relay::repositories::ReconciliationRepository>(
            impl_->queryExecutor.get());
        impl_->validationRepo = std::make_shared<icao::relay::repositories::ValidationRepository>(
            impl_->queryExecutor.get());

        // Step 5: Services (depend on repositories)
        spdlog::info("Creating service instances with repository dependencies...");
        impl_->syncService = std::make_shared<icao::relay::services::SyncService>(
            impl_->syncStatusRepo,
            impl_->certificateRepo,
            impl_->crlRepo
        );

        impl_->reconciliationService = std::make_shared<icao::relay::services::ReconciliationService>(
            impl_->reconciliationRepo,
            impl_->certificateRepo,
            impl_->crlRepo
        );

        impl_->validationService = std::make_shared<icao::relay::services::ValidationService>(
            impl_->validationRepo.get()
        );

        spdlog::info("All PKD Relay Service dependencies initialized successfully");
        return true;

    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize PKD Relay Service: {}", e.what());
        return false;
    }
}

void ServiceContainer::shutdown() {
    if (!impl_) return;

    spdlog::info("Shutting down PKD Relay Service dependencies...");

    // Delete in reverse order of initialization
    // Services first
    impl_->validationService.reset();
    impl_->reconciliationService.reset();
    impl_->syncService.reset();

    // Repositories
    impl_->validationRepo.reset();
    impl_->reconciliationRepo.reset();
    impl_->crlRepo.reset();
    impl_->certificateRepo.reset();
    impl_->syncStatusRepo.reset();

    // Query Executor
    impl_->queryExecutor.reset();

    // Connection pools
    impl_->ldapPool.reset();

    if (impl_->dbPool) {
        impl_->dbPool.reset();
    }

    spdlog::info("PKD Relay Service dependencies shut down");
}

// --- Connection Pool Accessors ---
common::IDbConnectionPool* ServiceContainer::dbPool() const {
    return impl_->dbPool.get();
}

common::IQueryExecutor* ServiceContainer::queryExecutor() const {
    return impl_->queryExecutor.get();
}

common::LdapConnectionPool* ServiceContainer::ldapPool() const {
    return impl_->ldapPool.get();
}

// --- Repository Accessors ---
icao::relay::repositories::SyncStatusRepository* ServiceContainer::syncStatusRepository() const {
    return impl_->syncStatusRepo.get();
}

icao::relay::repositories::CertificateRepository* ServiceContainer::certificateRepository() const {
    return impl_->certificateRepo.get();
}

icao::relay::repositories::CrlRepository* ServiceContainer::crlRepository() const {
    return impl_->crlRepo.get();
}

icao::relay::repositories::ReconciliationRepository* ServiceContainer::reconciliationRepository() const {
    return impl_->reconciliationRepo.get();
}

icao::relay::repositories::ValidationRepository* ServiceContainer::validationRepository() const {
    return impl_->validationRepo.get();
}

// --- Service Accessors ---
icao::relay::services::SyncService* ServiceContainer::syncService() const {
    return impl_->syncService.get();
}

icao::relay::services::ReconciliationService* ServiceContainer::reconciliationService() const {
    return impl_->reconciliationService.get();
}

icao::relay::services::ValidationService* ServiceContainer::validationService() const {
    return impl_->validationService.get();
}

} // namespace infrastructure

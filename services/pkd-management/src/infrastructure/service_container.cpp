/**
 * @file service_container.cpp
 * @brief ServiceContainer implementation — centralized dependency initialization
 *
 * Replaces scattered global variable initialization in main.cpp.
 * All components are created in strict dependency order.
 *
 * @date 2026-02-17
 */

#include "service_container.h"
#include "app_config.h"

// Infrastructure (shared libraries — headers exposed via CMake PUBLIC includes)
#include "db_connection_pool.h"
#include "db_connection_pool_factory.h"
#include "i_query_executor.h"
#include <ldap_connection_pool.h>

// Repositories
#include "../repositories/upload_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/validation_repository.h"
#include "../repositories/audit_repository.h"
#include "../repositories/user_repository.h"
#include "../repositories/auth_audit_repository.h"
#include "../repositories/crl_repository.h"
#include "../repositories/ldap_certificate_repository.h"
#include "../repositories/code_master_repository.h"
#include "../repositories/api_client_repository.h"
#include "../repositories/api_client_request_repository.h"
#include "../repositories/pending_dsc_repository.h"
#include "../repositories/csr_repository.h"

// Services
#include "../services/upload_service.h"
#include "../services/validation_service.h"
#include "../services/audit_service.h"
#include "../services/certificate_service.h"
#include "../services/ldap_storage_service.h"
#include "../services/csr_service.h"

// LDAP Provider Adapters (for real-time PA Lookup validation)
#include "../adapters/ldap_csca_provider.h"
#include "../adapters/ldap_crl_provider.h"

// Handlers
#include "../handlers/auth_handler.h"
#include "../handlers/upload_handler.h"
#include "../handlers/certificate_handler.h"
#include "../handlers/code_master_handler.h"
#include "../handlers/api_client_handler.h"
#include "../handlers/api_client_request_handler.h"
#include "../handlers/csr_handler.h"

// PII Encryption (개인정보보호법)
#include "../auth/personal_info_crypto.h"

// Password hashing (admin user initialization)
#include "../auth/password_hash.h"

// Sync module (moved from pkd-relay)
#include "../sync/repositories/sync_status_repository.h"
#include "../sync/repositories/certificate_repository.h"
#include "../sync/repositories/crl_repository.h"
#include "../sync/repositories/reconciliation_repository.h"
#include "../sync/repositories/validation_repository.h"
#include "../sync/services/sync_service.h"
#include "../sync/services/reconciliation_service.h"
#include "../sync/services/validation_service.h"
#include "../sync/common/config.h"
#include "../sync/infrastructure/sync_scheduler.h"

#include <spdlog/spdlog.h>

namespace infrastructure {

struct ServiceContainer::Impl {
    // Connection pools
    std::shared_ptr<common::IDbConnectionPool> dbPool;
    std::unique_ptr<common::IQueryExecutor> queryExecutor;
    std::shared_ptr<common::LdapConnectionPool> ldapPool;

    // Repositories
    std::shared_ptr<repositories::UploadRepository> uploadRepository;
    std::shared_ptr<repositories::CertificateRepository> certificateRepository;
    std::shared_ptr<repositories::ValidationRepository> validationRepository;
    std::shared_ptr<repositories::AuditRepository> auditRepository;
    std::shared_ptr<repositories::UserRepository> userRepository;
    std::shared_ptr<repositories::AuthAuditRepository> authAuditRepository;
    std::shared_ptr<repositories::CrlRepository> crlRepository;
    std::shared_ptr<repositories::LdapCertificateRepository> ldapCertificateRepository;
    std::shared_ptr<repositories::CodeMasterRepository> codeMasterRepository;
    std::shared_ptr<repositories::ApiClientRepository> apiClientRepository;
    std::shared_ptr<repositories::ApiClientRequestRepository> apiClientRequestRepository;
    std::shared_ptr<repositories::PendingDscRepository> pendingDscRepository;
    std::shared_ptr<repositories::CsrRepository> csrRepository;

    // LDAP Provider Adapters (for real-time PA Lookup)
    std::unique_ptr<adapters::LdapCscaProvider> ldapCscaProvider;
    std::unique_ptr<adapters::LdapCrlProvider> ldapCrlProvider;

    // Services
    std::shared_ptr<services::UploadService> uploadService;
    std::shared_ptr<services::ValidationService> validationService;
    std::shared_ptr<services::AuditService> auditService;
    std::shared_ptr<services::CertificateService> certificateService;
    std::shared_ptr<services::LdapStorageService> ldapStorageService;
    std::shared_ptr<services::CsrService> csrService;

    // Handlers
    std::shared_ptr<handlers::AuthHandler> authHandler;
    std::shared_ptr<handlers::UploadHandler> uploadHandler;
    std::shared_ptr<handlers::CertificateHandler> certificateHandler;
    std::shared_ptr<handlers::CodeMasterHandler> codeMasterHandler;
    std::shared_ptr<handlers::ApiClientHandler> apiClientHandler;
    std::shared_ptr<handlers::ApiClientRequestHandler> apiClientRequestHandler;
    std::shared_ptr<handlers::CsrHandler> csrHandler;

    // Sync module (moved from pkd-relay)
    std::unique_ptr<icao::relay::Config> syncConfig;
    std::shared_ptr<icao::relay::repositories::SyncStatusRepository> syncStatusRepo;
    std::shared_ptr<icao::relay::repositories::CertificateRepository> syncCertificateRepo;
    std::shared_ptr<icao::relay::repositories::CrlRepository> syncCrlRepo;
    std::shared_ptr<icao::relay::repositories::ReconciliationRepository> syncReconciliationRepo;
    std::shared_ptr<icao::relay::repositories::ValidationRepository> syncValidationRepo;
    std::shared_ptr<icao::relay::services::SyncService> syncService;
    std::shared_ptr<icao::relay::services::ReconciliationService> reconciliationService;
    std::shared_ptr<icao::relay::services::ValidationService> syncValidationService;
    std::unique_ptr<infrastructure::SyncScheduler> syncScheduler;
};

ServiceContainer::ServiceContainer() : impl_(std::make_unique<Impl>()) {}

ServiceContainer::~ServiceContainer() {
    shutdown();
}

void ServiceContainer::shutdown() {
    if (!impl_) return;

    // Release in reverse order
    impl_->csrHandler.reset();
    impl_->apiClientRequestHandler.reset();
    impl_->apiClientHandler.reset();
    impl_->codeMasterHandler.reset();
    impl_->certificateHandler.reset();
    impl_->uploadHandler.reset();
    impl_->authHandler.reset();

    impl_->csrService.reset();
    impl_->ldapStorageService.reset();
    impl_->auditService.reset();
    impl_->validationService.reset();
    impl_->uploadService.reset();
    impl_->certificateService.reset();

    // Release LDAP providers before pool
    impl_->ldapCrlProvider.reset();
    impl_->ldapCscaProvider.reset();

    impl_->csrRepository.reset();
    impl_->pendingDscRepository.reset();
    impl_->apiClientRequestRepository.reset();
    impl_->apiClientRepository.reset();
    impl_->codeMasterRepository.reset();
    impl_->ldapCertificateRepository.reset();
    impl_->crlRepository.reset();
    impl_->authAuditRepository.reset();
    impl_->userRepository.reset();
    impl_->auditRepository.reset();
    impl_->validationRepository.reset();
    impl_->certificateRepository.reset();
    impl_->uploadRepository.reset();

    // Sync module
    impl_->syncScheduler.reset();
    impl_->syncValidationService.reset();
    impl_->reconciliationService.reset();
    impl_->syncService.reset();
    impl_->syncValidationRepo.reset();
    impl_->syncReconciliationRepo.reset();
    impl_->syncCrlRepo.reset();
    impl_->syncCertificateRepo.reset();
    impl_->syncStatusRepo.reset();
    impl_->syncConfig.reset();

    impl_->queryExecutor.reset();
    impl_->ldapPool.reset();
    if (impl_->dbPool) {
        impl_->dbPool.reset();
        spdlog::info("Database connection pool closed");
    }

    spdlog::info("ServiceContainer resources released");
}

bool ServiceContainer::initialize(const AppConfig& config) {
    spdlog::info("ServiceContainer initializing...");

    // --- Phase 0: PII Encryption (개인정보보호법 제29조 안전조치) ---
    auth::pii::initialize();

    // --- Phase 1: LDAP Connection Pool ---
    try {
        std::string ldapWriteUri = "ldap://" + config.ldapWriteHost + ":" + std::to_string(config.ldapWritePort);

        // Read LDAP pool sizes from environment (default: min=2, max=10, timeout=5)
        int ldapPoolMin = 2, ldapPoolMax = 10, ldapPoolTimeout = 5;
        int ldapNetworkTimeout = 5, ldapHealthCheckTimeout = 2;
        auto safeStoi = [](const char* v, int defaultVal) {
            try { return std::stoi(v); } catch (...) { spdlog::warn("Invalid env value '{}', using default {}", v, defaultVal); return defaultVal; }
        };
        if (auto* v = std::getenv("LDAP_POOL_MIN")) ldapPoolMin = safeStoi(v, ldapPoolMin);
        if (auto* v = std::getenv("LDAP_POOL_MAX")) ldapPoolMax = safeStoi(v, ldapPoolMax);
        if (auto* v = std::getenv("LDAP_POOL_TIMEOUT")) ldapPoolTimeout = safeStoi(v, ldapPoolTimeout);
        if (auto* v = std::getenv("LDAP_NETWORK_TIMEOUT")) ldapNetworkTimeout = safeStoi(v, ldapNetworkTimeout);
        if (auto* v = std::getenv("LDAP_HEALTH_CHECK_TIMEOUT")) ldapHealthCheckTimeout = safeStoi(v, ldapHealthCheckTimeout);

        impl_->ldapPool = std::make_shared<common::LdapConnectionPool>(
            ldapWriteUri,
            config.ldapBindDn,
            config.ldapBindPassword,
            ldapPoolMin,
            ldapPoolMax,
            ldapPoolTimeout,
            ldapNetworkTimeout,
            ldapHealthCheckTimeout
        );

        spdlog::info("LDAP connection pool initialized (min={}, max={}, networkTimeout={}s, healthCheckTimeout={}s, host={})",
                     ldapPoolMin, ldapPoolMax, ldapNetworkTimeout, ldapHealthCheckTimeout, ldapWriteUri);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize LDAP connection pool: {}", e.what());
        return false;
    }

    // --- Phase 2: Certificate Service (LDAP-based search) ---
    std::string certSearchBaseDn = config.ldapBaseDn;

    impl_->ldapCertificateRepository = std::make_shared<repositories::LdapCertificateRepository>(
        impl_->ldapPool.get(),
        certSearchBaseDn
    );
    impl_->certificateService = std::make_shared<services::CertificateService>(
        impl_->ldapCertificateRepository
    );
    spdlog::info("Certificate service initialized with LDAP connection pool (baseDN: {})", certSearchBaseDn);

    // --- Phase 3: Database Connection Pool + Query Executor ---
    try {
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

        impl_->queryExecutor = common::createQueryExecutor(impl_->dbPool.get());
        spdlog::info("Query Executor initialized (DB type: {})", impl_->queryExecutor->getDatabaseType());
    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize database connection pool: {}", e.what());
        return false;
    }

    // --- Phase 4: Repositories ---
    impl_->uploadRepository = std::make_shared<repositories::UploadRepository>(impl_->queryExecutor.get());
    impl_->certificateRepository = std::make_shared<repositories::CertificateRepository>(impl_->queryExecutor.get());
    impl_->validationRepository = std::make_shared<repositories::ValidationRepository>(
        impl_->queryExecutor.get(), impl_->ldapPool, config.ldapBaseDn);
    impl_->auditRepository = std::make_shared<repositories::AuditRepository>(impl_->queryExecutor.get());
    impl_->userRepository = std::make_shared<repositories::UserRepository>(impl_->queryExecutor.get());
    impl_->authAuditRepository = std::make_shared<repositories::AuthAuditRepository>(impl_->queryExecutor.get());
    impl_->crlRepository = std::make_shared<repositories::CrlRepository>(impl_->queryExecutor.get());
    impl_->codeMasterRepository = std::make_shared<repositories::CodeMasterRepository>(impl_->queryExecutor.get());
    impl_->apiClientRepository = std::make_shared<repositories::ApiClientRepository>(impl_->queryExecutor.get());
    impl_->apiClientRequestRepository = std::make_shared<repositories::ApiClientRequestRepository>(impl_->queryExecutor.get());
    impl_->pendingDscRepository = std::make_shared<repositories::PendingDscRepository>(impl_->queryExecutor.get());
    impl_->csrRepository = std::make_shared<repositories::CsrRepository>(impl_->queryExecutor.get());
    spdlog::info("Repositories initialized (Upload, Certificate, Validation, Audit, User, AuthAudit, CRL, DL, LdifStructure, IcaoVersion, CodeMaster, ApiClient, ApiClientRequest, PendingDsc, Csr)");

    // --- Phase 4.5: LDAP Storage Service ---
    impl_->ldapStorageService = std::make_shared<services::LdapStorageService>(config);
    spdlog::info("LDAP Storage Service initialized");

    // ICAO Sync Module moved to pkd-relay (v2.41.0)

    // --- Phase 6: Business Logic Services ---
    impl_->uploadService = std::make_shared<services::UploadService>(
        impl_->uploadRepository.get(),
        impl_->certificateRepository.get(),
        impl_->ldapPool.get()
    );

    // Create LDAP provider adapters for real-time PA Lookup validation
    impl_->ldapCscaProvider = std::make_unique<adapters::LdapCscaProvider>(
        impl_->ldapPool.get(), config.ldapBaseDn);
    impl_->ldapCrlProvider = std::make_unique<adapters::LdapCrlProvider>(
        impl_->ldapPool.get(), config.ldapBaseDn);
    spdlog::info("LDAP provider adapters initialized for real-time PA Lookup (baseDN: {})", config.ldapBaseDn);

    impl_->validationService = std::make_shared<services::ValidationService>(
        impl_->validationRepository.get(),
        impl_->certificateRepository.get(),
        impl_->crlRepository.get(),
        impl_->ldapCscaProvider.get(),
        impl_->ldapCrlProvider.get()
    );

    impl_->auditService = std::make_shared<services::AuditService>(
        impl_->auditRepository.get()
    );

    // LdifStructureService moved to pkd-relay (v2.41.0)

    impl_->csrService = std::make_shared<services::CsrService>(
        impl_->csrRepository.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("Services initialized (Upload, Validation, Audit, LdifStructure, Csr)");

    // --- Phase 7: Handlers ---
    impl_->authHandler = std::make_shared<handlers::AuthHandler>(
        impl_->userRepository.get(),
        impl_->authAuditRepository.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("Authentication handler initialized");

    impl_->uploadHandler = std::make_shared<handlers::UploadHandler>(
        impl_->uploadService.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("Upload handler initialized (2 certificate upload endpoints)");

    impl_->certificateHandler = std::make_shared<handlers::CertificateHandler>(
        impl_->certificateService.get(),
        impl_->validationService.get(),
        impl_->certificateRepository.get(),
        impl_->crlRepository.get(),
        impl_->queryExecutor.get(),
        impl_->ldapPool.get(),
        impl_->pendingDscRepository.get(),
        impl_->ldapStorageService.get()
    );
    spdlog::info("Certificate handler initialized (20 endpoints)");

    impl_->codeMasterHandler = std::make_shared<handlers::CodeMasterHandler>(
        impl_->codeMasterRepository.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("Code Master handler initialized (6 endpoints)");

    impl_->apiClientHandler = std::make_shared<handlers::ApiClientHandler>(
        impl_->apiClientRepository.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("API Client handler initialized (7 endpoints)");

    impl_->apiClientRequestHandler = std::make_shared<handlers::ApiClientRequestHandler>(
        impl_->apiClientRequestRepository.get(),
        impl_->apiClientRepository.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("API Client Request handler initialized (5 endpoints)");

    impl_->csrHandler = std::make_shared<handlers::CsrHandler>(
        impl_->csrService.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("CSR handler initialized (6 endpoints)");

    // --- Phase 8: Sync Module (moved from pkd-relay) ---
    try {
        impl_->syncConfig = std::make_unique<icao::relay::Config>();
        // Populate sync config from shared LDAP/DB environment
        impl_->syncConfig->ldapWriteHost = config.ldapWriteHost;
        impl_->syncConfig->ldapWritePort = config.ldapWritePort;
        impl_->syncConfig->ldapBindDn = config.ldapBindDn;
        impl_->syncConfig->ldapBindPassword = config.ldapBindPassword;
        impl_->syncConfig->ldapBaseDn = config.ldapBaseDn;
        impl_->syncConfig->ldapDataContainer = config.ldapDataContainer;
        impl_->syncConfig->ldapNcDataContainer = config.ldapNcDataContainer;
        // Load sync-specific settings from env
        if (auto e = std::getenv("AUTO_RECONCILE")) impl_->syncConfig->autoReconcile = (std::string(e) == "true");
        if (auto e = std::getenv("MAX_RECONCILE_BATCH_SIZE")) impl_->syncConfig->maxReconcileBatchSize = std::stoi(e);
        if (auto e = std::getenv("DAILY_SYNC_ENABLED")) impl_->syncConfig->dailySyncEnabled = (std::string(e) == "true");
        if (auto e = std::getenv("DAILY_SYNC_HOUR")) impl_->syncConfig->dailySyncHour = std::stoi(e);
        if (auto e = std::getenv("DAILY_SYNC_MINUTE")) impl_->syncConfig->dailySyncMinute = std::stoi(e);
        if (auto e = std::getenv("REVALIDATE_CERTS_ON_SYNC")) impl_->syncConfig->revalidateCertsOnSync = (std::string(e) == "true");

        // Sync repositories (use same queryExecutor as main service)
        impl_->syncStatusRepo = std::make_shared<icao::relay::repositories::SyncStatusRepository>(
            impl_->queryExecutor.get());
        impl_->syncCertificateRepo = std::make_shared<icao::relay::repositories::CertificateRepository>(
            impl_->queryExecutor.get());
        impl_->syncCrlRepo = std::make_shared<icao::relay::repositories::CrlRepository>(
            impl_->queryExecutor.get());
        impl_->syncReconciliationRepo = std::make_shared<icao::relay::repositories::ReconciliationRepository>(
            impl_->queryExecutor.get());
        impl_->syncValidationRepo = std::make_shared<icao::relay::repositories::ValidationRepository>(
            impl_->queryExecutor.get());

        // Sync services
        impl_->syncService = std::make_shared<icao::relay::services::SyncService>(
            impl_->syncStatusRepo, impl_->syncCertificateRepo, impl_->syncCrlRepo);
        impl_->reconciliationService = std::make_shared<icao::relay::services::ReconciliationService>(
            impl_->syncReconciliationRepo, impl_->syncCertificateRepo, impl_->syncCrlRepo);
        impl_->syncValidationService = std::make_shared<icao::relay::services::ValidationService>(
            impl_->syncValidationRepo.get(), impl_->syncCertificateRepo.get(), impl_->syncCrlRepo.get());

        // Sync scheduler
        impl_->syncScheduler = std::make_unique<infrastructure::SyncScheduler>();

        spdlog::info("Sync module initialized (autoReconcile={}, dailySync={})",
                     impl_->syncConfig->autoReconcile, impl_->syncConfig->dailySyncEnabled);
    } catch (const std::exception& e) {
        spdlog::warn("Sync module initialization failed: {} (non-fatal)", e.what());
    }

    // --- Phase 9: Ensure admin user exists ---
    ensureAdminUser();

    spdlog::info("ServiceContainer initialization complete");
    return true;
}

// --- Connection Pool Accessors ---
common::IQueryExecutor* ServiceContainer::queryExecutor() const { return impl_->queryExecutor.get(); }
common::LdapConnectionPool* ServiceContainer::ldapPool() const { return impl_->ldapPool.get(); }
common::IDbConnectionPool* ServiceContainer::dbPool() const { return impl_->dbPool.get(); }

// --- Repository Accessors ---
repositories::UploadRepository* ServiceContainer::uploadRepository() const { return impl_->uploadRepository.get(); }
repositories::CertificateRepository* ServiceContainer::certificateRepository() const { return impl_->certificateRepository.get(); }
repositories::ValidationRepository* ServiceContainer::validationRepository() const { return impl_->validationRepository.get(); }
repositories::AuditRepository* ServiceContainer::auditRepository() const { return impl_->auditRepository.get(); }
repositories::UserRepository* ServiceContainer::userRepository() const { return impl_->userRepository.get(); }
repositories::AuthAuditRepository* ServiceContainer::authAuditRepository() const { return impl_->authAuditRepository.get(); }
repositories::CrlRepository* ServiceContainer::crlRepository() const { return impl_->crlRepository.get(); }
repositories::CodeMasterRepository* ServiceContainer::codeMasterRepository() const { return impl_->codeMasterRepository.get(); }
repositories::ApiClientRepository* ServiceContainer::apiClientRepository() const { return impl_->apiClientRepository.get(); }
repositories::ApiClientRequestRepository* ServiceContainer::apiClientRequestRepository() const { return impl_->apiClientRequestRepository.get(); }
repositories::PendingDscRepository* ServiceContainer::pendingDscRepository() const { return impl_->pendingDscRepository.get(); }
repositories::CsrRepository* ServiceContainer::csrRepository() const { return impl_->csrRepository.get(); }

// --- Service Accessors ---
services::UploadService* ServiceContainer::uploadService() const { return impl_->uploadService.get(); }
services::ValidationService* ServiceContainer::validationService() const { return impl_->validationService.get(); }
services::AuditService* ServiceContainer::auditService() const { return impl_->auditService.get(); }
services::CertificateService* ServiceContainer::certificateService() const { return impl_->certificateService.get(); }
services::LdapStorageService* ServiceContainer::ldapStorageService() const { return impl_->ldapStorageService.get(); }
services::CsrService* ServiceContainer::csrService() const { return impl_->csrService.get(); }

// --- Handler Accessors ---
handlers::AuthHandler* ServiceContainer::authHandler() const { return impl_->authHandler.get(); }
handlers::UploadHandler* ServiceContainer::uploadHandler() const { return impl_->uploadHandler.get(); }
handlers::CertificateHandler* ServiceContainer::certificateHandler() const { return impl_->certificateHandler.get(); }
handlers::CodeMasterHandler* ServiceContainer::codeMasterHandler() const { return impl_->codeMasterHandler.get(); }
handlers::ApiClientHandler* ServiceContainer::apiClientHandler() const { return impl_->apiClientHandler.get(); }
handlers::ApiClientRequestHandler* ServiceContainer::apiClientRequestHandler() const { return impl_->apiClientRequestHandler.get(); }
handlers::CsrHandler* ServiceContainer::csrHandler() const { return impl_->csrHandler.get(); }

// --- Sync Module Accessors ---
icao::relay::repositories::SyncStatusRepository* ServiceContainer::syncStatusRepository() const { return impl_->syncStatusRepo.get(); }
icao::relay::repositories::ReconciliationRepository* ServiceContainer::reconciliationRepository() const { return impl_->syncReconciliationRepo.get(); }
icao::relay::services::SyncService* ServiceContainer::syncService() const { return impl_->syncService.get(); }
icao::relay::services::ReconciliationService* ServiceContainer::reconciliationService() const { return impl_->reconciliationService.get(); }
icao::relay::services::ValidationService* ServiceContainer::syncValidationService() const { return impl_->syncValidationService.get(); }
icao::relay::Config& ServiceContainer::syncConfig() { return *impl_->syncConfig; }
infrastructure::SyncScheduler* ServiceContainer::syncScheduler() const { return impl_->syncScheduler.get(); }

// --- Admin User Initialization ---

void ServiceContainer::ensureAdminUser() {
    try {
        // Check if admin user already exists
        auto existingAdmin = impl_->userRepository->findByUsername("admin");
        if (existingAdmin.has_value()) {
            spdlog::debug("Admin user already exists, skipping creation");
            return;
        }

        // Read initial password from environment variable
        const char* initialPassword = std::getenv("ADMIN_INITIAL_PASSWORD");
        if (!initialPassword || std::strlen(initialPassword) == 0) {
            spdlog::warn("ADMIN_INITIAL_PASSWORD not set — admin user will not be created. "
                         "Set this environment variable to create the initial admin account.");
            return;
        }

        // Validate password minimum length
        std::string password(initialPassword);
        if (password.length() < 8) {
            spdlog::error("ADMIN_INITIAL_PASSWORD must be at least 8 characters — admin user not created");
            return;
        }

        // Hash password using PBKDF2-HMAC-SHA256 (OWASP 2023: 310,000 iterations)
        std::string passwordHash = auth::hashPassword(password);

        // Insert admin user via parameterized query
        std::string dbType = impl_->queryExecutor->getDatabaseType();
        std::string insertQuery;
        if (dbType == "oracle") {
            insertQuery =
                "INSERT INTO users (id, username, password_hash, email, full_name, is_admin, permissions) "
                "VALUES (SYS_GUID(), $1, $2, $3, $4, 1, $5)";
        } else {
            insertQuery =
                "INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions) "
                "VALUES ($1, $2, $3, $4, true, $5::jsonb)";
        }

        std::vector<std::string> params = {
            "admin",
            passwordHash,
            "admin@localhost",
            "System Administrator",
            R"(["admin","upload:read","upload:file","upload:cert","cert:read","cert:export","pa:verify","pa:read","pa:stats","sync:read","report:read","ai:read","icao:read"])"
        };

        impl_->queryExecutor->executeCommand(insertQuery, params);
        spdlog::info("Initial admin user created successfully (password from ADMIN_INITIAL_PASSWORD env var)");

    } catch (const std::exception& e) {
        // Non-fatal: log warning but don't prevent service startup
        // Admin may already exist (race condition) or DB not ready
        spdlog::warn("ensureAdminUser: {} (non-fatal, admin may already exist)", e.what());
    }
}

} // namespace infrastructure

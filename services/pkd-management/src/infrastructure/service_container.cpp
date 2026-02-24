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
#include "../repositories/ldif_structure_repository.h"
#include "../repositories/user_repository.h"
#include "../repositories/auth_audit_repository.h"
#include "../repositories/crl_repository.h"
#include "../repositories/deviation_list_repository.h"
#include "../repositories/icao_version_repository.h"
#include "../repositories/ldap_certificate_repository.h"
#include "../repositories/code_master_repository.h"
#include "../repositories/api_client_repository.h"

// Services
#include "../services/upload_service.h"
#include "../services/validation_service.h"
#include "../services/audit_service.h"
#include "../services/ldif_structure_service.h"
#include "../services/certificate_service.h"
#include "../services/icao_sync_service.h"
#include "../services/ldap_storage_service.h"

// Handlers
#include "../handlers/icao_handler.h"
#include "../handlers/auth_handler.h"
#include "../handlers/upload_handler.h"
#include "../handlers/upload_stats_handler.h"
#include "../handlers/certificate_handler.h"
#include "../handlers/code_master_handler.h"
#include "../handlers/api_client_handler.h"

// HTTP and Notification infrastructure
#include "../infrastructure/http/http_client.h"
#include "../infrastructure/notification/email_sender.h"

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
    std::shared_ptr<repositories::LdifStructureRepository> ldifStructureRepository;
    std::shared_ptr<repositories::UserRepository> userRepository;
    std::shared_ptr<repositories::AuthAuditRepository> authAuditRepository;
    std::shared_ptr<repositories::CrlRepository> crlRepository;
    std::shared_ptr<repositories::DeviationListRepository> deviationListRepository;
    std::shared_ptr<repositories::IcaoVersionRepository> icaoVersionRepository;
    std::shared_ptr<repositories::LdapCertificateRepository> ldapCertificateRepository;
    std::shared_ptr<repositories::CodeMasterRepository> codeMasterRepository;
    std::shared_ptr<repositories::ApiClientRepository> apiClientRepository;

    // Services
    std::shared_ptr<services::UploadService> uploadService;
    std::shared_ptr<services::ValidationService> validationService;
    std::shared_ptr<services::AuditService> auditService;
    std::shared_ptr<services::LdifStructureService> ldifStructureService;
    std::shared_ptr<services::CertificateService> certificateService;
    std::shared_ptr<services::IcaoSyncService> icaoSyncService;
    std::shared_ptr<services::LdapStorageService> ldapStorageService;

    // Handlers
    std::shared_ptr<handlers::IcaoHandler> icaoHandler;
    std::shared_ptr<handlers::AuthHandler> authHandler;
    std::shared_ptr<handlers::UploadHandler> uploadHandler;
    std::shared_ptr<handlers::UploadStatsHandler> uploadStatsHandler;
    std::shared_ptr<handlers::CertificateHandler> certificateHandler;
    std::shared_ptr<handlers::CodeMasterHandler> codeMasterHandler;
    std::shared_ptr<handlers::ApiClientHandler> apiClientHandler;
};

ServiceContainer::ServiceContainer() : impl_(std::make_unique<Impl>()) {}

ServiceContainer::~ServiceContainer() {
    shutdown();
}

void ServiceContainer::shutdown() {
    if (!impl_) return;

    // Release in reverse order
    impl_->apiClientHandler.reset();
    impl_->codeMasterHandler.reset();
    impl_->certificateHandler.reset();
    impl_->uploadStatsHandler.reset();
    impl_->uploadHandler.reset();
    impl_->authHandler.reset();
    impl_->icaoHandler.reset();

    impl_->ldapStorageService.reset();
    impl_->icaoSyncService.reset();
    impl_->ldifStructureService.reset();
    impl_->auditService.reset();
    impl_->validationService.reset();
    impl_->uploadService.reset();
    impl_->certificateService.reset();

    impl_->apiClientRepository.reset();
    impl_->codeMasterRepository.reset();
    impl_->ldapCertificateRepository.reset();
    impl_->icaoVersionRepository.reset();
    impl_->deviationListRepository.reset();
    impl_->crlRepository.reset();
    impl_->authAuditRepository.reset();
    impl_->userRepository.reset();
    impl_->ldifStructureRepository.reset();
    impl_->auditRepository.reset();
    impl_->validationRepository.reset();
    impl_->certificateRepository.reset();
    impl_->uploadRepository.reset();

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

    // --- Phase 1: LDAP Connection Pool ---
    try {
        std::string ldapWriteUri = "ldap://" + config.ldapWriteHost + ":" + std::to_string(config.ldapWritePort);

        impl_->ldapPool = std::make_shared<common::LdapConnectionPool>(
            ldapWriteUri,
            config.ldapBindDn,
            config.ldapBindPassword,
            2,   // minConnections
            10,  // maxConnections
            5    // acquireTimeoutSec
        );

        spdlog::info("LDAP connection pool initialized (min=2, max=10, host={})", ldapWriteUri);
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
    impl_->deviationListRepository = std::make_shared<repositories::DeviationListRepository>(impl_->queryExecutor.get());
    impl_->ldifStructureRepository = std::make_shared<repositories::LdifStructureRepository>(impl_->uploadRepository.get());
    impl_->icaoVersionRepository = std::make_shared<repositories::IcaoVersionRepository>(impl_->queryExecutor.get());
    impl_->codeMasterRepository = std::make_shared<repositories::CodeMasterRepository>(impl_->queryExecutor.get());
    impl_->apiClientRepository = std::make_shared<repositories::ApiClientRepository>(impl_->queryExecutor.get());
    spdlog::info("Repositories initialized (Upload, Certificate, Validation, Audit, User, AuthAudit, CRL, DL, LdifStructure, IcaoVersion, CodeMaster, ApiClient)");

    // --- Phase 4.5: LDAP Storage Service ---
    impl_->ldapStorageService = std::make_shared<services::LdapStorageService>(config);
    spdlog::info("LDAP Storage Service initialized");

    // --- Phase 5: ICAO Sync Module ---
    spdlog::info("Initializing ICAO Auto Sync module...");

    auto httpClient = std::make_shared<infrastructure::http::HttpClient>();

    infrastructure::notification::EmailSender::EmailConfig emailConfig;
    emailConfig.smtpHost = "localhost";
    emailConfig.smtpPort = 25;
    emailConfig.fromAddress = config.notificationEmail;
    emailConfig.useTls = false;
    auto emailSender = std::make_shared<infrastructure::notification::EmailSender>(emailConfig);

    services::IcaoSyncService::Config icaoConfig;
    icaoConfig.icaoPortalUrl = config.icaoPortalUrl;
    icaoConfig.notificationEmail = config.notificationEmail;
    icaoConfig.autoNotify = config.icaoAutoNotify;
    icaoConfig.httpTimeoutSeconds = config.icaoHttpTimeout;

    impl_->icaoSyncService = std::make_shared<services::IcaoSyncService>(
        impl_->icaoVersionRepository, httpClient, emailSender, icaoConfig
    );

    impl_->icaoHandler = std::make_shared<handlers::IcaoHandler>(impl_->icaoSyncService);
    spdlog::info("ICAO Auto Sync module initialized (Portal: {}, Notify: {})",
                config.icaoPortalUrl, config.icaoAutoNotify ? "enabled" : "disabled");

    // --- Phase 6: Business Logic Services ---
    impl_->uploadService = std::make_shared<services::UploadService>(
        impl_->uploadRepository.get(),
        impl_->certificateRepository.get(),
        impl_->ldapPool.get(),
        impl_->deviationListRepository.get()
    );

    impl_->validationService = std::make_shared<services::ValidationService>(
        impl_->validationRepository.get(),
        impl_->certificateRepository.get(),
        impl_->crlRepository.get()
    );

    impl_->auditService = std::make_shared<services::AuditService>(
        impl_->auditRepository.get()
    );

    impl_->ldifStructureService = std::make_shared<services::LdifStructureService>(
        impl_->ldifStructureRepository.get()
    );

    spdlog::info("Services initialized (Upload, Validation, Audit, LdifStructure)");

    // --- Phase 7: Handlers ---
    impl_->authHandler = std::make_shared<handlers::AuthHandler>(
        impl_->userRepository.get(),
        impl_->authAuditRepository.get()
    );
    spdlog::info("Authentication handler initialized");

    handlers::UploadHandler::LdapConfig ldapCfg;
    ldapCfg.writeHost = config.ldapWriteHost;
    ldapCfg.writePort = config.ldapWritePort;
    ldapCfg.bindDn = config.ldapBindDn;
    ldapCfg.bindPassword = config.ldapBindPassword;
    ldapCfg.baseDn = config.ldapBaseDn;
    ldapCfg.trustAnchorPath = config.trustAnchorPath;

    impl_->uploadHandler = std::make_shared<handlers::UploadHandler>(
        impl_->uploadService.get(),
        impl_->validationService.get(),
        impl_->ldifStructureService.get(),
        impl_->uploadRepository.get(),
        impl_->certificateRepository.get(),
        impl_->crlRepository.get(),
        impl_->validationRepository.get(),
        impl_->queryExecutor.get(),
        ldapCfg
    );
    spdlog::info("Upload handler initialized (10 endpoints)");

    impl_->uploadStatsHandler = std::make_shared<handlers::UploadStatsHandler>(
        impl_->uploadService.get(),
        impl_->uploadRepository.get(),
        impl_->certificateRepository.get(),
        impl_->validationRepository.get(),
        impl_->queryExecutor.get()
    );
    spdlog::info("Upload Stats handler initialized (11 endpoints)");

    impl_->certificateHandler = std::make_shared<handlers::CertificateHandler>(
        impl_->certificateService.get(),
        impl_->validationService.get(),
        impl_->certificateRepository.get(),
        impl_->crlRepository.get(),
        impl_->queryExecutor.get(),
        impl_->ldapPool.get()
    );
    spdlog::info("Certificate handler initialized (12 endpoints)");

    impl_->codeMasterHandler = std::make_shared<handlers::CodeMasterHandler>(
        impl_->codeMasterRepository.get()
    );
    spdlog::info("Code Master handler initialized (6 endpoints)");

    impl_->apiClientHandler = std::make_shared<handlers::ApiClientHandler>(
        impl_->apiClientRepository.get()
    );
    spdlog::info("API Client handler initialized (7 endpoints)");

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
repositories::LdifStructureRepository* ServiceContainer::ldifStructureRepository() const { return impl_->ldifStructureRepository.get(); }
repositories::UserRepository* ServiceContainer::userRepository() const { return impl_->userRepository.get(); }
repositories::AuthAuditRepository* ServiceContainer::authAuditRepository() const { return impl_->authAuditRepository.get(); }
repositories::CrlRepository* ServiceContainer::crlRepository() const { return impl_->crlRepository.get(); }
repositories::DeviationListRepository* ServiceContainer::deviationListRepository() const { return impl_->deviationListRepository.get(); }
repositories::CodeMasterRepository* ServiceContainer::codeMasterRepository() const { return impl_->codeMasterRepository.get(); }
repositories::ApiClientRepository* ServiceContainer::apiClientRepository() const { return impl_->apiClientRepository.get(); }

// --- Service Accessors ---
services::UploadService* ServiceContainer::uploadService() const { return impl_->uploadService.get(); }
services::ValidationService* ServiceContainer::validationService() const { return impl_->validationService.get(); }
services::AuditService* ServiceContainer::auditService() const { return impl_->auditService.get(); }
services::LdifStructureService* ServiceContainer::ldifStructureService() const { return impl_->ldifStructureService.get(); }
services::CertificateService* ServiceContainer::certificateService() const { return impl_->certificateService.get(); }
services::IcaoSyncService* ServiceContainer::icaoSyncService() const { return impl_->icaoSyncService.get(); }
services::LdapStorageService* ServiceContainer::ldapStorageService() const { return impl_->ldapStorageService.get(); }

// --- Handler Accessors ---
handlers::IcaoHandler* ServiceContainer::icaoHandler() const { return impl_->icaoHandler.get(); }
handlers::AuthHandler* ServiceContainer::authHandler() const { return impl_->authHandler.get(); }
handlers::UploadHandler* ServiceContainer::uploadHandler() const { return impl_->uploadHandler.get(); }
handlers::UploadStatsHandler* ServiceContainer::uploadStatsHandler() const { return impl_->uploadStatsHandler.get(); }
handlers::CertificateHandler* ServiceContainer::certificateHandler() const { return impl_->certificateHandler.get(); }
handlers::CodeMasterHandler* ServiceContainer::codeMasterHandler() const { return impl_->codeMasterHandler.get(); }
handlers::ApiClientHandler* ServiceContainer::apiClientHandler() const { return impl_->apiClientHandler.get(); }

} // namespace infrastructure

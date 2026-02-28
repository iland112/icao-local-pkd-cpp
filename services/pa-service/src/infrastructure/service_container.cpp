/**
 * @file service_container.cpp
 * @brief PA Service ServiceContainer implementation
 */

#include "service_container.h"
#include "app_config.h"

#include <spdlog/spdlog.h>
#include <ldap.h>
#include <thread>
#include <chrono>

// Infrastructure
#include "db_connection_pool.h"
#include "db_connection_pool_factory.h"

// Repositories
#include "../repositories/pa_verification_repository.h"
#include "../repositories/data_group_repository.h"
#include "../repositories/ldap_certificate_repository.h"
#include "../repositories/ldap_crl_repository.h"

// Parsers
#include <sod_parser.h>
#include <dg_parser.h>

// Services
#include "../services/certificate_validation_service.h"
#include "../services/dsc_auto_registration_service.h"
#include "../services/pa_verification_service.h"

namespace infrastructure {

struct ServiceContainer::Impl {
    // Connection pools
    std::shared_ptr<common::IDbConnectionPool> dbPool;
    std::unique_ptr<common::IQueryExecutor> queryExecutor;
    LDAP* ldapConn = nullptr;

    // Repositories
    std::unique_ptr<repositories::PaVerificationRepository> paVerificationRepo;
    std::unique_ptr<repositories::DataGroupRepository> dataGroupRepo;
    std::unique_ptr<repositories::LdapCertificateRepository> ldapCertificateRepo;
    std::unique_ptr<repositories::LdapCrlRepository> ldapCrlRepo;

    // Parsers
    std::unique_ptr<icao::SodParser> sodParser;
    std::unique_ptr<icao::DgParser> dgParser;

    // Services
    std::unique_ptr<services::CertificateValidationService> certificateValidationService;
    std::unique_ptr<services::DscAutoRegistrationService> dscAutoRegistrationService;
    std::unique_ptr<services::PaVerificationService> paVerificationService;
};

ServiceContainer::ServiceContainer() : impl_(std::make_unique<Impl>()) {}

ServiceContainer::~ServiceContainer() {
    shutdown();
}

namespace {

/**
 * @brief Establish LDAP connection with retry logic
 */
LDAP* connectLdap(const AppConfig& config) {
    std::string ldapUri = "ldap://" + config.ldapHost + ":" + std::to_string(config.ldapPort);
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 100;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        LDAP* ld = nullptr;
        int rc = ldap_initialize(&ld, ldapUri.c_str());
        if (rc != LDAP_SUCCESS) {
            spdlog::warn("LDAP initialize failed (attempt {}/{}): {}", attempt, MAX_RETRIES, ldap_err2string(rc));
            if (attempt < MAX_RETRIES) {
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                continue;
            }
            return nullptr;
        }

        int version = LDAP_VERSION3;
        ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

        int networkTimeoutSec = 5;
        if (auto* v = std::getenv("LDAP_NETWORK_TIMEOUT")) networkTimeoutSec = std::stoi(v);
        struct timeval timeout;
        timeout.tv_sec = networkTimeoutSec;
        timeout.tv_usec = 0;
        ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &timeout);

        struct berval cred;
        cred.bv_val = const_cast<char*>(config.ldapBindPassword.c_str());
        cred.bv_len = config.ldapBindPassword.length();

        rc = ldap_sasl_bind_s(ld, config.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
        if (rc != LDAP_SUCCESS) {
            spdlog::warn("LDAP bind failed (attempt {}/{}): {}", attempt, MAX_RETRIES, ldap_err2string(rc));
            ldap_unbind_ext_s(ld, nullptr, nullptr);
            if (attempt < MAX_RETRIES) {
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                continue;
            }
            return nullptr;
        }

        spdlog::debug("LDAP connection established (attempt {})", attempt);
        return ld;
    }

    return nullptr;
}

} // anonymous namespace

bool ServiceContainer::initialize(const AppConfig& config) {
    spdlog::info("Initializing PA Service dependencies...");

    try {
        // Step 1: Database connection pool
        impl_->dbPool = common::DbConnectionPoolFactory::createFromEnv();
        if (!impl_->dbPool) {
            spdlog::critical("Failed to create database connection pool");
            return false;
        }
        if (!impl_->dbPool->initialize()) {
            spdlog::critical("Failed to initialize database connection pool");
            return false;
        }
        spdlog::info("Database connection pool initialized (type={})", impl_->dbPool->getDatabaseType());

        // Step 2: Query Executor
        impl_->queryExecutor = common::createQueryExecutor(impl_->dbPool.get());
        if (!impl_->queryExecutor) {
            spdlog::critical("Failed to create Query Executor");
            return false;
        }
        spdlog::info("Query Executor initialized (DB type: {})", impl_->queryExecutor->getDatabaseType());

        // Step 3: LDAP connection
        impl_->ldapConn = connectLdap(config);
        if (!impl_->ldapConn) {
            spdlog::critical("Failed to establish LDAP connection");
            return false;
        }

        // Step 4: Repositories
        impl_->paVerificationRepo = std::make_unique<repositories::PaVerificationRepository>(
            impl_->queryExecutor.get());

        impl_->dataGroupRepo = std::make_unique<repositories::DataGroupRepository>(
            impl_->queryExecutor.get());

        impl_->ldapCertificateRepo = std::make_unique<repositories::LdapCertificateRepository>(
            impl_->ldapConn, config.ldapBaseDn);

        impl_->ldapCrlRepo = std::make_unique<repositories::LdapCrlRepository>(
            impl_->ldapConn, config.ldapBaseDn);

        // Step 5: Parsers
        impl_->sodParser = std::make_unique<icao::SodParser>();
        impl_->dgParser = std::make_unique<icao::DgParser>();

        // Step 6: Services
        impl_->certificateValidationService = std::make_unique<services::CertificateValidationService>(
            impl_->ldapCertificateRepo.get(),
            impl_->ldapCrlRepo.get());

        impl_->dscAutoRegistrationService = std::make_unique<services::DscAutoRegistrationService>(
            impl_->queryExecutor.get());

        impl_->paVerificationService = std::make_unique<services::PaVerificationService>(
            impl_->paVerificationRepo.get(),
            impl_->dataGroupRepo.get(),
            impl_->sodParser.get(),
            impl_->certificateValidationService.get(),
            impl_->dgParser.get(),
            impl_->dscAutoRegistrationService.get());

        spdlog::info("All PA Service dependencies initialized successfully");
        return true;

    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize PA Service: {}", e.what());
        return false;
    }
}

void ServiceContainer::shutdown() {
    if (!impl_) return;

    spdlog::info("Shutting down PA Service dependencies...");

    // Delete in reverse order of initialization
    impl_->paVerificationService.reset();
    impl_->dscAutoRegistrationService.reset();
    impl_->certificateValidationService.reset();
    impl_->dgParser.reset();
    impl_->sodParser.reset();
    impl_->ldapCrlRepo.reset();
    impl_->ldapCertificateRepo.reset();
    impl_->dataGroupRepo.reset();
    impl_->paVerificationRepo.reset();

    // LDAP connection cleanup
    if (impl_->ldapConn) {
        ldap_unbind_ext_s(impl_->ldapConn, nullptr, nullptr);
        impl_->ldapConn = nullptr;
    }

    impl_->queryExecutor.reset();

    if (impl_->dbPool) {
        impl_->dbPool->shutdown();
        impl_->dbPool.reset();
    }

    spdlog::info("PA Service dependencies shut down");
}

// --- Accessors ---
common::IDbConnectionPool* ServiceContainer::dbPool() const { return impl_->dbPool.get(); }
common::IQueryExecutor* ServiceContainer::queryExecutor() const { return impl_->queryExecutor.get(); }
repositories::PaVerificationRepository* ServiceContainer::paVerificationRepository() const { return impl_->paVerificationRepo.get(); }
repositories::DataGroupRepository* ServiceContainer::dataGroupRepository() const { return impl_->dataGroupRepo.get(); }
repositories::LdapCertificateRepository* ServiceContainer::ldapCertificateRepository() const { return impl_->ldapCertificateRepo.get(); }
repositories::LdapCrlRepository* ServiceContainer::ldapCrlRepository() const { return impl_->ldapCrlRepo.get(); }
icao::SodParser* ServiceContainer::sodParser() const { return impl_->sodParser.get(); }
icao::DgParser* ServiceContainer::dgParser() const { return impl_->dgParser.get(); }
services::CertificateValidationService* ServiceContainer::certificateValidationService() const { return impl_->certificateValidationService.get(); }
services::DscAutoRegistrationService* ServiceContainer::dscAutoRegistrationService() const { return impl_->dscAutoRegistrationService.get(); }
services::PaVerificationService* ServiceContainer::paVerificationService() const { return impl_->paVerificationService.get(); }

} // namespace infrastructure

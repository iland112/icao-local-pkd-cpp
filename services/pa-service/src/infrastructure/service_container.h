#pragma once

/**
 * @file service_container.h
 * @brief Centralized service container for PA Service dependency management
 *
 * Owns all connection pools, repositories, and services.
 * Provides non-owning pointer accessors for handler construction.
 */

#include <memory>

struct AppConfig;

// Forward declarations - Infrastructure
namespace common {
    class IDbConnectionPool;
    class IQueryExecutor;
}

// Forward declarations - Repositories
namespace repositories {
    class PaVerificationRepository;
    class DataGroupRepository;
    class LdapCertificateRepository;
    class LdapCrlRepository;
}

// Forward declarations - Services/Parsers
namespace icao {
    class SodParser;
    class DgParser;
}
namespace services {
    class CertificateValidationService;
    class DscAutoRegistrationService;
    class PaVerificationService;
}

namespace infrastructure {

class ServiceContainer {
public:
    ServiceContainer();
    ~ServiceContainer();

    // Non-copyable, non-movable
    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;

    /**
     * @brief Initialize all components in dependency order
     * @param config Application configuration
     * @return true on success, false on failure (details logged)
     */
    bool initialize(const AppConfig& config);

    /**
     * @brief Release all resources (called automatically by destructor)
     */
    void shutdown();

    // --- Connection Pool Accessors ---
    common::IDbConnectionPool* dbPool() const;
    common::IQueryExecutor* queryExecutor() const;

    // --- Repository Accessors ---
    repositories::PaVerificationRepository* paVerificationRepository() const;
    repositories::DataGroupRepository* dataGroupRepository() const;
    repositories::LdapCertificateRepository* ldapCertificateRepository() const;
    repositories::LdapCrlRepository* ldapCrlRepository() const;

    // --- Service/Parser Accessors ---
    icao::SodParser* sodParser() const;
    icao::DgParser* dgParser() const;
    services::CertificateValidationService* certificateValidationService() const;
    services::DscAutoRegistrationService* dscAutoRegistrationService() const;
    services::PaVerificationService* paVerificationService() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infrastructure

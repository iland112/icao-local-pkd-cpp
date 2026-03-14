#pragma once

/**
 * @file service_container.h
 * @brief Centralized DI container for EAC Service
 */

#include <memory>

namespace eac {
struct AppConfig;
}

namespace common {
class IDbConnectionPool;
class IQueryExecutor;
}

namespace eac::repositories {
class CvcCertificateRepository;
}

namespace eac::services {
class CvcService;
class EacChainValidator;
}

namespace eac::infrastructure {

class ServiceContainer {
public:
    ServiceContainer();
    ~ServiceContainer();

    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;

    bool initialize(const AppConfig& config);
    void shutdown();

    // Accessors
    common::IDbConnectionPool* dbPool() const;
    common::IQueryExecutor* queryExecutor() const;

    repositories::CvcCertificateRepository* cvcCertificateRepository() const;

    services::CvcService* cvcService() const;
    services::EacChainValidator* eacChainValidator() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eac::infrastructure

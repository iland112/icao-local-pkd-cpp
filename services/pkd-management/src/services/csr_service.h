#pragma once

#include <string>
#include <vector>
#include <json/json.h>

/**
 * @file csr_service.h
 * @brief CSR Service - ICAO PKD Certificate Signing Request generation
 *
 * Generates RSA-2048 key pairs and PKCS#10 CSR signed with SHA256withRSA
 * per ICAO PKD requirements (March 2026+).
 *
 * Security:
 * - Private key: AES-256-GCM encrypted (PII_ENCRYPTION_KEY)
 * - CSR PEM: AES-256-GCM encrypted at rest
 * - Audit logging handled by CsrHandler (request context required)
 */

namespace repositories {
    class CsrRepository;
}

namespace common {
    class IQueryExecutor;
}

namespace services {

struct CsrGenerateRequest {
    std::string countryCode;
    std::string organization;
    std::string commonName;
    std::string memo;
    std::string createdBy;
};

struct CsrGenerateResult {
    bool success;
    std::string id;
    std::string subjectDn;
    std::string csrPem;
    std::string publicKeyFingerprint;
    std::string errorMessage;
};

class CsrService {
public:
    CsrService(repositories::CsrRepository* csrRepo,
               common::IQueryExecutor* queryExecutor);
    ~CsrService() = default;

    CsrGenerateResult generate(const CsrGenerateRequest& req);
    CsrGenerateResult importCsr(const std::string& csrPem,
                                 const std::string& privateKeyPem,
                                 const std::string& memo,
                                 const std::string& createdBy);
    CsrGenerateResult registerCertificate(const std::string& id,
                                           const std::string& certPem,
                                           const std::string& username);

    Json::Value getById(const std::string& id);
    Json::Value list(int page, int pageSize, const std::string& statusFilter = "");
    std::string getPemById(const std::string& id);
    bool deleteById(const std::string& id);

private:
    std::string buildSubjectDn(const std::string& countryCode,
                               const std::string& organization,
                               const std::string& commonName);

    repositories::CsrRepository* csrRepo_;
    common::IQueryExecutor* queryExecutor_;
};

} // namespace services

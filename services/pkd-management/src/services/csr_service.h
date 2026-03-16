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
 * ICAO Requirements:
 * - RSA 2048 bit public key
 * - Signed using SHA256withRSA
 * - Base64 encoded (PEM)
 * - No restrictions on subjectDN
 */

namespace repositories {
    class CsrRepository;
}

namespace common {
    class IQueryExecutor;
}

namespace services {

struct CsrGenerateRequest {
    std::string countryCode;    // C= (optional)
    std::string organization;   // O= (optional)
    std::string commonName;     // CN= (optional)
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

    /**
     * @brief Generate RSA-2048 key pair + PKCS#10 CSR
     * @param req Subject DN fields and metadata
     * @return Result with CSR PEM and DB ID
     */
    CsrGenerateResult generate(const CsrGenerateRequest& req);

    /**
     * @brief Get CSR by ID
     */
    Json::Value getById(const std::string& id);

    /**
     * @brief List CSRs with pagination
     */
    Json::Value list(int page, int pageSize, const std::string& statusFilter = "");

    /**
     * @brief Get CSR DER binary by ID
     * @return DER bytes or empty vector if not found
     */
    std::vector<uint8_t> getDerById(const std::string& id);

    /**
     * @brief Get CSR PEM by ID
     * @return PEM string or empty if not found
     */
    std::string getPemById(const std::string& id);

    /**
     * @brief Delete CSR by ID
     */
    bool deleteById(const std::string& id);

private:
    /**
     * @brief Build subject DN string from individual fields
     */
    std::string buildSubjectDn(const std::string& countryCode,
                               const std::string& organization,
                               const std::string& commonName);

    repositories::CsrRepository* csrRepo_;
    common::IQueryExecutor* queryExecutor_;
};

} // namespace services

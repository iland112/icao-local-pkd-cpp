#pragma once

#include <string>
#include <vector>
#include <json/json.h>
#include <icao/audit/audit_log.h>

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
 * - All operations: audit logged to operation_audit_log
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
     * All data encrypted before DB storage. Audit logged.
     */
    CsrGenerateResult generate(const CsrGenerateRequest& req);

    /**
     * @brief Get CSR by ID (decrypts PEM for display). Audit logged.
     */
    Json::Value getById(const std::string& id, const std::string& username = "");

    /**
     * @brief List CSRs with pagination (no PEM data, summary only)
     */
    Json::Value list(int page, int pageSize, const std::string& statusFilter = "");

    /**
     * @brief Get CSR DER binary by ID. Audit logged as export.
     */
    std::vector<uint8_t> getDerById(const std::string& id, const std::string& username = "");

    /**
     * @brief Get CSR PEM by ID (decrypted). Audit logged as export.
     */
    std::string getPemById(const std::string& id, const std::string& username = "");

    /**
     * @brief Import externally generated CSR + private key pair.
     * Validates CSR signature, extracts metadata, encrypts and stores.
     */
    CsrGenerateResult importCsr(const std::string& csrPem,
                                 const std::string& privateKeyPem,
                                 const std::string& memo,
                                 const std::string& createdBy);

    /**
     * @brief Register ICAO-issued certificate for a CSR.
     * Parses X.509 cert, verifies public key matches CSR, encrypts and stores.
     * @param id CSR ID
     * @param certPem PEM-encoded certificate from ICAO
     * @param username User performing the registration
     * @return Result with success flag and error message
     */
    CsrGenerateResult registerCertificate(const std::string& id,
                                           const std::string& certPem,
                                           const std::string& username);

    /**
     * @brief Delete CSR by ID. Audit logged.
     */
    bool deleteById(const std::string& id, const std::string& username = "");

private:
    std::string buildSubjectDn(const std::string& countryCode,
                               const std::string& organization,
                               const std::string& commonName);

    void logAudit(icao::audit::OperationType opType,
                  const std::string& username,
                  const std::string& resourceId,
                  const Json::Value& metadata = Json::nullValue,
                  bool success = true,
                  const std::string& errorMessage = "");

    repositories::CsrRepository* csrRepo_;
    common::IQueryExecutor* queryExecutor_;
};

} // namespace services

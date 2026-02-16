#pragma once

#include <string>
#include <memory>
#include <openssl/x509.h>
#include <json/json.h>
#include <icao/validation/types.h>
#include <icao/validation/trust_chain_builder.h>
#include <icao/validation/crl_checker.h>
#include "../repositories/validation_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include "../adapters/db_csca_provider.h"
#include "../adapters/db_crl_provider.h"

/**
 * @file validation_service.h
 * @brief Validation Service - Certificate Validation Business Logic Layer
 *
 * Delegates core validation logic to icao::validation library:
 *   - cert_ops: pure X509 operations
 *   - TrustChainBuilder: trust chain building (via ICscaProvider)
 *   - CrlChecker: CRL revocation check (via ICrlProvider)
 *
 * This service handles:
 *   - Orchestration (DB reads + validation + DB writes)
 *   - Result storage and retrieval
 *   - Batch re-validation
 *
 * @date 2026-02-16
 */

namespace services {

/**
 * @brief Validation Service Class
 *
 * Encapsulates all business logic related to certificate validation.
 * Pure validation logic is delegated to icao::validation shared library.
 */
class ValidationService {
public:
    /**
     * @brief Constructor with Repository Dependency Injection
     * @param validationRepo Validation repository (non-owning pointer)
     * @param certRepo Certificate repository (non-owning pointer)
     * @param crlRepo CRL repository for revocation checking (non-owning pointer, optional)
     */
    ValidationService(
        repositories::ValidationRepository* validationRepo,
        repositories::CertificateRepository* certRepo,
        repositories::CrlRepository* crlRepo = nullptr
    );

    ~ValidationService() = default;

    /// @name DSC Re-validation
    /// @{

    struct RevalidateResult {
        bool success;
        int totalProcessed;
        int validCount;
        int expiredValidCount;
        int invalidCount;
        int pendingCount;
        int errorCount;
        std::string message;
        double durationSeconds;
    };

    RevalidateResult revalidateDscCertificates();

    /// @}

    /// @name Single Certificate Validation
    /// @{

    struct ValidationResult {
        bool trustChainValid;
        std::string trustChainMessage;
        std::string trustChainPath;

        bool signatureValid;
        std::string signatureError;

        bool crlChecked;
        bool revoked;
        std::string crlMessage;

        bool cscaFound;
        std::string cscaSubjectDn;
        std::string cscaFingerprint;

        bool dscExpired;
        bool cscaExpired;

        std::string validationStatus;
        std::string errorMessage;
    };

    ValidationResult validateCertificate(
        X509* cert,
        const std::string& certType = "DSC"
    );

    /// @}

    /// @name Validation Result Retrieval
    /// @{

    Json::Value getValidationByFingerprint(const std::string& fingerprint);
    Json::Value getValidationBySubjectDn(const std::string& subjectDn);
    Json::Value getValidationsByUploadId(
        const std::string& uploadId,
        int limit,
        int offset,
        const std::string& statusFilter = "",
        const std::string& certTypeFilter = ""
    );
    Json::Value getValidationStatistics(const std::string& uploadId);

    /// @}

    /// @name Link Certificate Validation
    /// @{

    struct LinkCertValidationResult {
        bool isValid;
        std::string message;
        std::string trustChainPath;
        int chainLength;
        std::vector<std::string> certificateDns;
    };

    LinkCertValidationResult validateLinkCertificate(X509* cert);

    /// @}

private:
    // Repository Dependencies
    repositories::ValidationRepository* validationRepo_;
    repositories::CertificateRepository* certRepo_;
    repositories::CrlRepository* crlRepo_;

    // Provider Adapters (owned)
    std::unique_ptr<adapters::DbCscaProvider> cscaProvider_;
    std::unique_ptr<adapters::DbCrlProvider> crlProvider_;

    // Validation Library Components
    std::unique_ptr<icao::validation::TrustChainBuilder> trustChainBuilder_;
    std::unique_ptr<icao::validation::CrlChecker> crlChecker_;
};

} // namespace services

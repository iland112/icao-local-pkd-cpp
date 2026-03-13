#pragma once

#include <string>
#include <set>
#include <memory>
#include <openssl/x509.h>
#include <json/json.h>
#include <icao/validation/types.h>
#include <icao/validation/trust_chain_builder.h>
#include <icao/validation/crl_checker.h>
#include <icao/validation/providers.h>
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
 * Supports two validation modes:
 *   1. DB-based (DbCscaProvider) — used during upload processing and revalidation
 *   2. LDAP-based (via injected ICscaProvider/ICrlProvider) — used for real-time PA Lookup
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
     * @param ldapCscaProvider LDAP-based CSCA provider for real-time PA Lookup (optional)
     * @param ldapCrlProvider LDAP-based CRL provider for real-time PA Lookup (optional)
     */
    ValidationService(
        repositories::ValidationRepository* validationRepo,
        repositories::CertificateRepository* certRepo,
        repositories::CrlRepository* crlRepo = nullptr,
        icao::validation::ICscaProvider* ldapCscaProvider = nullptr,
        icao::validation::ICrlProvider* ldapCrlProvider = nullptr
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

    /**
     * @brief Re-validate PENDING DSCs for specific countries
     *
     * Called automatically when new CSCAs are uploaded. Finds PENDING DSCs
     * for the given country codes and re-runs trust chain validation.
     *
     * @param countryCodes Set of country codes whose CSCAs were newly added
     * @return RevalidateResult with counts of re-validated certificates
     */
    RevalidateResult revalidatePendingDscForCountries(const std::set<std::string>& countryCodes);

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

    /// @name Real-time LDAP Validation (PA Lookup)
    /// @{

    /**
     * @brief Validate DSC in real-time using LDAP CSCA/CRL lookup
     *
     * Finds DSC from DB by fingerprint or subject DN, then builds
     * trust chain against LDAP CSCAs and checks CRL in real-time.
     * This replaces pre-computed DB result lookup for PA Lookup endpoint.
     *
     * ICAO Doc 9303 compliant: uses latest CSCA/CRL data from LDAP.
     *
     * @param subjectDn DSC subject DN (empty if using fingerprint)
     * @param fingerprint DSC fingerprint (empty if using subject DN)
     * @return JSON response with validation result (same format as getValidationByFingerprint)
     */
    Json::Value validateDscRealTime(const std::string& subjectDn, const std::string& fingerprint);

    /// @}

    /// @name Validation Result Retrieval (pre-computed from DB)
    /// @{

    Json::Value getValidationByFingerprint(const std::string& fingerprint);
    Json::Value getValidationBySubjectDn(const std::string& subjectDn);
    Json::Value getValidationsByUploadId(
        const std::string& uploadId,
        int limit,
        int offset,
        const std::string& statusFilter = "",
        const std::string& certTypeFilter = "",
        const std::string& icaoCategoryFilter = ""
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

    // DB-based Provider Adapters (for upload-time validation)
    std::unique_ptr<adapters::DbCscaProvider> cscaProvider_;
    std::unique_ptr<adapters::DbCrlProvider> crlProvider_;

    // DB-based Validation Library Components
    std::unique_ptr<icao::validation::TrustChainBuilder> trustChainBuilder_;
    std::unique_ptr<icao::validation::CrlChecker> crlChecker_;

    // LDAP-based Provider References (for real-time PA Lookup validation)
    icao::validation::ICscaProvider* ldapCscaProvider_;  // non-owning
    icao::validation::ICrlProvider* ldapCrlProvider_;    // non-owning

    // LDAP-based Validation Library Components
    std::unique_ptr<icao::validation::TrustChainBuilder> ldapTrustChainBuilder_;
    std::unique_ptr<icao::validation::CrlChecker> ldapCrlChecker_;
};

} // namespace services

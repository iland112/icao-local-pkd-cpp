#pragma once

/**
 * @file certificate_handler.h
 * @brief Certificate endpoints handler
 *
 * Provides certificate-related API endpoints:
 * - GET  /api/certificates/search          - Search certificates from LDAP/DB
 * - GET  /api/certificates/detail          - Get certificate details
 * - GET  /api/certificates/validation      - Get validation result by fingerprint
 * - POST /api/certificates/pa-lookup       - Lightweight PA lookup
 * - GET  /api/certificates/export/file     - Export single certificate file
 * - GET  /api/certificates/export/country  - Export country certificates (ZIP)
 * - GET  /api/certificates/export/all      - Export all as DIT-structured ZIP
 * - GET  /api/certificates/countries       - Get country list
 * - GET  /api/certificates/dsc-nc/report   - DSC_NC non-conformant report
 * - POST /api/validate/link-cert           - Validate Link Certificate
 * - GET  /api/link-certs/search            - Search Link Certificates
 * - GET  /api/link-certs/{id}              - Get Link Certificate detail
 *
 * Uses Repository Pattern for database-agnostic operation.
 *
 * @date 2026-02-17
 */

#include <drogon/drogon.h>
#include <string>

// Forward declarations - services
namespace services {
    class CertificateService;
    class ValidationService;
}

// Forward declarations - repositories
namespace repositories {
    class CertificateRepository;
    class CrlRepository;
}

// Forward declarations - common
namespace common {
    class IQueryExecutor;
    class LdapConnectionPool;
}

namespace handlers {

/**
 * @brief Certificate endpoints handler
 *
 * Provides all certificate-related API endpoints extracted from main.cpp.
 * Manages certificate search, export, validation, DSC_NC reporting,
 * and Link Certificate operations.
 */
class CertificateHandler {
public:
    /**
     * @brief Construct CertificateHandler
     *
     * Initializes all dependencies for certificate operations.
     *
     * @param certificateService Certificate service (non-owning pointer)
     * @param validationService Validation service (non-owning pointer)
     * @param certificateRepository Certificate repository (non-owning pointer)
     * @param crlRepository CRL repository (non-owning pointer)
     * @param queryExecutor Query executor for DB operations (non-owning pointer)
     * @param ldapPool LDAP connection pool (non-owning pointer)
     */
    CertificateHandler(
        services::CertificateService* certificateService,
        services::ValidationService* validationService,
        repositories::CertificateRepository* certificateRepository,
        repositories::CrlRepository* crlRepository,
        common::IQueryExecutor* queryExecutor,
        common::LdapConnectionPool* ldapPool);

    /**
     * @brief Register certificate routes
     *
     * Registers all certificate endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    // --- Dependencies (non-owning pointers) ---
    services::CertificateService* certificateService_;
    services::ValidationService* validationService_;
    repositories::CertificateRepository* certificateRepository_;
    repositories::CrlRepository* crlRepository_;
    common::IQueryExecutor* queryExecutor_;
    common::LdapConnectionPool* ldapPool_;

    // --- Handler methods ---

    /**
     * @brief GET /api/certificates/search
     *
     * Search certificates from LDAP or DB (when source filter specified).
     * Query params: country, certType, validity, searchTerm, source, limit, offset
     */
    void handleSearch(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/detail
     *
     * Get certificate details by DN.
     * Query params: dn
     */
    void handleDetail(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/validation
     *
     * Get validation result by fingerprint.
     * Query params: fingerprint
     */
    void handleValidation(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/certificates/pa-lookup
     *
     * Lightweight PA lookup by subject DN or fingerprint.
     * JSON body: { subjectDn, fingerprint }
     */
    void handlePaLookup(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/export/file
     *
     * Export single certificate file (PEM or DER).
     * Query params: dn, format
     */
    void handleExportFile(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/export/country
     *
     * Export all certificates by country as ZIP.
     * Query params: country, format
     */
    void handleExportCountry(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/export/all
     *
     * Export all LDAP-stored data as DIT-structured ZIP.
     * Query params: format
     */
    void handleExportAll(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/countries
     *
     * Get list of available countries from certificate repository.
     */
    void handleCountries(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/dsc-nc/report
     *
     * DSC_NC non-conformant certificate report with aggregation.
     * Query params: country, conformanceCode, page, size
     */
    void handleDscNcReport(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/validate/link-cert
     *
     * Validate Link Certificate trust chain.
     * JSON body: { certificateBinary (base64) }
     */
    void handleValidateLinkCert(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/link-certs/search
     *
     * Search Link Certificates.
     * Query params: country, validOnly, limit, offset
     */
    void handleLinkCertsSearch(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/link-certs/{id}
     *
     * Get Link Certificate details by ID.
     */
    void handleLinkCertDetail(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /**
     * @brief GET /api/certificates/crl/report
     * CRL report with aggregation and per-CRL revoked certificate parsing.
     */
    void handleCrlReport(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/certificates/crl/{id}
     * CRL detail including parsed revoked certificate list.
     */
    void handleCrlDetail(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);
};

} // namespace handlers

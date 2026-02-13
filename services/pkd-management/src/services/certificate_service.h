/**
 * @file certificate_service.h
 * @brief Application Service - Certificate Business Logic
 *
 * Clean Architecture: Application Service Layer
 * Orchestrates use cases and coordinates domain logic
 */

#pragma once

#include "../domain/models/certificate.h"
#include "../repositories/ldap_certificate_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include "i_query_executor.h"
#include <memory>
#include <string>
#include <vector>

namespace services {

/**
 * @brief Certificate export format
 */
enum class ExportFormat {
    DER,  // Binary DER format
    PEM   // Base64 PEM format
};

/**
 * @brief Result of certificate export operation
 */
struct ExportResult {
    std::vector<uint8_t> data;
    std::string filename;
    std::string contentType;
    bool success;
    std::string errorMessage;
};

/**
 * @brief Certificate Service - Application Layer
 *
 * Implements use cases for certificate search, detail view, and export.
 * Coordinates repository access and domain logic.
 */
class CertificateService {
public:
    /**
     * @brief Constructor with dependency injection
     * @param repository Certificate repository (LDAP or mock)
     */
    explicit CertificateService(
        std::shared_ptr<repositories::ICertificateRepository> repository
    );

    /**
     * @brief Search certificates with filters and pagination
     * @param criteria Search parameters
     * @return Search result with certificates
     * @throws std::exception on errors
     */
    domain::models::CertificateSearchResult searchCertificates(
        const domain::models::CertificateSearchCriteria& criteria
    );

    /**
     * @brief Get certificate details by DN
     * @param dn Distinguished Name
     * @return Certificate entity with full details
     * @throws std::exception if not found
     */
    domain::models::Certificate getCertificateDetail(const std::string& dn);

    /**
     * @brief Export single certificate file
     * @param dn Distinguished Name
     * @param format Export format (DER or PEM)
     * @return Export result with binary data and metadata
     */
    ExportResult exportCertificateFile(
        const std::string& dn,
        ExportFormat format
    );

    /**
     * @brief Export all certificates for a country as ZIP archive
     * @param country ISO 3166-1 alpha-2 code
     * @param format Certificate format in ZIP (DER or PEM)
     * @return Export result with ZIP binary data
     */
    ExportResult exportCountryCertificates(
        const std::string& country,
        ExportFormat format
    );

private:
    std::shared_ptr<repositories::ICertificateRepository> repository_;

    /**
     * @brief Convert DER to PEM format
     * @param derData Binary DER data
     * @param certType Certificate type (for PEM header/footer)
     * @return PEM-encoded string as bytes
     */
    std::vector<uint8_t> convertDerToPem(
        const std::vector<uint8_t>& derData,
        domain::models::CertificateType certType
    );

    /**
     * @brief Generate filename for certificate export
     * @param cert Certificate entity
     * @param format Export format
     * @return Filename (e.g., "US_CSCA_37.pem")
     */
    std::string generateCertificateFilename(
        const domain::models::Certificate& cert,
        ExportFormat format
    );

    /**
     * @brief Generate filename for certificate export by DN
     * @param dn Distinguished Name
     * @param format Export format
     * @return Filename
     */
    std::string generateFilenameFromDn(
        const std::string& dn,
        ExportFormat format
    );

    /**
     * @brief Create ZIP archive from multiple certificates
     * @param dns List of Distinguished Names
     * @param format Certificate format in ZIP
     * @return ZIP binary data
     */
    std::vector<uint8_t> createZipArchive(
        const std::vector<std::string>& dns,
        ExportFormat format
    );

    /**
     * @brief Get content type for HTTP response
     * @param format Export format
     * @param isZip Whether it's a ZIP archive
     * @return MIME type string
     */
    std::string getContentType(ExportFormat format, bool isZip = false);

    /**
     * @brief Get file extension for format
     * @param format Export format
     * @param certType Certificate type
     * @return Extension (e.g., ".pem", ".crt", ".crl")
     */
    std::string getFileExtension(
        ExportFormat format,
        domain::models::CertificateType certType
    );
};

/**
 * @brief Export all LDAP-stored data as DIT-structured ZIP archive
 *
 * Queries DB for all stored_in_ldap=TRUE certificates and CRLs.
 * Queries LDAP directly for Master Lists (o=ml entries).
 * Creates ZIP with LDAP DIT folder structure:
 *   data/{country}/{csca|lc|dsc|mlsc|crl|ml}/
 *   nc-data/{country}/dsc/
 *
 * @param certRepo Certificate repository (DB)
 * @param crlRepo CRL repository (DB)
 * @param queryExecutor Query executor (unused, kept for compatibility)
 * @param format PEM or DER
 * @param ldapPool LDAP connection pool for ML retrieval (optional, nullptr to skip MLs)
 * @return ExportResult with ZIP binary
 */
ExportResult exportAllCertificatesFromDb(
    repositories::CertificateRepository* certRepo,
    repositories::CrlRepository* crlRepo,
    common::IQueryExecutor* queryExecutor,
    ExportFormat format,
    common::LdapConnectionPool* ldapPool = nullptr
);

} // namespace services

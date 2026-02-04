#pragma once

#include <string>
#include <vector>
#include <optional>
#include <libpq-fe.h>
#include <json/json.h>
#include "db_connection_pool.h"
#include <openssl/x509.h>

/**
 * @file certificate_repository.h
 * @brief Certificate Repository - Database Access Layer for certificate table
 *
 * Handles all database operations related to certificates (CSCA, DSC, DSC_NC, MLSC, Link Certs).
 * Database-agnostic interface (currently PostgreSQL, future: Oracle support).
 *
 * @note Part of main.cpp refactoring Phase 1.5
 * @date 2026-01-29
 */

namespace repositories {

/**
 * @brief Certificate Search Filter
 */
struct CertificateSearchFilter {
    std::optional<std::string> fingerprint;
    std::optional<std::string> subjectDn;
    std::optional<std::string> issuerDn;
    std::optional<std::string> countryCode;
    std::optional<std::string> certificateType;  // "CSCA", "DSC", "DSC_NC", "MLSC"
    int limit = 100;
    int offset = 0;
};

/**
 * @brief Repository for certificate table
 *
 * Handles database operations for certificate CRUD and search.
 * Database-agnostic design for future Oracle migration.
 */
class CertificateRepository {
public:
    explicit CertificateRepository(common::DbConnectionPool* dbPool);
    ~CertificateRepository() = default;

    // ========================================================================
    // Search Operations
    // ========================================================================

    /**
     * @brief Search certificates with filters
     * @param filter Search filter
     * @return JSON array of certificates
     */
    Json::Value search(const CertificateSearchFilter& filter);

    /**
     * @brief Find certificate by fingerprint (SHA-256)
     * @param fingerprint 64-character hex string
     * @return JSON object if found, null otherwise
     */
    Json::Value findByFingerprint(const std::string& fingerprint);

    /**
     * @brief Find certificates by country code
     * @param countryCode ISO 3166-1 alpha-2 code
     * @param limit Maximum results
     * @param offset Pagination offset
     * @return JSON array of certificates
     */
    Json::Value findByCountry(const std::string& countryCode, int limit, int offset);

    /**
     * @brief Find certificates by subject DN
     * @param subjectDn Subject Distinguished Name
     * @param limit Maximum results
     * @return JSON array of certificates
     */
    Json::Value findBySubjectDn(const std::string& subjectDn, int limit);

    // ========================================================================
    // Certificate Counts
    // ========================================================================

    /**
     * @brief Count certificates by type
     * @param certType "CSCA", "DSC", "DSC_NC", "MLSC"
     * @return Count
     */
    int countByType(const std::string& certType);

    /**
     * @brief Count total certificates
     * @return Total count
     */
    int countAll();

    /**
     * @brief Count certificates by country
     * @param countryCode Country code
     * @return Count
     */
    int countByCountry(const std::string& countryCode);

    // ========================================================================
    // LDAP Storage Tracking
    // ========================================================================

    /**
     * @brief Find certificates not yet stored in LDAP
     * @param limit Maximum results
     * @return JSON array of certificates
     */
    Json::Value findNotStoredInLdap(int limit);

    /**
     * @brief Mark certificate as stored in LDAP
     * @param fingerprint Certificate fingerprint
     * @return true if updated successfully
     */
    bool markStoredInLdap(const std::string& fingerprint);

    // ========================================================================
    // Duplicate Certificate Tracking (v2.2.1)
    // ========================================================================

    /**
     * @brief Find the upload_id of the first upload that introduced this certificate
     * @param fingerprint SHA-256 fingerprint of the certificate
     * @return Upload ID string if found, empty string otherwise
     */
    std::string findFirstUploadIdByFingerprint(const std::string& fingerprint);

    /**
     * @brief Save duplicate certificate record to duplicate_certificate table
     * @param uploadId Current upload ID that detected this duplicate
     * @param firstUploadId Upload ID that first introduced this certificate
     * @param fingerprint SHA-256 fingerprint of the certificate
     * @param certType Certificate type (CSCA, DSC, DSC_NC, MLSC, CRL)
     * @param subjectDn Subject DN
     * @param issuerDn Issuer DN
     * @param countryCode Country code (optional)
     * @param serialNumber Serial number (optional)
     * @return true if saved successfully, false otherwise
     */
    bool saveDuplicate(const std::string& uploadId,
                      const std::string& firstUploadId,
                      const std::string& fingerprint,
                      const std::string& certType,
                      const std::string& subjectDn,
                      const std::string& issuerDn,
                      const std::string& countryCode = "",
                      const std::string& serialNumber = "");

    // ========================================================================
    // X509 Certificate Retrieval (for Validation)
    // ========================================================================

    /**
     * @brief Find CSCA certificate by issuer DN
     * Used for DSC trust chain validation.
     * Uses normalized DN comparison to handle format variations.
     *
     * @param issuerDn Issuer DN from DSC certificate
     * @return X509* certificate if found, nullptr otherwise
     * @note Caller must free the returned X509* using X509_free()
     */
    X509* findCscaByIssuerDn(const std::string& issuerDn);

    /**
     * @brief Find ALL CSCA certificates matching subject DN
     * Returns all CSCAs including link certificates for trust chain building.
     * Uses normalized DN comparison to handle format variations.
     *
     * @param subjectDn Subject DN to match
     * @return Vector of X509* certificates (may be empty)
     * @note Caller must free all X509* in the vector using X509_free()
     */
    std::vector<X509*> findAllCscasBySubjectDn(const std::string& subjectDn);

    /**
     * @brief Find DSC certificates that need re-validation
     * Retrieves DSC/DSC_NC certificates with CSCA_NOT_FOUND error for re-validation.
     *
     * @param limit Maximum number of certificates to retrieve
     * @return JSON array with certificate info (id, issuer_dn, certificate_data, fingerprint_sha256)
     *
     * Response format:
     * [
     *   {
     *     "id": "uuid",
     *     "issuerDn": "...",
     *     "certificateData": "\\x308...",  // PostgreSQL bytea hex format
     *     "fingerprint": "abc123..."
     *   },
     *   ...
     * ]
     */
    Json::Value findDscForRevalidation(int limit);

private:
    common::DbConnectionPool* dbPool_;  // Database connection pool (non-owning)

    // Query execution helpers
    PGresult* executeParamQuery(const std::string& query, const std::vector<std::string>& params);
    PGresult* executeQuery(const std::string& query);
    Json::Value pgResultToJson(PGresult* res);

    // DN normalization helpers (for CSCA lookup)
    std::string extractDnAttribute(const std::string& dn, const std::string& attr);
    std::string normalizeDnForComparison(const std::string& dn);
    std::string escapeSingleQuotes(const std::string& str);

    // X509 certificate parsing helper
    X509* parseCertificateData(PGresult* res, int row, int col);
};

} // namespace repositories

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"
#include <openssl/x509.h>

/**
 * @file certificate_repository.h
 * @brief Certificate Repository - Database Access Layer for certificate table
 *
 * Handles all database operations related to certificates (CSCA, DSC, DSC_NC, MLSC, Link Certs).
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @note Part of Oracle migration Phase 3: Query Executor Pattern
 * @date 2026-02-04
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
    /**
     * @brief Constructor
     * @param queryExecutor Query executor (PostgreSQL or Oracle, non-owning pointer)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit CertificateRepository(common::IQueryExecutor* queryExecutor);
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

    // ========================================================================
    // Certificate Insert & Duplicate Tracking (Phase 6.1 - Oracle Migration)
    // ========================================================================

    /**
     * @brief Save certificate with automatic duplicate detection
     *
     * Checks if certificate already exists (by type + fingerprint).
     * If exists: returns existing ID with isDuplicate=true
     * If new: inserts certificate with full X.509 metadata
     *
     * @param uploadId Upload UUID that contains this certificate
     * @param certType Certificate type (CSCA, DSC, DSC_NC, MLSC)
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @param subjectDn Subject Distinguished Name
     * @param issuerDn Issuer Distinguished Name
     * @param serialNumber Serial number (hex string)
     * @param fingerprint SHA-256 fingerprint (hex string, 64 chars)
     * @param notBefore Validity start date (ISO 8601 format)
     * @param notAfter Validity end date (ISO 8601 format)
     * @param certData Certificate data (DER encoded bytes)
     * @param validationStatus Validation status (UNKNOWN, VALID, INVALID, PENDING)
     * @param validationMessage Validation error message (optional)
     * @return pair<certificateId, isDuplicate> - UUID and duplicate flag
     *
     * @note Extracts 15 X.509 metadata fields automatically
     * @note Database-agnostic (works with PostgreSQL and Oracle)
     */
    std::pair<std::string, bool> saveCertificateWithDuplicateCheck(
        const std::string& uploadId,
        const std::string& certType,
        const std::string& countryCode,
        const std::string& subjectDn,
        const std::string& issuerDn,
        const std::string& serialNumber,
        const std::string& fingerprint,
        const std::string& notBefore,
        const std::string& notAfter,
        const std::vector<uint8_t>& certData,
        const std::string& validationStatus = "UNKNOWN",
        const std::string& validationMessage = ""
    );

    /**
     * @brief Track certificate duplicate source
     *
     * Records duplicate certificate detection in certificate_duplicates table.
     * Allows tracking all uploads that contain the same certificate.
     *
     * @param certificateId Certificate UUID from certificate table
     * @param uploadId Upload UUID that detected this duplicate
     * @param sourceType Source type (ML_FILE, LDIF_001, LDIF_002, LDIF_003)
     * @param sourceCountry Country code from source (optional)
     * @param sourceEntryDn LDIF entry DN (optional)
     * @param sourceFileName Original filename (optional)
     * @return true if tracked successfully, false on error
     */
    bool trackCertificateDuplicate(
        const std::string& certificateId,
        const std::string& uploadId,
        const std::string& sourceType,
        const std::string& sourceCountry = "",
        const std::string& sourceEntryDn = "",
        const std::string& sourceFileName = ""
    );

    /**
     * @brief Increment duplicate count for existing certificate
     *
     * Updates certificate.duplicate_count, last_seen_upload_id, last_seen_at
     * Called when the same certificate is encountered in another upload.
     *
     * @param certificateId Certificate UUID
     * @param uploadId Upload UUID that re-encountered this certificate
     * @return true if updated successfully, false on error
     */
    bool incrementDuplicateCount(
        const std::string& certificateId,
        const std::string& uploadId
    );

    /**
     * @brief Update LDAP storage status for certificate
     *
     * Marks certificate as stored in LDAP with the LDAP DN.
     * Called after successful LDAP add operation.
     *
     * @param certificateId Certificate UUID
     * @param ldapDn LDAP DN where certificate was stored
     * @return true if updated successfully, false on error
     */
    bool updateCertificateLdapStatus(
        const std::string& certificateId,
        const std::string& ldapDn
    );

    /**
     * @brief Count LDAP-stored vs total certificates for an upload
     * @param uploadId Upload UUID
     * @param outTotal Total certificate count (output)
     * @param outInLdap Count stored in LDAP (output)
     */
    void countLdapStatusByUploadId(const std::string& uploadId, int& outTotal, int& outInLdap);

    /**
     * @brief Get distinct country codes from certificates
     * @return JSON array of country code strings
     */
    Json::Value getDistinctCountries();

    /**
     * @brief Search link certificates with filters
     * @return JSON object with search results
     */
    Json::Value searchLinkCertificates(const std::string& countryFilter,
                                        const std::string& validFilter,
                                        int limit, int offset);

    /**
     * @brief Find link certificate by ID
     * @return JSON object with link certificate details, or null
     */
    Json::Value findLinkCertificateById(const std::string& id);

private:
    common::IQueryExecutor* queryExecutor_;  // Query executor (non-owning)

    // DN normalization helpers (for CSCA lookup)
    std::string extractDnAttribute(const std::string& dn, const std::string& attr);
    std::string normalizeDnForComparison(const std::string& dn);
    std::string escapeSingleQuotes(const std::string& str);

    // X509 certificate parsing helper
    /**
     * @brief Parse certificate data from hex-encoded bytea format to X509*
     * @param hexData Hex-encoded certificate data (e.g., "\\x3082...")
     * @return X509* certificate, or nullptr on failure
     * @note Caller must free the returned X509* using X509_free()
     */
    X509* parseCertificateDataFromHex(const std::string& hexData);
};

} // namespace repositories

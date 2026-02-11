/**
 * @file lc_validator.h
 * @brief Link Certificate (LC) trust chain validation
 *
 * Sprint 2: Link Certificate Validation Core
 * Implements ICAO Doc 9303 Part 12 Link Certificate validation
 *
 * Link Certificates bridge CSCA key transitions:
 * Trust Chain: CSCA (old) → LC → CSCA (new)
 *
 * @version 1.0.0
 * @date 2026-01-24
 */

#pragma once

#include "crl_validator.h"
#include <string>
#include <optional>
#include <vector>
#include <memory>
#include "i_query_executor.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>

namespace lc {

/**
 * @brief Link Certificate validation result
 *
 * Contains detailed validation status for all aspects of LC verification:
 * - Old CSCA signature validation
 * - New CSCA signature validation
 * - Certificate extensions validation
 * - Validity period check
 * - CRL revocation status
 */
struct LcValidationResult {
    // Overall Status
    bool trustChainValid;                   ///< Overall trust chain validity
    std::string validationMessage;           ///< Human-readable result message

    // Signature Validation
    bool oldCscaSignatureValid;              ///< LC signature by old CSCA
    bool newCscaSignatureValid;              ///< New CSCA signature by LC
    std::string oldCscaSubjectDn;            ///< Old CSCA Subject DN
    std::string oldCscaFingerprint;          ///< Old CSCA SHA-256 fingerprint
    std::string newCscaSubjectDn;            ///< New CSCA Subject DN
    std::string newCscaFingerprint;          ///< New CSCA SHA-256 fingerprint

    // Certificate Properties
    bool validityPeriodValid;                ///< Not before/after check
    bool extensionsValid;                    ///< X509v3 extensions validation
    std::string notBefore;                   ///< ISO 8601 format
    std::string notAfter;                    ///< ISO 8601 format

    // Extensions Details
    bool basicConstraintsCa;                 ///< CA:TRUE required for LC
    int basicConstraintsPathlen;             ///< pathlen:0 typical for LC
    std::string keyUsage;                    ///< Certificate Sign, CRL Sign
    std::string extendedKeyUsage;            ///< EKU (if present)

    // Revocation Status
    crl::RevocationStatus revocationStatus;  ///< CRL check result
    std::string revocationMessage;           ///< Revocation details

    // Validation Metadata
    int validationDurationMs;                ///< Time taken to validate
};

/**
 * @brief Link Certificate Validator
 *
 * Validates Link Certificates according to ICAO Doc 9303 Part 12:
 * 1. Verify LC signature by old CSCA
 * 2. Verify new CSCA signature by LC
 * 3. Check validity period (not before/after)
 * 4. Validate certificate extensions (BasicConstraints, KeyUsage)
 * 5. Check CRL revocation status
 *
 * Trust Chain:
 * ```
 * CSCA (old, being phased out)
 *   |
 *   | Signs LC (intermediate CA)
 *   v
 * Link Certificate (LC)
 *   |
 *   | Signs new CSCA
 *   v
 * CSCA (new, being introduced)
 * ```
 */
class LcValidator {
public:
    /**
     * @brief Construct LC validator
     * @param executor Query executor for database operations (must be valid)
     */
    explicit LcValidator(common::IQueryExecutor* executor);

    /**
     * @brief Destructor
     */
    ~LcValidator() = default;

    /**
     * @brief Validate Link Certificate trust chain
     *
     * Complete validation workflow:
     * 1. Parse LC binary (DER format)
     * 2. Extract metadata (Subject DN, Issuer DN, Serial)
     * 3. Find old CSCA by issuer DN
     * 4. Verify LC signature with old CSCA public key
     * 5. Find new CSCA by LC subject DN (forward lookup)
     * 6. Verify new CSCA signature with LC public key
     * 7. Check LC validity period
     * 8. Validate certificate extensions
     * 9. Check CRL revocation status
     * 10. Store validation result in database
     *
     * @param linkCertBinary LC certificate DER binary
     * @return LcValidationResult with detailed status
     *
     * @note Returns trustChainValid=false if any step fails
     * @note Logs all validation steps for audit trail
     */
    LcValidationResult validateLinkCertificate(
        const std::vector<uint8_t>& linkCertBinary
    );

    /**
     * @brief Validate LC from X509 object (for testing)
     *
     * @param linkCert X509 certificate object
     * @return LcValidationResult with detailed status
     */
    LcValidationResult validateLinkCertificate(X509* linkCert);

    /**
     * @brief Store LC in database
     *
     * Inserts validated LC into link_certificate table with all metadata.
     *
     * @param linkCert X509 certificate object
     * @param validationResult Validation result to store
     * @param uploadId Optional upload UUID (if from file upload)
     * @return Link certificate UUID, or empty on failure
     */
    std::string storeLinkCertificate(
        X509* linkCert,
        const LcValidationResult& validationResult,
        const std::string& uploadId = ""
    );

private:
    common::IQueryExecutor* executor_;       ///< Query executor (non-owning)
    std::unique_ptr<crl::CrlValidator> crlValidator_;  ///< CRL validator instance

    /**
     * @brief Find CSCA certificate by subject DN
     *
     * Queries certificate table for CSCA with matching subject DN.
     *
     * @param subjectDn Subject DN to search
     * @return X509 certificate object, or nullptr if not found
     *
     * @note Caller must free returned X509 with X509_free()
     */
    X509* findCscaBySubjectDn(const std::string& subjectDn);

    /**
     * @brief Find CSCA certificate by issuer DN (forward lookup)
     *
     * Finds CSCA where issuer DN matches given subject DN.
     * This is used to find "new CSCA" signed by LC.
     *
     * @param issuerDn Issuer DN to match (LC subject DN)
     * @return X509 certificate object, or nullptr if not found
     *
     * @note Caller must free returned X509 with X509_free()
     */
    X509* findCscaByIssuerDn(const std::string& issuerDn);

    /**
     * @brief Verify certificate signature
     *
     * Uses OpenSSL X509_verify() to check signature validity.
     *
     * @param cert Certificate to verify
     * @param issuerPubKey Issuer's public key
     * @return true if signature valid, false otherwise
     */
    static bool verifyCertificateSignature(X509* cert, EVP_PKEY* issuerPubKey);

    /**
     * @brief Check certificate validity period
     *
     * Verifies:
     * - notBefore <= NOW()
     * - NOW() <= notAfter
     *
     * @param cert Certificate to check
     * @return true if valid, false if expired or not yet valid
     */
    static bool checkValidityPeriod(X509* cert);

    /**
     * @brief Validate Link Certificate extensions
     *
     * Checks X509v3 extensions according to ICAO requirements:
     * - BasicConstraints: CA:TRUE, pathlen:0 (typical)
     * - KeyUsage: keyCertSign, cRLSign
     * - SubjectKeyIdentifier: Must be present
     * - AuthorityKeyIdentifier: Must be present
     *
     * @param cert Certificate to validate
     * @return true if all required extensions valid
     */
    static bool validateLcExtensions(X509* cert);

    /**
     * @brief Extract BasicConstraints extension
     *
     * @param cert Certificate
     * @return Tuple: {isCa, pathlen} or nullopt if not present
     */
    static std::optional<std::tuple<bool, int>> getBasicConstraints(X509* cert);

    /**
     * @brief Extract KeyUsage extension
     *
     * @param cert Certificate
     * @return Comma-separated key usage string (e.g., "Certificate Sign, CRL Sign")
     */
    static std::string getKeyUsage(X509* cert);

    /**
     * @brief Extract ExtendedKeyUsage extension
     *
     * @param cert Certificate
     * @return Comma-separated EKU string, or empty if not present
     */
    static std::string getExtendedKeyUsage(X509* cert);

    /**
     * @brief Extract Subject DN from X509 certificate
     *
     * @param cert Certificate
     * @return Subject DN string (RFC 2253 format)
     */
    static std::string extractSubjectDn(X509* cert);

    /**
     * @brief Extract Issuer DN from X509 certificate
     *
     * @param cert Certificate
     * @return Issuer DN string (RFC 2253 format)
     */
    static std::string extractIssuerDn(X509* cert);

    /**
     * @brief Extract Serial Number from X509 certificate
     *
     * @param cert Certificate
     * @return Serial number as hex string
     */
    static std::string extractSerialNumber(X509* cert);

    /**
     * @brief Extract SHA-256 fingerprint from X509 certificate
     *
     * @param cert Certificate
     * @return Fingerprint as hex string (64 characters)
     */
    static std::string extractFingerprint(X509* cert);

    /**
     * @brief Extract country code from Subject DN
     *
     * Parses C= component from DN (e.g., "CN=...,C=US" → "US")
     *
     * @param subjectDn Subject DN string
     * @return 2 or 3 letter country code, or empty if not found
     */
    static std::string extractCountryCode(const std::string& subjectDn);

    /**
     * @brief Convert ASN1_TIME to ISO 8601 string
     *
     * @param asn1Time OpenSSL ASN1_TIME object
     * @return ISO 8601 formatted string (YYYY-MM-DDTHH:MM:SSZ)
     */
    static std::string asn1TimeToIso8601(const ASN1_TIME* asn1Time);

    /**
     * @brief Get certificate DER binary
     *
     * @param cert X509 certificate
     * @return DER binary data
     */
    static std::vector<uint8_t> getCertificateDer(X509* cert);
};

} // namespace lc

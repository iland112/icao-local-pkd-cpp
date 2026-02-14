#pragma once

#include <string>
#include <memory>
#include <openssl/x509.h>
#include <json/json.h>
#include "../repositories/validation_repository.h"
#include "../repositories/certificate_repository.h"

/**
 * @file validation_service.h
 * @brief Validation Service - Certificate Validation Business Logic Layer
 *
 * Handles DSC certificate validation, re-validation, trust chain verification,
 * and Link Certificate validation.
 * Following DDD (Domain-Driven Design) and SRP (Single Responsibility Principle).
 *
 * Responsibilities:
 * - DSC certificate re-validation
 * - Trust chain building and validation
 * - Link Certificate validation
 * - Validation result storage and retrieval
 *
 * Does NOT handle:
 * - HTTP request/response (Controller's job)
 * - Direct database access (Repository's job)
 * - Upload processing (UploadService's job)
 *
 * @note Part of main.cpp refactoring Phase 1.6
 * @date 2026-01-29
 */

namespace services {

/**
 * @brief Validation Service Class
 *
 * Encapsulates all business logic related to certificate validation.
 * Extracted from main.cpp to improve maintainability and testability.
 */
class ValidationService {
public:
    /**
     * @brief Constructor with Repository Dependency Injection
     * @param validationRepo Validation repository (non-owning pointer)
     * @param certRepo Certificate repository (non-owning pointer)
     */
    ValidationService(
        repositories::ValidationRepository* validationRepo,
        repositories::CertificateRepository* certRepo
    );

    /**
     * @brief Destructor
     */
    ~ValidationService() = default;

    // ========================================================================
    // DSC Re-validation
    // ========================================================================

    /**
     * @brief Re-validation Result
     */
    struct RevalidateResult {
        bool success;
        int totalProcessed;
        int validCount;          // VALID + EXPIRED_VALID
        int expiredValidCount;   // EXPIRED_VALID only (subset of validCount)
        int invalidCount;
        int pendingCount;
        int errorCount;
        std::string message;
        double durationSeconds;
    };

    /**
     * @brief Re-validate all DSC certificates
     *
     * Business Logic:
     * 1. Query all DSC certificates from database
     * 2. For each certificate:
     *    a. Build trust chain (recursive CSCA lookup)
     *    b. Verify signature
     *    c. Check CRL (if available)
     *    d. Determine validation status
     * 3. Save validation results to database
     * 4. Return statistics
     *
     * @return RevalidateResult Validation statistics
     */
    RevalidateResult revalidateDscCertificates();

    /**
     * @brief Re-validate DSC certificates for specific upload
     *
     * @param uploadId Upload UUID
     * @return RevalidateResult Validation statistics
     */
    RevalidateResult revalidateDscCertificatesForUpload(const std::string& uploadId);

    // ========================================================================
    // Single Certificate Validation
    // ========================================================================

    /**
     * @brief Validation Result for Single Certificate
     */
    struct ValidationResult {
        bool trustChainValid;
        std::string trustChainMessage;
        std::string trustChainPath;  // e.g., "DSC → Link → Root"

        bool signatureValid;
        std::string signatureError;

        bool crlChecked;
        bool revoked;
        std::string crlMessage;

        bool cscaFound;
        std::string cscaSubjectDn;
        std::string cscaFingerprint;

        // ICAO Doc 9303 hybrid chain model: expiration is informational, not a hard failure
        bool dscExpired;    // DSC certificate has expired
        bool cscaExpired;   // CSCA in chain has expired

        std::string validationStatus;  // "VALID", "EXPIRED_VALID", "INVALID", "PENDING", "ERROR"
        std::string errorMessage;
    };

    /**
     * @brief Validate single certificate
     *
     * @param cert X509 certificate (non-owning pointer)
     * @param certType "DSC", "DSC_NC", "CSCA"
     * @return ValidationResult Validation result
     */
    ValidationResult validateCertificate(
        X509* cert,
        const std::string& certType = "DSC"
    );

    /**
     * @brief Validate certificate by fingerprint
     *
     * @param fingerprint SHA-256 fingerprint (64 hex chars)
     * @return ValidationResult Validation result
     */
    ValidationResult validateCertificateByFingerprint(const std::string& fingerprint);

    // ========================================================================
    // Validation Result Retrieval
    // ========================================================================

    /**
     * @brief Get validation result by certificate fingerprint
     *
     * @param fingerprint SHA-256 fingerprint
     * @return Json::Value Validation result from database
     *
     * Response format:
     * {
     *   "certificateType": "DSC",
     *   "countryCode": "KR",
     *   "validationStatus": "VALID",
     *   "trustChainValid": true,
     *   "trustChainPath": "DSC → CSCA",
     *   "signatureValid": true,
     *   "crlChecked": true,
     *   "revoked": false,
     *   "validatedAt": "2026-01-29T..."
     * }
     */
    Json::Value getValidationByFingerprint(const std::string& fingerprint);

    /**
     * @brief Get validation results for an upload (paginated)
     *
     * @param uploadId Upload UUID
     * @param limit Maximum results
     * @param offset Pagination offset
     * @param statusFilter Filter by validation_status (VALID/INVALID/PENDING)
     * @param certTypeFilter Filter by certificate_type (DSC/DSC_NC)
     * @return Json::Value Validation results with pagination metadata
     *
     * Response format:
     * {
     *   "success": true,
     *   "count": 50,
     *   "total": 29838,
     *   "limit": 50,
     *   "offset": 0,
     *   "validations": [...]
     * }
     */
    Json::Value getValidationsByUploadId(
        const std::string& uploadId,
        int limit,
        int offset,
        const std::string& statusFilter = "",
        const std::string& certTypeFilter = ""
    );

    /**
     * @brief Get validation statistics for an upload
     *
     * @param uploadId Upload UUID
     * @return Json::Value Validation statistics
     *
     * Response includes:
     * - Total count
     * - Valid count
     * - Invalid count
     * - Pending count
     * - Error count
     * - Trust chain success rate
     */
    Json::Value getValidationStatistics(const std::string& uploadId);

    // ========================================================================
    // Link Certificate Validation
    // ========================================================================

    /**
     * @brief Link Certificate Validation Result
     */
    struct LinkCertValidationResult {
        bool isValid;
        std::string message;
        std::string trustChainPath;
        int chainLength;
        std::vector<std::string> certificateDns;  // Subject DNs in chain
    };

    /**
     * @brief Validate Link Certificate trust chain
     *
     * Business Logic:
     * 1. Verify certificate has CA:TRUE basic constraint
     * 2. Verify certificate has keyCertSign key usage
     * 3. Verify certificate is not self-signed
     * 4. Build trust chain to root CSCA
     * 5. Verify each signature in chain
     * 6. Return validation result
     *
     * @param cert Link Certificate X509 (non-owning pointer)
     * @return LinkCertValidationResult Validation result
     */
    LinkCertValidationResult validateLinkCertificate(X509* cert);

    /**
     * @brief Validate Link Certificate by ID
     *
     * @param certId Certificate UUID
     * @return LinkCertValidationResult Validation result
     */
    LinkCertValidationResult validateLinkCertificateById(const std::string& certId);

private:
    // Repository Dependencies
    repositories::ValidationRepository* validationRepo_;
    repositories::CertificateRepository* certRepo_;

    // ========================================================================
    // Trust Chain Building
    // ========================================================================

    /**
     * @brief Trust Chain Node
     */
    struct TrustChainNode {
        X509* cert;
        std::string subjectDn;
        std::string issuerDn;
        std::string fingerprint;
        bool isSelfSigned;
        bool isLinkCert;
    };

    /**
     * @brief Trust Chain
     */
    struct TrustChain {
        std::vector<TrustChainNode> chain;
        std::vector<X509*> certificates;  // Raw X509 pointers for signature verification
        bool isValid;
        std::string message;
        std::string path;  // Human-readable path
    };

    /**
     * @brief Build trust chain for a certificate
     *
     * @param leafCert Leaf certificate (DSC)
     * @param maxDepth Maximum chain depth (default: 10)
     * @return TrustChain Trust chain with validation result
     */
    TrustChain buildTrustChain(X509* leafCert, int maxDepth = 10);

    /**
     * @brief Find CSCA by issuer DN
     *
     * @param issuerDn Issuer DN
     * @return X509* CSCA certificate (caller must free), or nullptr if not found
     */
    X509* findCscaByIssuerDn(const std::string& issuerDn);

    /**
     * @brief Verify certificate signature using issuer's public key
     *
     * @param cert Certificate to verify
     * @param issuerCert Issuer certificate (contains public key)
     * @return true if signature is valid
     */
    bool verifyCertificateSignature(X509* cert, X509* issuerCert);

    /**
     * @brief Validate trust chain signatures and check expiration (ICAO hybrid model)
     *
     * Per ICAO Doc 9303 Part 12, uses hybrid/chain model:
     * - Signature verification is a hard requirement
     * - Expiration is informational (reported but does not fail validation)
     *
     * @param chain Trust chain to validate
     * @param[out] cscaExpired Set to true if any CSCA in chain is expired
     * @return true if all signature verifications pass (regardless of expiration)
     */
    bool validateTrustChainInternal(const TrustChain& chain, bool& cscaExpired);

    // ========================================================================
    // CRL Check
    // ========================================================================

    /**
     * @brief Check if certificate is revoked via CRL
     *
     * @param cert Certificate to check
     * @return true if revoked, false if not revoked or CRL not found
     */
    bool checkCrlRevocation(X509* cert);

    // ========================================================================
    // Utility Methods
    // ========================================================================

    /**
     * @brief Build human-readable trust chain path
     *
     * @param chain Trust chain nodes
     * @return std::string Path (e.g., "DSC → Link → Root")
     */
    std::string buildTrustChainPath(const std::vector<TrustChainNode>& chain);

    /**
     * @brief Get certificate fingerprint (SHA-256)
     *
     * @param cert X509 certificate
     * @return std::string Hex-encoded fingerprint (64 chars)
     */
    std::string getCertificateFingerprint(X509* cert);

    /**
     * @brief Extract Subject DN from X509 certificate
     */
    std::string getSubjectDn(X509* cert);

    /**
     * @brief Extract Issuer DN from X509 certificate
     */
    std::string getIssuerDn(X509* cert);

    /**
     * @brief Check if certificate is self-signed
     */
    bool isSelfSigned(X509* cert);

    /**
     * @brief Check if certificate is a Link Certificate
     */
    bool isLinkCertificate(X509* cert);

    /**
     * @brief Normalize DN for format-independent comparison
     *
     * Handles both OpenSSL slash format (/C=X/O=Y/CN=Z) and RFC2253 comma format (CN=Z,O=Y,C=X).
     * Normalizes by lowercasing, sorting components, and joining with pipe separator.
     *
     * @param dn Distinguished Name in any format
     * @return Normalized DN string for comparison
     */
    std::string normalizeDnForComparison(const std::string& dn);

    /**
     * @brief Extract RDN attribute value from DN string
     *
     * @param dn Distinguished Name (any format)
     * @param attr Attribute name (e.g., "CN", "C", "O")
     * @return Lowercase attribute value, or empty string if not found
     */
    std::string extractDnAttribute(const std::string& dn, const std::string& attr);
};

} // namespace services

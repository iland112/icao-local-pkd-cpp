#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace icao {
namespace certificate_parser {

/**
 * @brief Certificate validation status
 */
enum class ValidationStatus {
    VALID,              ///< Certificate is valid
    EXPIRED,            ///< Certificate has expired
    NOT_YET_VALID,      ///< Certificate not yet valid (future notBefore)
    INVALID_SIGNATURE,  ///< Signature verification failed
    REVOKED,            ///< Certificate is revoked (CRL check)
    UNTRUSTED,          ///< Cannot build trust chain
    INVALID_PURPOSE,    ///< Certificate purpose doesn't match usage
    UNKNOWN_ERROR       ///< Unknown validation error
};

/**
 * @brief Certificate validation result
 *
 * Contains detailed validation information
 */
struct ValidationResult {
    bool isValid;                                   ///< Overall validation result
    ValidationStatus status;                        ///< Validation status
    std::string errorMessage;                       ///< Error description

    // Expiration check
    bool isExpired;                                 ///< Whether certificate has expired
    bool isNotYetValid;                             ///< Whether certificate is not yet valid
    std::optional<std::chrono::system_clock::time_point> notBefore;  ///< Valid from date
    std::optional<std::chrono::system_clock::time_point> notAfter;   ///< Valid until date

    // Signature verification
    bool signatureVerified;                         ///< Whether signature is valid
    std::string signatureAlgorithm;                 ///< Signature algorithm name

    // Trust chain
    bool trustChainValid;                           ///< Whether trust chain is valid
    int trustChainDepth;                            ///< Trust chain depth (0 = self-signed)
    std::vector<std::string> trustChainPath;        ///< Trust chain subject DNs

    // Purpose validation
    bool purposeValid;                              ///< Whether purpose matches usage
    std::vector<std::string> keyUsages;             ///< Key usage extensions
    std::vector<std::string> extendedKeyUsages;     ///< Extended key usage extensions

    // CRL check (optional)
    bool crlChecked;                                ///< Whether CRL was checked
    bool isRevoked;                                 ///< Whether certificate is revoked

    ValidationResult()
        : isValid(false)
        , status(ValidationStatus::UNKNOWN_ERROR)
        , isExpired(false)
        , isNotYetValid(false)
        , signatureVerified(false)
        , trustChainValid(false)
        , trustChainDepth(0)
        , purposeValid(true)
        , crlChecked(false)
        , isRevoked(false)
    {}
};

/**
 * @brief Certificate Validator
 *
 * Provides certificate validation functionality according to:
 * - RFC 5280 (X.509 PKI Certificate and CRL Profile)
 * - ICAO Doc 9303 Part 12 (PKI for MRTDs)
 *
 * Validation Checks:
 * 1. Expiration check (notBefore, notAfter)
 * 2. Signature verification
 * 3. Trust chain validation (optional)
 * 4. Purpose validation (key usage, extended key usage)
 * 5. CRL check (optional)
 *
 * Usage Example:
 * @code
 * X509* cert = ...;
 * X509* issuer = ...;
 *
 * // Basic validation
 * ValidationResult result = CertValidator::validate(cert);
 *
 * // Validation with trust chain
 * ValidationResult result = CertValidator::validate(cert, issuer);
 *
 * if (result.isValid) {
 *     std::cout << "Certificate is valid" << std::endl;
 * } else {
 *     std::cout << "Validation failed: " << result.errorMessage << std::endl;
 * }
 * @endcode
 */
class CertValidator {
public:
    /**
     * @brief Validate certificate
     *
     * @param cert Certificate to validate
     * @return ValidationResult with detailed validation info
     *
     * Performs:
     * - Expiration check
     * - Signature verification (self-signed only)
     * - Purpose validation
     */
    static ValidationResult validate(X509* cert);

    /**
     * @brief Validate certificate with issuer
     *
     * @param cert Certificate to validate
     * @param issuer Issuer certificate for signature verification
     * @return ValidationResult with detailed validation info
     *
     * Performs all checks plus:
     * - Signature verification with issuer public key
     * - Trust chain validation (1-level)
     */
    static ValidationResult validate(X509* cert, X509* issuer);

    /**
     * @brief Validate certificate with trust chain
     *
     * @param cert Certificate to validate
     * @param trustChain Trust chain certificates (issuer, intermediate CAs, root CA)
     * @return ValidationResult with detailed validation info
     *
     * Performs all checks plus:
     * - Full trust chain validation
     * - Multiple-level chain building
     */
    static ValidationResult validate(X509* cert, const std::vector<X509*>& trustChain);

    /**
     * @brief Check if certificate has expired
     *
     * @param cert Certificate to check
     * @return true if expired
     */
    static bool isExpired(X509* cert);

    /**
     * @brief Check if certificate is not yet valid
     *
     * @param cert Certificate to check
     * @return true if not yet valid
     */
    static bool isNotYetValid(X509* cert);

    /**
     * @brief Verify certificate signature
     *
     * @param cert Certificate to verify
     * @param issuer Issuer certificate (or nullptr for self-signed)
     * @return true if signature is valid
     */
    static bool verifySignature(X509* cert, X509* issuer = nullptr);

    /**
     * @brief Extract key usage extensions
     *
     * @param cert Certificate
     * @return Vector of key usage strings
     */
    static std::vector<std::string> getKeyUsages(X509* cert);

    /**
     * @brief Extract extended key usage extensions
     *
     * @param cert Certificate
     * @return Vector of extended key usage strings
     */
    static std::vector<std::string> getExtendedKeyUsages(X509* cert);

    /**
     * @brief Get signature algorithm name
     *
     * @param cert Certificate
     * @return Algorithm name (e.g., "sha256WithRSAEncryption")
     */
    static std::string getSignatureAlgorithm(X509* cert);

private:
    /**
     * @brief Perform expiration check
     *
     * @param cert Certificate
     * @param result Output validation result
     */
    static void checkExpiration(X509* cert, ValidationResult& result);

    /**
     * @brief Perform signature verification
     *
     * @param cert Certificate
     * @param issuer Issuer certificate (optional)
     * @param result Output validation result
     */
    static void checkSignature(X509* cert, X509* issuer, ValidationResult& result);

    /**
     * @brief Perform purpose validation
     *
     * @param cert Certificate
     * @param result Output validation result
     */
    static void checkPurpose(X509* cert, ValidationResult& result);

    /**
     * @brief Convert ASN1_TIME to system_clock time_point
     *
     * @param asn1Time ASN.1 time structure
     * @return time_point or nullopt on error
     */
    static std::optional<std::chrono::system_clock::time_point> asn1TimeToTimePoint(
        const ASN1_TIME* asn1Time
    );
};

} // namespace certificate_parser
} // namespace icao

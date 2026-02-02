/**
 * @file metadata_extractor.h
 * @brief X.509 certificate metadata extraction
 *
 * Extracts comprehensive metadata from X.509 certificates including
 * algorithms, key information, extensions, and validity.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <openssl/x509.h>

namespace icao {
namespace x509 {

/**
 * @brief Complete X.509 certificate metadata
 *
 * Includes all metadata fields tracked in v2.3.0 database schema.
 */
struct CertificateMetadata {
    // Version and basic info
    int version;                                     ///< 0=v1, 1=v2, 2=v3
    std::string serialNumber;                        ///< Serial number (hex)

    // Algorithm information
    std::optional<std::string> signatureAlgorithm;   ///< e.g., "sha256WithRSAEncryption"
    std::optional<std::string> signatureHashAlgorithm; ///< e.g., "SHA-256"
    std::optional<std::string> publicKeyAlgorithm;   ///< e.g., "RSA", "ECDSA"
    std::optional<int> publicKeySize;                ///< Key size in bits (2048, 4096, etc.)
    std::optional<std::string> publicKeyCurve;       ///< For ECDSA: "prime256v1", etc.

    // Key usage
    std::vector<std::string> keyUsage;               ///< digitalSignature, keyCertSign, etc.
    std::vector<std::string> extendedKeyUsage;       ///< serverAuth, clientAuth, etc.

    // CA information
    std::optional<bool> isCA;                        ///< true if CA certificate
    std::optional<int> pathLenConstraint;            ///< Path length constraint

    // Identifiers
    std::optional<std::string> subjectKeyIdentifier; ///< SKI (hex)
    std::optional<std::string> authorityKeyIdentifier; ///< AKI (hex)

    // Distribution points
    std::vector<std::string> crlDistributionPoints;  ///< CRL URLs
    std::optional<std::string> ocspResponderUrl;     ///< OCSP URL

    // Validity
    std::chrono::system_clock::time_point validFrom; ///< Not Before
    std::chrono::system_clock::time_point validTo;   ///< Not After

    // Flags
    bool isSelfSigned;                               ///< Subject DN == Issuer DN
};

/**
 * @brief Extract all metadata from certificate
 *
 * @param cert X509 certificate
 * @return CertificateMetadata with all available fields populated
 *
 * @example
 * X509* cert = parseCertificate(data);
 * CertificateMetadata meta = extractMetadata(cert);
 * std::cout << "Algorithm: " << *meta.signatureAlgorithm << std::endl;
 * std::cout << "Key Size: " << *meta.publicKeySize << " bits" << std::endl;
 */
CertificateMetadata extractMetadata(X509* cert);

/**
 * @brief Get certificate version (v1, v2, v3)
 *
 * @param cert X509 certificate
 * @return Version number (0=v1, 1=v2, 2=v3)
 */
int getVersion(X509* cert);

/**
 * @brief Get serial number as hex string
 *
 * @param cert X509 certificate
 * @return Serial number in hex format, or empty string on error
 */
std::string getSerialNumber(X509* cert);

/**
 * @brief Get signature algorithm name
 *
 * @param cert X509 certificate
 * @return Algorithm name (e.g., "sha256WithRSAEncryption"),
 *         or std::nullopt on error
 */
std::optional<std::string> getSignatureAlgorithm(X509* cert);

/**
 * @brief Get signature hash algorithm
 *
 * Extracts hash algorithm from signature algorithm.
 *
 * @param cert X509 certificate
 * @return Hash algorithm (e.g., "SHA-256"), or std::nullopt on error
 */
std::optional<std::string> getSignatureHashAlgorithm(X509* cert);

/**
 * @brief Get public key algorithm
 *
 * @param cert X509 certificate
 * @return Algorithm name ("RSA", "ECDSA", "DSA", etc.),
 *         or std::nullopt on error
 */
std::optional<std::string> getPublicKeyAlgorithm(X509* cert);

/**
 * @brief Get public key size in bits
 *
 * @param cert X509 certificate
 * @return Key size (e.g., 2048 for RSA-2048), or std::nullopt on error
 */
std::optional<int> getPublicKeySize(X509* cert);

/**
 * @brief Get elliptic curve name (for ECDSA keys)
 *
 * @param cert X509 certificate
 * @return Curve name (e.g., "prime256v1"), or std::nullopt if not ECDSA
 */
std::optional<std::string> getPublicKeyCurve(X509* cert);

/**
 * @brief Extract Key Usage extension
 *
 * @param cert X509 certificate
 * @return Vector of key usage strings (e.g., ["digitalSignature", "keyCertSign"])
 */
std::vector<std::string> getKeyUsage(X509* cert);

/**
 * @brief Extract Extended Key Usage extension
 *
 * @param cert X509 certificate
 * @return Vector of extended key usage OIDs or names
 */
std::vector<std::string> getExtendedKeyUsage(X509* cert);

/**
 * @brief Check if certificate is a CA
 *
 * Reads Basic Constraints extension.
 *
 * @param cert X509 certificate
 * @return true if CA=TRUE in Basic Constraints, std::nullopt if extension missing
 */
std::optional<bool> isCA(X509* cert);

/**
 * @brief Get path length constraint
 *
 * @param cert X509 certificate
 * @return Path length value, or std::nullopt if not set
 */
std::optional<int> getPathLenConstraint(X509* cert);

/**
 * @brief Extract Subject Key Identifier
 *
 * @param cert X509 certificate
 * @return SKI as hex string, or std::nullopt if not present
 */
std::optional<std::string> getSubjectKeyIdentifier(X509* cert);

/**
 * @brief Extract Authority Key Identifier
 *
 * @param cert X509 certificate
 * @return AKI as hex string, or std::nullopt if not present
 */
std::optional<std::string> getAuthorityKeyIdentifier(X509* cert);

/**
 * @brief Extract CRL Distribution Points
 *
 * @param cert X509 certificate
 * @return Vector of CRL URLs
 */
std::vector<std::string> getCrlDistributionPoints(X509* cert);

/**
 * @brief Extract OCSP responder URL
 *
 * Reads Authority Information Access extension.
 *
 * @param cert X509 certificate
 * @return OCSP URL, or std::nullopt if not present
 */
std::optional<std::string> getOcspResponderUrl(X509* cert);

/**
 * @brief Get certificate validity period
 *
 * @param cert X509 certificate
 * @return Pair of (notBefore, notAfter) timestamps
 */
std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
getValidityPeriod(X509* cert);

/**
 * @brief Check if certificate is currently valid
 *
 * @param cert X509 certificate
 * @return true if current time is within validity period
 */
bool isCurrentlyValid(X509* cert);

/**
 * @brief Check if certificate is expired
 *
 * @param cert X509 certificate
 * @return true if current time is after notAfter
 */
bool isExpired(X509* cert);

/**
 * @brief Get days until expiration
 *
 * @param cert X509 certificate
 * @return Number of days (negative if expired)
 */
int getDaysUntilExpiration(X509* cert);

} // namespace x509
} // namespace icao

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>

/**
 * @file x509_metadata_extractor.h
 * @brief X.509 Certificate Metadata Extraction Utilities
 *
 * Extracts all relevant metadata from X509 certificates for database storage.
 * Based on RFC 5280 and OpenSSL API.
 *
 * @date 2026-01-30
 */

namespace x509 {

/**
 * @brief Complete X.509 certificate metadata
 */
struct CertificateMetadata {
    // Basic Fields
    int version;                                // 0=v1, 1=v2, 2=v3
    std::string signatureAlgorithm;             // "sha256WithRSAEncryption"
    std::string signatureHashAlgorithm;         // "SHA-256"

    // Public Key Info
    std::string publicKeyAlgorithm;             // "RSA", "ECDSA", "DSA"
    int publicKeySize;                          // 2048, 4096, 256, 384 (bits)
    std::optional<std::string> publicKeyCurve;  // "prime256v1" (ECDSA only)

    // Key Usage Extensions
    std::vector<std::string> keyUsage;          // {"digitalSignature", "keyCertSign"}
    std::vector<std::string> extendedKeyUsage;  // {"serverAuth", "clientAuth"}

    // Basic Constraints
    bool isCA;                                  // TRUE if CA certificate
    std::optional<int> pathLenConstraint;       // NULL = unlimited

    // Identifiers
    std::optional<std::string> subjectKeyIdentifier;    // SKI (hex)
    std::optional<std::string> authorityKeyIdentifier;  // AKI (hex)

    // CRL & OCSP
    std::vector<std::string> crlDistributionPoints;     // CRL URLs
    std::optional<std::string> ocspResponderUrl;        // OCSP URL

    // Derived/Computed
    bool isSelfSigned;                          // Subject DN == Issuer DN
};

/**
 * @brief Extract complete metadata from X509 certificate
 * @param cert OpenSSL X509 certificate pointer
 * @return CertificateMetadata structure with all extracted fields
 */
CertificateMetadata extractMetadata(X509* cert);

/**
 * @brief Get certificate version
 * @param cert X509 certificate
 * @return Version number (0=v1, 1=v2, 2=v3)
 */
int getVersion(X509* cert);

/**
 * @brief Get signature algorithm name
 * @param cert X509 certificate
 * @return Algorithm name (e.g., "sha256WithRSAEncryption")
 */
std::string getSignatureAlgorithm(X509* cert);

/**
 * @brief Extract hash algorithm from signature algorithm
 * @param signatureAlgorithm Full signature algorithm name
 * @return Hash algorithm (e.g., "SHA-256", "SHA-384", "SHA-512")
 */
std::string extractHashAlgorithm(const std::string& signatureAlgorithm);

/**
 * @brief Get public key algorithm name
 * @param cert X509 certificate
 * @return Algorithm name ("RSA", "ECDSA", "DSA", "Ed25519", etc.)
 */
std::string getPublicKeyAlgorithm(X509* cert);

/**
 * @brief Get public key size in bits
 * @param cert X509 certificate
 * @return Key size (2048, 4096 for RSA; 256, 384, 521 for ECDSA)
 */
int getPublicKeySize(X509* cert);

/**
 * @brief Get elliptic curve name (ECDSA only)
 * @param cert X509 certificate
 * @return Curve name (e.g., "prime256v1", "secp384r1") or empty
 */
std::optional<std::string> getPublicKeyCurve(X509* cert);

/**
 * @brief Get Key Usage extension
 * @param cert X509 certificate
 * @return Array of usage strings (e.g., {"digitalSignature", "keyCertSign"})
 */
std::vector<std::string> getKeyUsage(X509* cert);

/**
 * @brief Get Extended Key Usage extension
 * @param cert X509 certificate
 * @return Array of usage strings (e.g., {"serverAuth", "clientAuth"})
 */
std::vector<std::string> getExtendedKeyUsage(X509* cert);

/**
 * @brief Check if certificate is a CA (Basic Constraints)
 * @param cert X509 certificate
 * @return TRUE if CA flag is set
 */
bool isCA(X509* cert);

/**
 * @brief Get path length constraint (Basic Constraints)
 * @param cert X509 certificate
 * @return Path length constraint or nullopt if unlimited
 */
std::optional<int> getPathLenConstraint(X509* cert);

/**
 * @brief Get Subject Key Identifier (SKI)
 * @param cert X509 certificate
 * @return SKI as hex string or nullopt
 */
std::optional<std::string> getSubjectKeyIdentifier(X509* cert);

/**
 * @brief Get Authority Key Identifier (AKI)
 * @param cert X509 certificate
 * @return AKI as hex string or nullopt
 */
std::optional<std::string> getAuthorityKeyIdentifier(X509* cert);

/**
 * @brief Get CRL Distribution Points
 * @param cert X509 certificate
 * @return Array of CRL URLs
 */
std::vector<std::string> getCrlDistributionPoints(X509* cert);

/**
 * @brief Get OCSP Responder URL (Authority Information Access)
 * @param cert X509 certificate
 * @return OCSP URL or nullopt
 */
std::optional<std::string> getOcspResponderUrl(X509* cert);

/**
 * @brief Check if certificate is self-signed
 * @param cert X509 certificate
 * @return TRUE if subject DN equals issuer DN
 */
bool isSelfSigned(X509* cert);

/**
 * @brief Convert byte array to hex string
 * @param data Byte array
 * @param len Length of array
 * @return Hex string (lowercase)
 */
std::string bytesToHex(const unsigned char* data, size_t len);

} // namespace x509

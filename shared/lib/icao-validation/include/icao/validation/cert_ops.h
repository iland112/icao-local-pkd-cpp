/**
 * @file cert_ops.h
 * @brief Pure X.509 certificate operations — no I/O, no DB, no LDAP
 *
 * All functions in this module are idempotent and side-effect free.
 * They operate only on OpenSSL X509/X509_CRL structures passed as arguments.
 *
 * ICAO Doc 9303 Part 12 Section 4 compliant.
 * RFC 5280 Section 6.1 (Basic Path Validation) utilities.
 *
 * @date 2026-02-16
 */

#pragma once

#include <string>
#include <openssl/x509.h>
#include <openssl/asn1.h>

namespace icao::validation {

/// @name Signature Verification
/// @{

/**
 * @brief Verify certificate signature using issuer's public key
 *
 * ICAO Doc 9303 Part 12 Section 4 — signature verification is a HARD requirement.
 *
 * @param cert Certificate to verify (non-owning)
 * @param issuerCert Issuer certificate containing public key (non-owning)
 * @return true if signature is cryptographically valid
 */
bool verifyCertificateSignature(X509* cert, X509* issuerCert);

/// @}

/// @name Certificate Status Checks
/// @{

/**
 * @brief Check if certificate has expired (notAfter < now)
 * @param cert Certificate to check (non-owning)
 * @return true if expired
 */
bool isCertificateExpired(X509* cert);

/**
 * @brief Check if certificate is not yet valid (notBefore > now)
 * @param cert Certificate to check (non-owning)
 * @return true if not yet valid
 */
bool isCertificateNotYetValid(X509* cert);

/**
 * @brief Check if certificate is self-signed (subject DN == issuer DN)
 *
 * Uses case-insensitive comparison per RFC 4517 Section 4.2.15.
 *
 * @param cert Certificate to check (non-owning)
 * @return true if self-signed
 */
bool isSelfSigned(X509* cert);

/**
 * @brief Check if certificate is a Link Certificate
 *
 * ICAO Doc 9303 Part 12: Link certificates enable CSCA key rollover.
 * Criteria: NOT self-signed, BasicConstraints CA:TRUE, KeyUsage keyCertSign.
 *
 * @param cert Certificate to check (non-owning)
 * @return true if link certificate
 */
bool isLinkCertificate(X509* cert);

/// @}

/// @name DN Extraction
/// @{

/**
 * @brief Extract Subject DN from certificate
 * @param cert Certificate (non-owning)
 * @return Subject DN in OpenSSL oneline format (e.g., "/C=KR/O=Gov/CN=CSCA")
 */
std::string getSubjectDn(X509* cert);

/**
 * @brief Extract Issuer DN from certificate
 * @param cert Certificate (non-owning)
 * @return Issuer DN in OpenSSL oneline format
 */
std::string getIssuerDn(X509* cert);

/// @}

/// @name Fingerprint
/// @{

/**
 * @brief Calculate SHA-256 fingerprint of certificate
 * @param cert Certificate (non-owning)
 * @return 64-char lowercase hex string, or empty on error
 */
std::string getCertificateFingerprint(X509* cert);

/// @}

/// @name DN Utilities
/// @{

/**
 * @brief Normalize DN for format-independent comparison
 *
 * Handles both OpenSSL slash format (/C=X/O=Y/CN=Z) and
 * RFC 2253 comma format (CN=Z,O=Y,C=X).
 * Normalizes by lowercasing, sorting components, and joining with pipe separator.
 *
 * @param dn Distinguished Name in any format
 * @return Normalized DN string for comparison
 */
std::string normalizeDnForComparison(const std::string& dn);

/**
 * @brief Extract RDN attribute value from DN string
 *
 * @param dn Distinguished Name (any format: slash or comma)
 * @param attr Attribute name (e.g., "CN", "C", "O")
 * @return Lowercase attribute value, or empty string if not found
 */
std::string extractDnAttribute(const std::string& dn, const std::string& attr);

/// @}

/// @name Time Utilities
/// @{

/**
 * @brief Convert ASN1_TIME to ISO 8601 string
 * @param t ASN.1 time structure (non-owning)
 * @return ISO 8601 formatted string (e.g., "2026-02-16T12:00:00Z"), or empty on error
 */
std::string asn1TimeToIso8601(const ASN1_TIME* t);

/// @}

} // namespace icao::validation

/**
 * @file main_utils.h
 * @brief External utility function declarations from main.cpp
 *
 * This header provides declarations for utility functions defined in main_utils.cpp
 * that are needed by other modules (e.g., masterlist_processor, upload_handler).
 *
 * Security Note: All functions handle sensitive data (certificates, LDAP credentials).
 * Ensure proper input validation and error handling when using these functions.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

// OpenSSL types (needed for x509NameToString, asn1IntegerToHex, asn1TimeToIso8601)
#include <openssl/types.h>

/**
 * @brief Decode Base64 encoded string to binary data
 *
 * @param encoded Base64 encoded string
 * @return std::vector<uint8_t> Decoded binary data
 *
 * @note Returns empty vector on decode failure
 * @note Thread-safe
 */
std::vector<uint8_t> base64Decode(const std::string& encoded);

/**
 * @brief Compute SHA-256 hash of binary data
 *
 * @param content Binary data to hash
 * @return std::string Hex-encoded SHA-256 hash (64 characters)
 *
 * @note Returns empty string on hash computation failure
 * @note Thread-safe
 */
std::string computeFileHash(const std::vector<uint8_t>& content);

/**
 * @brief Extract country code from DN (Universal)
 *
 * Extracts 2 or 3 letter country code from DN patterns in both formats:
 * - Comma-separated (RFC 4514): "C=KR, O=Gov" or "CN=Test, C=US, O=Org"
 * - Slash-separated (OpenSSL): "/C=CR/O=Junta/CN=CSCA"
 * - LDAP DN: "cn=test,o=ml,c=FR,dc=data"
 *
 * Special handling:
 * - ZZ → UN: Normalizes ISO 3166-1 "Unknown Territory" to United Nations per ICAO Doc 9303
 * - O=United Nations → UN: Detects UN organization name and forces UN country code
 *
 * @param dn Distinguished Name (X.509 Subject/Issuer DN or LDAP DN)
 * @return std::string Country code (2-3 uppercase letters), "UN" for United Nations, "XX" if not found
 *
 * @note Thread-safe
 * @note Case-insensitive
 *
 * Examples:
 * - "/C=CR/O=Junta/CN=CSCA" → "CR"
 * - "C=KR, O=Gov, CN=CSCA" → "KR"
 * - "C=ZZ, O=United Nations, CN=CSCA" → "UN" (normalized)
 * - "O=United Nations, CN=CSCA" → "UN" (org detection)
 */
std::string extractCountryCode(const std::string& dn);

/**
 * @brief Extract country code from LDAP DN (Backward Compatibility Alias)
 *
 * This function is now an alias for extractCountryCode() to maintain backward compatibility.
 * Use extractCountryCode() for new code.
 *
 * @param dn LDAP Distinguished Name
 * @return std::string Country code (2-3 uppercase letters), or "XX" if not found
 *
 * @note Thread-safe
 * @deprecated Use extractCountryCode() instead
 */
std::string extractCountryCodeFromDn(const std::string& dn);

/**
 * @brief Save Master List metadata to database using the Repository Pattern
 *
 * Inserts Master List record into the master_list table.
 *
 * @param uploadId Upload UUID
 * @param countryCode ISO 3166-1 country code
 * @param signerDn Signer certificate Subject DN
 * @param fingerprint SHA-256 fingerprint of Master List CMS
 * @param cscaCount Number of CSCAs in the Master List
 * @param mlData Master List binary data (CMS/PKCS#7)
 * @return std::string Master List ID (UUID), or empty on failure
 *
 * @warning mlData should be valid CMS/PKCS#7 structure
 * @note Uses global masterListRepository (stub implementation)
 */
std::string saveMasterList(
    const std::string& uploadId,
    const std::string& countryCode,
    const std::string& signerDn,
    const std::string& fingerprint,
    int cscaCount,
    const std::vector<uint8_t>& mlData
);

// saveMasterListToLdap() moved to services::LdapStorageService (v2.13.0)

/**
 * @brief Update Master List LDAP storage status in database
 *
 * Updates master_list.ldap_dn and ldap_stored_at fields.
 *
 * @param mlId Master List UUID
 * @param ldapDn LDAP DN where Master List is stored
 *
 * @note If ldapDn is empty, function returns without updating
 * @note Uses global masterListRepository (stub implementation)
 */
void updateMasterListLdapStatus(
    const std::string& mlId,
    const std::string& ldapDn
);

// saveCertificateToLdap() moved to services::LdapStorageService (v2.13.0)

/**
 * @brief Convert X509_NAME to RFC 2253 string
 */
std::string x509NameToString(X509_NAME* name);

/**
 * @brief Convert ASN1_INTEGER to hex string
 */
std::string asn1IntegerToHex(const ASN1_INTEGER* asn1Int);

/**
 * @brief Convert ASN1_TIME to ISO 8601 string
 */
std::string asn1TimeToIso8601(const ASN1_TIME* asn1Time);

/**
 * @brief Sanitize filename to prevent path traversal attacks
 *
 * @param filename Original filename from upload
 * @return Sanitized filename (alphanumeric, dash, underscore, dot only)
 *
 * @throws std::runtime_error if filename contains '..' or is empty after sanitization
 */
std::string sanitizeFilename(const std::string& filename);

/**
 * @brief Validate LDIF file format
 *
 * @param content File content as string
 * @return true if valid LDIF format
 */
bool isValidLdifFile(const std::string& content);

/**
 * @brief Validate PKCS#7 (Master List) file format
 *
 * @param content File content as binary vector
 * @return true if valid PKCS#7 DER format
 */
bool isValidP7sFile(const std::vector<uint8_t>& content);

/**
 * @brief Generate UUID v4
 *
 * @return std::string UUID v4 string (e.g., "550e8400-e29b-41d4-a716-446655440000")
 */
std::string generateUuid();

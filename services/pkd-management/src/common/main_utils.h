/**
 * @file main_utils.h
 * @brief External utility function declarations from main.cpp
 *
 * This header provides declarations for utility functions defined in main.cpp
 * that are needed by other modules (e.g., masterlist_processor).
 *
 * Security Note: All functions handle sensitive data (certificates, LDAP credentials).
 * Ensure proper input validation and error handling when using these functions.
 *
 * @version 2.0.0
 * @date 2026-01-23
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <libpq-fe.h>
#include <ldap.h>

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
 * @brief Save Master List metadata to PostgreSQL database
 *
 * Inserts Master List record into the master_list table.
 *
 * @param conn PostgreSQL connection (must be valid)
 * @param uploadId Upload UUID
 * @param countryCode ISO 3166-1 country code
 * @param signerDn Signer certificate Subject DN
 * @param fingerprint SHA-256 fingerprint of Master List CMS
 * @param cscaCount Number of CSCAs in the Master List
 * @param mlData Master List binary data (CMS/PKCS#7)
 * @return std::string Master List ID (UUID), or empty on failure
 *
 * @warning mlData should be valid CMS/PKCS#7 structure
 * @note NOT thread-safe (requires exclusive PostgreSQL connection)
 */
std::string saveMasterList(
    PGconn* conn,
    const std::string& uploadId,
    const std::string& countryCode,
    const std::string& signerDn,
    const std::string& fingerprint,
    int cscaCount,
    const std::vector<uint8_t>& mlData
);

/**
 * @brief Save Master List to LDAP (o=ml branch)
 *
 * Stores Master List CMS binary in LDAP for backup purposes.
 * DN format: cn={fingerprint},o=ml,c={country},dc=data,dc=download,dc=pkd,...
 *
 * @param ld LDAP connection (must be authenticated and connected)
 * @param countryCode ISO 3166-1 country code
 * @param signerDn Signer certificate Subject DN
 * @param fingerprint SHA-256 fingerprint of Master List CMS
 * @param mlBinary Master List binary data (CMS/PKCS#7)
 * @return std::string LDAP DN of created entry, or empty on failure
 *
 * @warning Requires write permission to LDAP
 * @note NOT thread-safe (requires exclusive LDAP connection)
 * @note Uses userCertificate;binary attribute for storage
 */
std::string saveMasterListToLdap(
    LDAP* ld,
    const std::string& countryCode,
    const std::string& signerDn,
    const std::string& fingerprint,
    const std::vector<uint8_t>& mlBinary
);

/**
 * @brief Update Master List LDAP storage status in database
 *
 * Updates master_list.ldap_dn and ldap_stored_at fields.
 *
 * @param conn PostgreSQL connection (must be valid)
 * @param mlId Master List UUID
 * @param ldapDn LDAP DN where Master List is stored
 *
 * @note If ldapDn is empty, function returns without updating
 * @note NOT thread-safe (requires exclusive PostgreSQL connection)
 */
void updateMasterListLdapStatus(
    PGconn* conn,
    const std::string& mlId,
    const std::string& ldapDn
);

/**
 * @brief Save certificate to LDAP (o=csca, o=dsc, o=lc, or o=crl branch)
 *
 * Stores X.509 certificate in LDAP for primary storage.
 * DN format: cn={fingerprint},o={type},c={country},dc=data,dc=download,dc=pkd,...
 *
 * @param ld LDAP connection (must be authenticated and connected)
 * @param certType Certificate type (CSCA, DSC, DSC_NC, LC)
 * @param countryCode ISO 3166-1 country code
 * @param subjectDn X.509 Subject DN
 * @param issuerDn X.509 Issuer DN
 * @param serialNumber Certificate serial number (hex)
 * @param fingerprint SHA-256 fingerprint (hex, used in DN)
 * @param certBinary Certificate DER binary data
 * @param pkdConformanceCode PKD conformance code (optional, default empty)
 * @param pkdConformanceText PKD conformance text (optional, default empty)
 * @param pkdVersion PKD version (optional, default empty)
 * @return std::string LDAP DN of created entry, or empty on failure
 *
 * @warning Requires write permission to LDAP
 * @warning certBinary must be valid X.509 DER format
 * @note NOT thread-safe (requires exclusive LDAP connection)
 * @note Uses userCertificate;binary attribute for storage
 * @note Link Certificates (LC) are Cross-signed CSCAs (subject_dn != issuer_dn) stored in o=lc
 */
std::string saveCertificateToLdap(
    LDAP* ld,
    const std::string& certType,
    const std::string& countryCode,
    const std::string& subjectDn,
    const std::string& issuerDn,
    const std::string& serialNumber,
    const std::string& fingerprint,
    const std::vector<uint8_t>& certBinary,
    const std::string& pkdConformanceCode = "",
    const std::string& pkdConformanceText = "",
    const std::string& pkdVersion = "",
    bool useLegacyDn = true
);

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <libpq-fe.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>

namespace certificate_utils {

// =============================================================================
// X.509 Certificate Parsing Utilities
// =============================================================================

/**
 * @brief Convert X509_NAME to string (RFC 2253 format)
 * @param name X509_NAME structure
 * @return std::string DN in RFC 2253 format (e.g., "CN=Test,O=Org,C=US")
 */
std::string x509NameToString(X509_NAME* name);

/**
 * @brief Convert ASN1_INTEGER to hexadecimal string
 * @param asn1Int ASN1_INTEGER (e.g., serial number)
 * @return std::string Hex string (e.g., "0A1B2C3D")
 */
std::string asn1IntegerToHex(const ASN1_INTEGER* asn1Int);

/**
 * @brief Convert ASN1_TIME to ISO 8601 string
 * @param asn1Time ASN1_TIME (notBefore or notAfter)
 * @return std::string ISO 8601 format (e.g., "2024-01-30T12:34:56")
 */
std::string asn1TimeToIso8601(const ASN1_TIME* asn1Time);

/**
 * @brief Extract country code from DN string
 * @param dn Distinguished Name (any format)
 * @return std::string Country code (e.g., "KR", "US") or empty if not found
 *
 * Supports both OpenSSL slash format (/C=KR/O=...) and RFC2253 comma format (C=KR,O=...)
 */
std::string extractCountryCode(const std::string& dn);

/**
 * @brief Compute SHA-256 fingerprint of X.509 certificate
 * @param cert X509 certificate
 * @return std::string Hex-encoded SHA-256 fingerprint (64 chars)
 */
std::string computeSha256Fingerprint(X509* cert);

/**
 * @brief Compute SHA-1 fingerprint of X.509 certificate
 * @param cert X509 certificate
 * @return std::string Hex-encoded SHA-1 fingerprint (40 chars)
 */
std::string computeSha1Fingerprint(X509* cert);

/**
 * @brief Check if certificate is expired
 * @param cert X509 certificate
 * @return bool True if expired, false otherwise
 */
bool isExpired(X509* cert);

/**
 * @brief Check if certificate is a link certificate
 * @param cert X509 certificate
 * @return bool True if link certificate (CA=true && not self-signed)
 */
bool isLinkCertificate(X509* cert);

/**
 * @brief Extract ASN.1 structure as human-readable text (from X509 object)
 * @param cert X509 certificate
 * @return std::string X.509 structure in OpenSSL text format
 *
 * Provides detailed ASN.1 structure for debugging and analysis.
 * Output includes all certificate fields, extensions, and their values.
 */
std::string extractAsn1Text(X509* cert);

/**
 * @brief Extract ASN.1 structure from PEM format file
 * @param pemData PEM file content (ASCII text with BEGIN/END markers)
 * @return std::string ASN.1 structure in human-readable format
 *
 * Supports PEM-encoded certificates with headers like:
 * -----BEGIN CERTIFICATE-----
 * Base64-encoded data
 * -----END CERTIFICATE-----
 */
std::string extractAsn1TextFromPem(const std::vector<uint8_t>& pemData);

/**
 * @brief Extract ASN.1 structure from DER/CER/BIN format file
 * @param derData DER/CER/BIN file content (binary format)
 * @return std::string ASN.1 structure in human-readable format
 *
 * Supports binary-encoded X.509 certificates (DER, CER, BIN formats).
 * All are DER encoding with different file extensions.
 */
std::string extractAsn1TextFromDer(const std::vector<uint8_t>& derData);

/**
 * @brief Extract ASN.1 structure from CMS SignedData (Master List)
 * @param cmsData CMS/PKCS#7 SignedData content
 * @return std::string CMS structure including signer info and certificates
 *
 * Extracts complete CMS SignedData structure including:
 * - Content type (Master List)
 * - Signer information (MLSC)
 * - Certificate chain (CSCA certificates)
 * - Signature algorithm
 * - Message digest
 *
 * Used for ICAO PKD Master List files distributed by issuing states.
 */
std::string extractCmsAsn1Text(const std::vector<uint8_t>& cmsData);

/**
 * @brief Auto-detect format and extract ASN.1 structure
 * @param fileData File content (any supported format)
 * @return std::string ASN.1 structure in human-readable format
 *
 * Automatically detects file format (PEM, DER, CMS) and extracts ASN.1 structure.
 * Detection order:
 * 1. Check for PEM markers (-----BEGIN CERTIFICATE-----)
 * 2. Try parsing as CMS SignedData
 * 3. Try parsing as DER-encoded X.509 certificate
 * 4. Return error if all formats fail
 */
std::string extractAsn1TextAuto(const std::vector<uint8_t>& fileData);

// =============================================================================
// Database Certificate Management
// =============================================================================

/**
 * @brief Save certificate with duplicate detection
 *
 * Checks if a certificate already exists based on (certificate_type, fingerprint_sha256).
 * If exists, returns existing ID and isDuplicate=true.
 * If not exists, inserts new certificate and returns new ID and isDuplicate=false.
 *
 * @param conn PostgreSQL connection
 * @param uploadId Current upload UUID
 * @param certType Certificate type (CSCA, DSC, DSC_NC)
 * @param countryCode ISO 3166-1 alpha-2 country code
 * @param subjectDn X.509 Subject DN
 * @param issuerDn X.509 Issuer DN
 * @param serialNumber Certificate serial number (hex)
 * @param fingerprint SHA-256 fingerprint (hex)
 * @param notBefore Validity start date (YYYY-MM-DD HH:MM:SS)
 * @param notAfter Validity end date (YYYY-MM-DD HH:MM:SS)
 * @param certData Certificate DER bytes
 * @param validationStatus Validation status (UNKNOWN, VALID, INVALID)
 * @param validationMessage Validation message
 * @return std::pair<std::string, bool> (certificate_id UUID, isDuplicate)
 *
 * @note This function uses parameterized queries to prevent SQL injection
 * @note Returns empty string ("") on error
 */
std::pair<std::string, bool> saveCertificateWithDuplicateCheck(
    PGconn* conn,
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
 * Records the source of a certificate in the certificate_duplicates table.
 * This allows tracking all sources (ML_FILE, LDIF_001, LDIF_002, LDIF_003)
 * that contain the same certificate.
 *
 * @param conn PostgreSQL connection
 * @param certificateId Certificate UUID from certificate table
 * @param uploadId Upload UUID that contains this certificate
 * @param sourceType Source type (ML_FILE, LDIF_001, LDIF_002, LDIF_003)
 * @param sourceCountry Country code from source (optional)
 * @param sourceEntryDn LDIF entry DN (optional, for LDIF sources)
 * @param sourceFileName Original filename (optional)
 * @return bool Success status
 */
bool trackCertificateDuplicate(
    PGconn* conn,
    const std::string& certificateId,
    const std::string& uploadId,
    const std::string& sourceType,
    const std::string& sourceCountry = "",
    const std::string& sourceEntryDn = "",
    const std::string& sourceFileName = ""
);

/**
 * @brief Increment duplicate count for a certificate
 *
 * Updates the duplicate_count, last_seen_upload_id, and last_seen_at
 * fields when a duplicate certificate is detected.
 *
 * @param conn PostgreSQL connection
 * @param certificateId Certificate UUID to update
 * @param uploadId Current upload UUID
 * @return bool Success status
 */
bool incrementDuplicateCount(
    PGconn* conn,
    const std::string& certificateId,
    const std::string& uploadId
);

/**
 * @brief Update upload file statistics for CSCA extraction
 *
 * Updates csca_extracted_from_ml and csca_duplicates counters
 * for Collection 002 LDIF processing.
 *
 * @param conn PostgreSQL connection
 * @param uploadId Upload UUID
 * @param extractedCount Number of CSCAs extracted from Master Lists
 * @param duplicateCount Number of duplicates detected
 * @return bool Success status
 */
bool updateCscaExtractionStats(
    PGconn* conn,
    const std::string& uploadId,
    int extractedCount,
    int duplicateCount
);

/**
 * @brief Update certificate LDAP storage status
 *
 * Marks a certificate as stored in LDAP with the LDAP DN.
 *
 * @param conn PostgreSQL connection
 * @param certificateId Certificate UUID
 * @param ldapDn LDAP DN where certificate is stored
 * @return bool Success status
 */
bool updateCertificateLdapStatus(
    PGconn* conn,
    const std::string& certificateId,
    const std::string& ldapDn
);

/**
 * @brief Get source type string for logging
 *
 * Converts file format to source type identifier.
 *
 * @param fileFormat File format (LDIF_001, LDIF_002, LDIF_003, MASTERLIST)
 * @return std::string Source type (LDIF_001, LDIF_002, LDIF_003, ML_FILE)
 */
std::string getSourceType(const std::string& fileFormat);

} // namespace certificate_utils

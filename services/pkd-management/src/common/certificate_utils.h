#pragma once

#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <libpq-fe.h>

namespace certificate_utils {

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
 * @return std::pair<int, bool> (certificate_id, isDuplicate)
 *
 * @note This function uses parameterized queries to prevent SQL injection
 */
std::pair<int, bool> saveCertificateWithDuplicateCheck(
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
 * @param certificateId Certificate ID from certificate table
 * @param uploadId Upload UUID that contains this certificate
 * @param sourceType Source type (ML_FILE, LDIF_001, LDIF_002, LDIF_003)
 * @param sourceCountry Country code from source (optional)
 * @param sourceEntryDn LDIF entry DN (optional, for LDIF sources)
 * @param sourceFileName Original filename (optional)
 * @return bool Success status
 */
bool trackCertificateDuplicate(
    PGconn* conn,
    int certificateId,
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
 * @param certificateId Certificate ID to update
 * @param uploadId Current upload UUID
 * @return bool Success status
 */
bool incrementDuplicateCount(
    PGconn* conn,
    int certificateId,
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
 * @brief Get source type string for logging
 *
 * Converts file format to source type identifier.
 *
 * @param fileFormat File format (LDIF_001, LDIF_002, LDIF_003, MASTERLIST)
 * @return std::string Source type (LDIF_001, LDIF_002, LDIF_003, ML_FILE)
 */
std::string getSourceType(const std::string& fileFormat);

} // namespace certificate_utils

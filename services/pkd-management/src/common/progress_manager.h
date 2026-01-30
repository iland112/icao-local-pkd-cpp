#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <functional>
#include <optional>
#include <chrono>
#include <json/json.h>

// Forward declaration for X509 certificate (OpenSSL)
typedef struct x509_st X509;

/**
 * @file progress_manager.h
 * @brief Enhanced Progress Manager - Thread-safe progress tracking with X.509 metadata and ICAO 9303 compliance
 *
 * Extracted from main.cpp as part of Phase 4.4 Task 1.3 refactoring.
 * Enhanced with detailed certificate metadata tracking and ICAO 9303 compliance monitoring.
 *
 * @version 2.2.0-dev (Phase 4.4)
 * @date 2026-01-30
 */

namespace common {

// =============================================================================
// Processing Stage Enumeration (Enhanced)
// =============================================================================

/**
 * @brief Processing stage enumeration
 *
 * Represents the various stages of file upload and processing workflow.
 * Enhanced with granular validation stages for better progress tracking.
 */
enum class ProcessingStage {
    // Upload stages
    UPLOAD_COMPLETED,

    // Parsing stages
    PARSING_STARTED,
    PARSING_IN_PROGRESS,
    PARSING_COMPLETED,

    // Validation stages (Enhanced - Phase 4.4)
    VALIDATION_STARTED,
    VALIDATION_EXTRACTING_METADATA,    // Extracting X.509 certificate metadata
    VALIDATION_VERIFYING_SIGNATURE,    // Verifying certificate signature
    VALIDATION_CHECKING_TRUST_CHAIN,   // Building and validating trust chain
    VALIDATION_CHECKING_CRL,           // Checking certificate revocation status
    VALIDATION_CHECKING_ICAO_COMPLIANCE, // Checking ICAO 9303 compliance
    VALIDATION_IN_PROGRESS,            // General validation progress
    VALIDATION_COMPLETED,

    // Database saving stages
    DB_SAVING_STARTED,
    DB_SAVING_IN_PROGRESS,
    DB_SAVING_COMPLETED,

    // LDAP saving stages
    LDAP_SAVING_STARTED,
    LDAP_SAVING_IN_PROGRESS,
    LDAP_SAVING_COMPLETED,

    // Final stages
    COMPLETED,
    FAILED
};

/**
 * @brief Convert processing stage to string (English)
 */
std::string stageToString(ProcessingStage stage);

/**
 * @brief Convert processing stage to Korean description
 */
std::string stageToKorean(ProcessingStage stage);

/**
 * @brief Convert processing stage to base percentage
 */
int stageToBasePercentage(ProcessingStage stage);

// =============================================================================
// Certificate Metadata (Phase 4.4 Enhancement)
// =============================================================================

/**
 * @brief X.509 Certificate metadata for progress tracking
 *
 * Contains detailed information about the certificate being processed.
 * Used for real-time display in frontend and troubleshooting.
 */
struct CertificateMetadata {
    // Identity
    std::string subjectDn;
    std::string issuerDn;
    std::string serialNumber;
    std::string countryCode;

    // Certificate type
    std::string certificateType;  // CSCA, DSC, DSC_NC, MLSC
    bool isSelfSigned;
    bool isLinkCertificate;

    // Cryptographic details
    std::string signatureAlgorithm;     // e.g., "SHA256withRSA"
    std::string publicKeyAlgorithm;     // e.g., "RSA", "ECDSA"
    int keySize;                        // e.g., 2048, 4096

    // X.509 Extensions
    bool isCa;
    std::optional<int> pathLengthConstraint;
    std::vector<std::string> keyUsage;           // e.g., ["digitalSignature", "keyCertSign"]
    std::vector<std::string> extendedKeyUsage;   // e.g., ["1.3.6.1.5.5.7.3.2"]

    // Validity period
    std::string notBefore;
    std::string notAfter;
    bool isExpired;

    // Fingerprints
    std::string fingerprintSha256;
    std::string fingerprintSha1;

    // ASN.1 Structure (optional - for advanced debugging/analysis)
    std::optional<std::string> asn1Text;  // Human-readable ASN.1 structure

    /**
     * @brief Convert to JSON for SSE streaming
     */
    Json::Value toJson() const;
};

// =============================================================================
// ICAO 9303 Compliance Status (Phase 4.4 Enhancement)
// =============================================================================

/**
 * @brief ICAO 9303 compliance check result
 *
 * Tracks compliance with ICAO 9303 PKI specifications.
 */
struct IcaoComplianceStatus {
    bool isCompliant;                    // Overall compliance status
    std::string complianceLevel;         // CONFORMANT, NON_CONFORMANT, WARNING
    std::vector<std::string> violations; // List of violations (if any)
    std::optional<std::string> pkdConformanceCode;  // e.g., "ERR:CSCA.CDP.14"
    std::optional<std::string> pkdConformanceText;  // Detailed error description
    std::optional<std::string> pkdVersion;          // PKD version number

    // Specific compliance checks
    bool keyUsageCompliant;              // Key usage flags correct for cert type
    bool algorithmCompliant;             // Approved signature algorithm
    bool keySizeCompliant;               // Minimum key size met
    bool validityPeriodCompliant;        // Validity period within limits
    bool dnFormatCompliant;              // DN format complies with ICAO
    bool extensionsCompliant;            // Required extensions present

    /**
     * @brief Convert to JSON for SSE streaming
     */
    Json::Value toJson() const;
};

// =============================================================================
// Validation Statistics (Phase 4.4 Enhancement)
// =============================================================================

/**
 * @brief Real-time validation statistics
 *
 * Provides aggregated statistics during batch certificate validation.
 * Updated incrementally for live dashboard display.
 */
struct ValidationStatistics {
    // Overall counts
    int totalCertificates;
    int processedCount;
    int validCount;
    int invalidCount;
    int pendingCount;

    // Trust chain results
    int trustChainValidCount;
    int trustChainInvalidCount;
    int cscaNotFoundCount;

    // Expiration status
    int expiredCount;
    int notYetValidCount;
    int validPeriodCount;

    // CRL status
    int revokedCount;
    int notRevokedCount;
    int crlNotCheckedCount;

    // Signature algorithm distribution
    std::map<std::string, int> signatureAlgorithms;  // "SHA256withRSA" -> count

    // Key size distribution
    std::map<int, int> keySizes;  // 2048 -> count, 4096 -> count

    // Certificate type distribution
    std::map<std::string, int> certificateTypes;  // "DSC" -> count, "CSCA" -> count

    // ICAO compliance summary
    int icaoCompliantCount;
    int icaoNonCompliantCount;
    int icaoWarningCount;
    std::map<std::string, int> complianceViolations;  // violation type -> count

    /**
     * @brief Convert to JSON for SSE streaming
     */
    Json::Value toJson() const;
};

// =============================================================================
// Processing Progress (Enhanced)
// =============================================================================

/**
 * @brief Enhanced processing progress data structure
 *
 * Contains comprehensive information about the current processing status,
 * including certificate metadata, ICAO compliance, and real-time statistics.
 */
struct ProcessingProgress {
    // Basic progress
    std::string uploadId;
    ProcessingStage stage;
    int percentage;
    int processedCount;
    int totalCount;
    std::string message;
    std::string errorMessage;
    std::string details;
    std::chrono::system_clock::time_point updatedAt;

    // Enhanced fields (Phase 4.4)
    std::optional<CertificateMetadata> currentCertificate;  // Currently processing certificate
    std::optional<IcaoComplianceStatus> currentCompliance;  // Current cert compliance status
    std::optional<ValidationStatistics> statistics;         // Aggregated statistics

    /**
     * @brief Convert progress to JSON string
     * @return JSON string representation (single-line for SSE compatibility)
     */
    std::string toJson() const;

    /**
     * @brief Create a basic ProcessingProgress instance
     *
     * @param uploadId Upload UUID
     * @param stage Current processing stage
     * @param processedCount Number of items processed
     * @param totalCount Total number of items
     * @param message User-facing message
     * @param errorMessage Error message (if any)
     * @param details Additional details
     * @return ProcessingProgress instance with calculated percentage
     */
    static ProcessingProgress create(
        const std::string& uploadId,
        ProcessingStage stage,
        int processedCount,
        int totalCount,
        const std::string& message,
        const std::string& errorMessage = "",
        const std::string& details = ""
    );

    /**
     * @brief Create an enhanced ProcessingProgress with certificate metadata
     *
     * @param uploadId Upload UUID
     * @param stage Current processing stage
     * @param processedCount Number of items processed
     * @param totalCount Total number of items
     * @param message User-facing message
     * @param certMetadata Current certificate metadata
     * @param compliance ICAO compliance status (optional)
     * @param stats Validation statistics (optional)
     * @return ProcessingProgress instance with all metadata
     */
    static ProcessingProgress createWithMetadata(
        const std::string& uploadId,
        ProcessingStage stage,
        int processedCount,
        int totalCount,
        const std::string& message,
        const CertificateMetadata& certMetadata,
        const std::optional<IcaoComplianceStatus>& compliance = std::nullopt,
        const std::optional<ValidationStatistics>& stats = std::nullopt
    );
};

// =============================================================================
// Progress Manager (Thread-safe Singleton)
// =============================================================================

/**
 * @brief Enhanced SSE Progress Manager
 *
 * Singleton class that manages progress updates for multiple concurrent file uploads.
 * Enhanced with certificate metadata tracking and ICAO compliance monitoring.
 *
 * Thread-safe for use in async processing contexts.
 */
class ProgressManager {
private:
    std::mutex mutex_;
    std::map<std::string, ProcessingProgress> progressCache_;
    std::map<std::string, std::function<void(const std::string&)>> sseCallbacks_;

    // Private constructor for singleton
    ProgressManager() = default;
    ProgressManager(const ProgressManager&) = delete;
    ProgressManager& operator=(const ProgressManager&) = delete;

public:
    /**
     * @brief Get singleton instance
     * @return Reference to the global ProgressManager instance
     */
    static ProgressManager& getInstance();

    /**
     * @brief Send progress update
     *
     * Updates the progress cache and notifies registered SSE callbacks.
     * Thread-safe.
     *
     * @param progress ProcessingProgress instance
     */
    void sendProgress(const ProcessingProgress& progress);

    /**
     * @brief Register SSE callback for progress updates
     *
     * Registers a callback function that will be called whenever progress
     * is updated for the specified uploadId.
     *
     * @param uploadId Upload UUID
     * @param callback Function to call with SSE-formatted data
     */
    void registerSseCallback(const std::string& uploadId, std::function<void(const std::string&)> callback);

    /**
     * @brief Unregister SSE callback
     *
     * @param uploadId Upload UUID
     */
    void unregisterSseCallback(const std::string& uploadId);

    /**
     * @brief Get current progress for an upload
     *
     * @param uploadId Upload UUID
     * @return Optional ProcessingProgress (nullopt if not found)
     */
    std::optional<ProcessingProgress> getProgress(const std::string& uploadId);

    /**
     * @brief Clear progress data for an upload
     *
     * Removes both cached progress and SSE callback.
     *
     * @param uploadId Upload UUID
     */
    void clearProgress(const std::string& uploadId);
};

// =============================================================================
// ICAO 9303 Compliance Checker
// =============================================================================

/**
 * @brief Check certificate compliance with ICAO 9303 Part 12 specifications
 *
 * @param cert X509 certificate to validate
 * @param certType Certificate type: "CSCA", "DSC", "DSC_NC", "MLSC"
 * @return IcaoComplianceStatus Detailed compliance check results
 *
 * Validates:
 * - Key Usage (per certificate type)
 * - Signature Algorithm (approved algorithms)
 * - Key Size (minimum requirements)
 * - Validity Period (recommended durations)
 * - DN Format (ICAO standard)
 * - Required Extensions (Basic Constraints, Key Usage)
 *
 * ICAO 9303 Part 12 Requirements:
 * - CSCA: keyCertSign + cRLSign, CA=TRUE, max 15 years
 * - DSC: digitalSignature, CA=FALSE, max 3 years
 * - MLSC: keyCertSign, CA=TRUE, self-signed
 * - Algorithms: SHA-256/384/512 with RSA or ECDSA
 * - Key Size: RSA 2048-4096 bits, ECDSA P-256/384/521
 */
IcaoComplianceStatus checkIcaoCompliance(X509* cert, const std::string& certType);

// =============================================================================
// Certificate Metadata Extraction for Progress Tracking
// =============================================================================

/**
 * @brief Extract complete certificate metadata for progress tracking
 *
 * Combines all helper functions from certificate_utils and x509_metadata_extractor
 * to populate a comprehensive CertificateMetadata structure for real-time SSE streaming.
 *
 * @param cert X509 certificate to extract metadata from
 * @param includeAsn1Text Whether to include ASN.1 structure text (optional, for detailed view)
 * @return CertificateMetadata Complete metadata structure
 *
 * Extracted Fields:
 * - Identity: subjectDn, issuerDn, serialNumber, countryCode
 * - Type: certificateType, isSelfSigned, isLinkCertificate
 * - Cryptography: signatureAlgorithm, publicKeyAlgorithm, keySize
 * - Extensions: isCa, pathLengthConstraint, keyUsage, extendedKeyUsage
 * - Validity: notBefore, notAfter, isExpired
 * - Fingerprints: fingerprintSha256, fingerprintSha1
 * - Optional: asn1Text (for detailed ASN.1 structure view)
 *
 * Usage in Validation Flow:
 * ```cpp
 * auto metadata = common::extractCertificateMetadataForProgress(cert, false);
 * auto progress = ProcessingProgress::createWithMetadata(
 *     uploadId, ProcessingStage::VALIDATION_EXTRACTING_METADATA,
 *     processedCount, totalCount, "Extracting metadata...", metadata
 * );
 * ProgressManager::getInstance().sendProgress(progress);
 * ```
 */
CertificateMetadata extractCertificateMetadataForProgress(
    X509* cert,
    bool includeAsn1Text = false
);

} // namespace common

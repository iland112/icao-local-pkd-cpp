#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"

/**
 * @file upload_repository.h
 * @brief Upload Repository - Database Access Layer for uploaded_file table
 *
 * Handles all database operations related to file uploads.
 * Provides CRUD operations and business-specific queries.
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @note Part of Oracle migration Phase 3: Query Executor Pattern
 * @date 2026-02-04
 */

namespace repositories {

/**
 * @brief Upload entity (Domain Model)
 *
 * Represents a record in the uploaded_file table.
 */
struct Upload {
    std::string id;                  // UUID
    std::string fileName;
    std::string fileHash;            // SHA-256 hash for deduplication
    std::string fileFormat;          // "LDIF", "MASTER_LIST"
    int fileSize;
    std::string status;              // "PENDING", "PARSING", "PARSED", "VALIDATING", "COMPLETED", "FAILED"
    std::string uploadedBy;
    std::optional<std::string> errorMessage;
    std::optional<std::string> processingMode;  // "AUTO", "MANUAL"

    // Processing progress
    int totalEntries = 0;
    int processedEntries = 0;

    // Certificate counts
    int cscaCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int mlscCount = 0;
    int mlCount = 0;

    // Validation statistics (Trust Chain validation results)
    int validationValidCount = 0;
    int validationInvalidCount = 0;
    int validationPendingCount = 0;
    int validationErrorCount = 0;
    int trustChainValidCount = 0;
    int trustChainInvalidCount = 0;
    int cscaNotFoundCount = 0;
    int expiredCount = 0;
    int revokedCount = 0;

    std::string createdAt;
    std::string updatedAt;
};

/**
 * @brief Repository for uploaded_file table
 *
 * Handles database operations for file upload tracking.
 * Database-agnostic design for future Oracle migration.
 */
class UploadRepository {
public:
    /**
     * @brief Constructor
     * @param queryExecutor Query executor (PostgreSQL or Oracle, non-owning pointer)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit UploadRepository(common::IQueryExecutor* queryExecutor);

    ~UploadRepository() = default;

    // ========================================================================
    // CRUD Operations
    // ========================================================================

    /**
     * @brief Insert a new upload record
     * @param upload Upload entity
     * @return true if inserted successfully
     */
    bool insert(const Upload& upload);

    /**
     * @brief Find upload by ID
     * @param uploadId Upload UUID
     * @return Upload entity if found, nullopt otherwise
     */
    std::optional<Upload> findById(const std::string& uploadId);

    /**
     * @brief Find all uploads with pagination
     * @param limit Maximum number of records
     * @param offset Offset for pagination
     * @param sortBy Column to sort by (default: "created_at")
     * @param direction Sort direction (default: "DESC")
     * @return Vector of Upload entities
     */
    std::vector<Upload> findAll(
        int limit,
        int offset,
        const std::string& sortBy = "created_at",
        const std::string& direction = "DESC"
    );

    /**
     * @brief Update upload status
     * @param uploadId Upload UUID
     * @param status New status
     * @param errorMessage Optional error message
     * @return true if updated successfully
     */
    bool updateStatus(
        const std::string& uploadId,
        const std::string& status,
        const std::string& errorMessage = ""
    );

    /**
     * @brief Update upload statistics (certificate counts)
     * @param uploadId Upload UUID
     * @param cscaCount CSCA count
     * @param dscCount DSC count
     * @param dscNcCount DSC_NC count
     * @param crlCount CRL count
     * @param mlscCount MLSC count
     * @param mlCount Master List count
     * @return true if updated successfully
     */
    bool updateStatistics(
        const std::string& uploadId,
        int cscaCount,
        int dscCount,
        int dscNcCount,
        int crlCount,
        int mlscCount = 0,
        int mlCount = 0
    );

    /**
     * @brief Update upload progress (total_entries, processed_entries)
     * @param uploadId Upload UUID
     * @param totalEntries Total entries to process
     * @param processedEntries Entries processed so far
     * @return true if updated successfully
     */
    bool updateProgress(
        const std::string& uploadId,
        int totalEntries,
        int processedEntries
    );

    /**
     * @brief Delete upload by ID
     * @param uploadId Upload UUID
     * @return true if deleted successfully
     */
    bool deleteById(const std::string& uploadId);

    /**
     * @brief Update file hash for an upload
     * @param uploadId Upload UUID
     * @param fileHash SHA-256 file hash
     * @return true if updated successfully
     */
    bool updateFileHash(const std::string& uploadId, const std::string& fileHash);

    // ========================================================================
    // Business-Specific Queries
    // ========================================================================

    /**
     * @brief Find duplicate upload by file hash
     * @param fileHash SHA-256 file hash
     * @return Upload entity if duplicate found, nullopt otherwise
     */
    std::optional<Upload> findByFileHash(const std::string& fileHash);

    /**
     * @brief Count uploads by status
     * @param status Status to filter ("COMPLETED", "FAILED", etc.)
     * @return Count
     */
    int countByStatus(const std::string& status);

    /**
     * @brief Count total uploads
     * @return Total count
     */
    int countAll();

    /**
     * @brief Find recent uploads (last N hours)
     * @param hours Number of hours to look back
     * @return Vector of Upload entities
     */
    std::vector<Upload> findRecentUploads(int hours);

    /**
     * @brief Get upload statistics summary
     * @return JSON object with statistics
     *
     * Response format:
     * {
     *   "totalUploads": 100,
     *   "byStatus": {
     *     "COMPLETED": 80,
     *     "FAILED": 15,
     *     "PENDING": 5
     *   },
     *   "byFormat": {
     *     "LDIF": 70,
     *     "MASTER_LIST": 30
     *   },
     *   "totalCertificates": 50000,
     *   "totalCrls": 200
     * }
     */
    Json::Value getStatisticsSummary();

    /**
     * @brief Get country statistics
     * @param limit Maximum number of countries (0 = all, default 20)
     * @return JSON object with country → certificate count mapping
     */
    Json::Value getCountryStatistics(int limit = 20);

    /**
     * @brief Get detailed country statistics
     * @param limit Maximum number of countries (0 = all)
     * @return JSON array with detailed breakdown per country
     *
     * Response format:
     * [
     *   {
     *     "countryCode": "KR",
     *     "mlscCount": 1,
     *     "cscaSelfSignedCount": 5,
     *     "cscaLinkCertCount": 2,
     *     "dscCount": 1400,
     *     "dscNcCount": 10,
     *     "crlCount": 5,
     *     "totalCertificates": 1423
     *   },
     *   ...
     * ]
     */
    Json::Value getDetailedCountryStatistics(int limit = 0);

    /**
     * @brief Get duplicate certificates for an upload
     * @param uploadId Upload UUID
     * @return JSON object with duplicate certificate information
     *
     * Response format:
     * {
     *   "success": true,
     *   "uploadId": "...",
     *   "totalDuplicates": 100,
     *   "byType": {
     *     "CSCA": 10,
     *     "DSC": 80,
     *     "DSC_NC": 5,
     *     "MLSC": 3,
     *     "CRL": 2
     *   },
     *   "duplicates": [
     *     {
     *       "id": 123,
     *       "sourceType": "...",
     *       "sourceCountry": "KR",
     *       "detectedAt": "2026-01-30...",
     *       "certificateType": "DSC",
     *       "country": "KR",
     *       "subjectDn": "...",
     *       "fingerprint": "..."
     *     },
     *     ...
     *   ]
     * }
     */
    Json::Value findDuplicatesByUploadId(const std::string& uploadId);

private:
    common::IQueryExecutor* queryExecutor_;  // Query executor (non-owning)

    /**
     * @brief Convert JSON object to Upload entity
     * @param json JSON object from query result
     * @return Upload entity
     */
    Upload jsonToUpload(const Json::Value& json);

    /**
     * @brief Get optional string from JSON value
     */
    std::optional<std::string> getOptionalString(const Json::Value& json, const std::string& field);

    /**
     * @brief Get optional int from JSON value
     */
    std::optional<int> getOptionalInt(const Json::Value& json, const std::string& field);

    /**
     * @brief Comprehensive integer parsing (handles PostgreSQL int and Oracle string)
     * @param json JSON object
     * @param field Field name
     * @param defaultValue Default value if missing/null
     * @return Parsed integer value
     */
    int getInt(const Json::Value& json, const std::string& field, int defaultValue = 0);
};

} // namespace repositories

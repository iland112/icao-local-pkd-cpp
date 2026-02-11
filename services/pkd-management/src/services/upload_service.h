#pragma once

#include <string>
#include <vector>
#include <memory>
#include <ldap.h>
#include <json/json.h>
#include "../processing_strategy.h"
#include "../common.h"
#include "../repositories/upload_repository.h"
#include "../repositories/certificate_repository.h"
#include <ldap_connection_pool.h>  // v2.4.3: LDAP connection pool (NEW)

/**
 * @file upload_service.h
 * @brief Upload Service - File Upload Business Logic Layer
 *
 * Handles LDIF and Master List file upload, parsing, validation, and storage.
 * Following DDD (Domain-Driven Design) and SRP (Single Responsibility Principle).
 *
 * Responsibilities:
 * - LDIF file upload and processing
 * - Master List file upload and processing
 * - Upload history management
 * - Upload validation results
 * - Upload statistics and issues
 *
 * Does NOT handle:
 * - HTTP request/response (Controller's job)
 * - Direct database access (Repository's job - but currently mixed)
 * - Authentication/Authorization (Middleware's job)
 *
 * @note This is Phase 1 of main.cpp refactoring
 * @date 2026-01-29
 */

namespace services {

/**
 * @brief Upload Service Class
 *
 * Encapsulates all business logic related to file uploads.
 * Extracted from main.cpp to improve maintainability and testability.
 */
class UploadService {
public:
    /**
     * @brief Constructor with Dependency Injection
     * @param uploadRepo Upload repository (non-owning pointer)
     * @param certRepo Certificate repository (non-owning pointer)
     * @param ldapPool LDAP connection pool (non-owning pointer, can be nullptr)
     */
    UploadService(
        repositories::UploadRepository* uploadRepo,
        repositories::CertificateRepository* certRepo,
        common::LdapConnectionPool* ldapPool
    );

    /**
     * @brief Destructor
     */
    ~UploadService() = default;

    // ========================================================================
    // LDIF Upload
    // ========================================================================

    /**
     * @brief LDIF Upload Result
     */
    struct LdifUploadResult {
        bool success;
        std::string uploadId;
        std::string message;
        int certificateCount;
        int cscaCount;
        int dscCount;
        int dscNcCount;
        int crlCount;
        std::string status;  // "COMPLETED", "FAILED", "PARSING", etc.
        std::string errorMessage;
    };

    /**
     * @brief Upload LDIF file
     *
     * @param fileName Original file name
     * @param fileContent Raw file content (bytes)
     * @param uploadMode "AUTO" or "MANUAL"
     * @param uploadedBy Username of uploader
     * @return LdifUploadResult Upload result with statistics
     *
     * Business Logic:
     * 1. Generate upload ID (UUID)
     * 2. Create database record (uploaded_file table)
     * 3. Save file to temporary location
     * 4. Create ProcessingStrategy based on mode
     * 5. Process LDIF entries (parse, validate, save to DB & LDAP)
     * 6. Update upload status
     * 7. Return result with statistics
     */
    LdifUploadResult uploadLdif(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent,
        const std::string& uploadMode,
        const std::string& uploadedBy
    );

    /**
     * @brief Process LDIF file asynchronously (Phase 4.4)
     *
     * @param uploadId Upload UUID
     * @param content File content bytes
     *
     * Migrated from main.cpp processLdifFileAsync().
     * Runs in background thread, processes LDIF entries, validates certificates,
     * saves to DB & LDAP, sends progress updates via ProgressManager.
     */
    void processLdifAsync(const std::string& uploadId, const std::vector<uint8_t>& content);

    // ========================================================================
    // Master List Upload
    // ========================================================================

    /**
     * @brief Master List Upload Result
     */
    struct MasterListUploadResult {
        bool success;
        std::string uploadId;
        std::string message;
        int mlscCount;
        int cscaCount;
        int crlCount;
        int mlCount;  // Master List count
        std::string status;
        std::string errorMessage;
    };

    /**
     * @brief Upload Master List file
     *
     * @param fileName Original file name
     * @param fileContent Raw file content (bytes)
     * @param uploadMode "AUTO" or "MANUAL"
     * @param uploadedBy Username of uploader
     * @return MasterListUploadResult Upload result with statistics
     *
     * Business Logic:
     * 1. Generate upload ID
     * 2. Create database record
     * 3. Save file to temporary location
     * 4. Create ProcessingStrategy based on mode
     * 5. Process Master List (parse CMS, extract certificates, validate)
     * 6. Update upload status
     * 7. Return result with statistics
     */
    MasterListUploadResult uploadMasterList(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent,
        const std::string& uploadMode,
        const std::string& uploadedBy
    );

    // Note: Master List async processing is handled by Strategy Pattern thread in the upload handler (main.cpp)
    // processMasterListAsync was removed to prevent dual-processing bug

    // ========================================================================
    // Upload Management (MANUAL mode)
    // ========================================================================

    /**
     * @brief Trigger parsing for MANUAL mode upload
     *
     * @param uploadId Upload UUID
     * @return true if parsing triggered successfully
     *
     * Business Logic:
     * 1. Load upload record from database
     * 2. Verify status is "PENDING"
     * 3. Update status to "PARSING"
     * 4. Trigger parsing logic
     * 5. Update status to "PARSED"
     */
    bool triggerParsing(const std::string& uploadId);

    /**
     * @brief Trigger validation and DB save for MANUAL mode upload
     *
     * @param uploadId Upload UUID
     * @return true if validation triggered successfully
     *
     * Business Logic:
     * 1. Load upload record from database
     * 2. Verify status is "PARSED"
     * 3. Update status to "VALIDATING"
     * 4. Load parsed data from temp file
     * 5. Validate and save to DB & LDAP
     * 6. Update status to "COMPLETED" or "FAILED"
     */
    bool triggerValidation(const std::string& uploadId);

    // ========================================================================
    // Upload History & Detail
    // ========================================================================

    /**
     * @brief Upload History Filter Parameters
     */
    struct UploadHistoryFilter {
        int page = 0;
        int size = 10;
        std::string sort = "created_at";
        std::string direction = "DESC";
    };

    /**
     * @brief Get upload history with pagination
     *
     * @param filter Filter and pagination parameters
     * @return Json::Value Paginated upload history
     *
     * Response format:
     * {
     *   "content": [...],
     *   "totalPages": 10,
     *   "totalElements": 100,
     *   "number": 0,
     *   "size": 10
     * }
     */
    Json::Value getUploadHistory(const UploadHistoryFilter& filter);

    /**
     * @brief Get upload detail by ID
     *
     * @param uploadId Upload UUID
     * @return Json::Value Upload detail including statistics
     *
     * Response includes:
     * - Basic info (fileName, fileFormat, fileSize, status)
     * - Certificate counts (csca, dsc, dscNc, crl, mlsc)
     * - Timestamps (createdAt, updatedAt)
     * - Error message (if failed)
     */
    Json::Value getUploadDetail(const std::string& uploadId);

    // ========================================================================
    // Upload Validations
    // ========================================================================

    /**
     * @brief Validation Filter Parameters
     */
    struct ValidationFilter {
        int limit = 50;
        int offset = 0;
        std::string status;    // "VALID", "INVALID", "PENDING", "ERROR"
        std::string certType;  // "DSC", "DSC_NC"
    };

    /**
     * @brief Get validation results for an upload
     *
     * @param uploadId Upload UUID
     * @param filter Filter and pagination parameters
     * @return Json::Value Paginated validation results
     *
     * Response format:
     * {
     *   "success": true,
     *   "count": 50,
     *   "total": 1000,
     *   "limit": 50,
     *   "offset": 0,
     *   "validations": [...]
     * }
     */
    Json::Value getUploadValidations(
        const std::string& uploadId,
        const ValidationFilter& filter
    );

    // ========================================================================
    // Upload Issues (Duplicates)
    // ========================================================================

    /**
     * @brief Get upload issues (duplicate certificates)
     *
     * @param uploadId Upload UUID
     * @return Json::Value Upload issues grouped by type
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
     *     "CRL": 5
     *   },
     *   "duplicates": [...]
     * }
     */
    Json::Value getUploadIssues(const std::string& uploadId);

    // ========================================================================
    // Upload Deletion
    // ========================================================================

    /**
     * @brief Delete failed or pending upload
     *
     * @param uploadId Upload UUID
     * @return true if deleted successfully
     *
     * Business Logic:
     * 1. Verify upload exists and status is "FAILED" or "PENDING"
     * 2. Delete from uploaded_file table (CASCADE deletes related records)
     * 3. Delete temporary files
     * 4. Return success
     */
    bool deleteUpload(const std::string& uploadId);

    // ========================================================================
    // Statistics
    // ========================================================================

    /**
     * @brief Get upload statistics
     *
     * @return Json::Value Upload statistics
     *
     * Response includes:
     * - Total uploads
     * - Uploads by status (completed, failed, in_progress)
     * - Uploads by format (LDIF, MASTER_LIST)
     * - Recent uploads (last 24 hours)
     */
    Json::Value getUploadStatistics();

    /**
     * @brief Get country statistics
     *
     * @param limit Maximum number of countries (0 = all, default 20)
     * @return Json::Value Country statistics (country → certificate count)
     */
    Json::Value getCountryStatistics(int limit = 20);

    /**
     * @brief Get detailed country statistics
     *
     * @param limit Maximum number of countries to return (0 = all)
     * @return Json::Value Detailed statistics per country
     *
     * Response includes per country:
     * - Country code
     * - MLSC count
     * - CSCA self-signed count
     * - CSCA link cert count
     * - DSC count
     * - DSC_NC count
     * - CRL count
     */
    Json::Value getDetailedCountryStatistics(int limit = 0);

private:
    // Dependencies (non-owning pointers)
    repositories::UploadRepository* uploadRepo_;
    repositories::CertificateRepository* certRepo_;
    common::LdapConnectionPool* ldapPool_;  // v2.4.3: LDAP connection pool (changed from LDAP* ldapConn_)

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * @brief Generate unique upload ID (UUID v4)
     */
    std::string generateUploadId();

    /**
     * @brief Save file to temporary location
     * @return File path
     */
    std::string saveToTempFile(
        const std::string& uploadId,
        const std::vector<uint8_t>& content,
        const std::string& extension
    );

    /**
     * @brief Compute SHA-256 hash of file content
     * @param content File content as bytes
     * @return std::string Hexadecimal hash string
     */
    static std::string computeFileHash(const std::vector<uint8_t>& content);

    /**
     * @brief Get LDAP write connection (wrapper for global function)
     * @return LDAP* LDAP connection or nullptr on failure
     */
    static LDAP* getLdapWriteConnection();

    /**
     * @brief Scrub credentials from error messages
     * @param message Error message that may contain credentials
     * @return std::string Scrubbed message
     */
    static std::string scrubCredentials(const std::string& message);
};

} // namespace services

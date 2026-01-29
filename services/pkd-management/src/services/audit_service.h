#pragma once

#include <string>
#include <vector>
#include <memory>
#include <json/json.h>
#include "../repositories/audit_repository.h"

/**
 * @file audit_service.h
 * @brief Audit Service - Security Audit and Logging Business Logic Layer
 *
 * Handles audit log recording, operation tracking, user activity monitoring,
 * and security event logging.
 * Following DDD (Domain-Driven Design) and SRP (Single Responsibility Principle).
 *
 * Responsibilities:
 * - Audit log recording (operation_audit_log table)
 * - Operation log retrieval with filtering
 * - Operation statistics and analytics
 * - User activity tracking
 * - Security event monitoring
 *
 * Does NOT handle:
 * - HTTP request/response (Controller's job)
 * - Authentication/Authorization (Middleware's job)
 * - Direct database access (Repository's job)
 *
 * @note Part of main.cpp refactoring Phase 1.6
 * @date 2026-01-29
 */

namespace services {

/**
 * @brief Audit Service Class
 *
 * Encapsulates all business logic related to security auditing and logging.
 * Extracted from main.cpp to improve maintainability and testability.
 */
class AuditService {
public:
    /**
     * @brief Constructor with Repository Dependency Injection
     * @param auditRepo Audit repository (non-owning pointer)
     */
    explicit AuditService(repositories::AuditRepository* auditRepo);

    /**
     * @brief Destructor
     */
    ~AuditService() = default;

    // ========================================================================
    // Audit Log Recording
    // ========================================================================

    /**
     * @brief Audit Log Entry
     */
    struct AuditLogEntry {
        std::string operationType;      // "FILE_UPLOAD", "CERTIFICATE_SEARCH", "PA_VERIFY", etc.
        std::string username;            // User who performed the operation
        std::string ipAddress;           // Client IP address
        bool success;                    // Operation result
        std::string errorMessage;        // Error message (if failed)
        Json::Value metadata;            // Additional context (JSON)
        int durationMs;                  // Operation duration in milliseconds
    };

    /**
     * @brief Record audit log entry
     *
     * Business Logic:
     * 1. Validate input parameters
     * 2. Extract metadata as JSONB
     * 3. Insert to operation_audit_log table
     * 4. Return success status
     *
     * @param entry Audit log entry
     * @return bool True if recorded successfully
     *
     * Table: operation_audit_log
     * Columns: operation_type, username, ip_address, success, error_message,
     *          metadata (JSONB), duration_ms, created_at
     */
    bool recordAuditLog(const AuditLogEntry& entry);

    /**
     * @brief Record file upload audit
     *
     * Convenience method for file upload operations.
     *
     * @param username Username
     * @param ipAddress Client IP
     * @param fileName File name
     * @param fileFormat "LDIF" or "MASTER_LIST"
     * @param fileSize File size in bytes
     * @param success Upload result
     * @param errorMessage Error message (if failed)
     * @param durationMs Duration in milliseconds
     * @return bool True if recorded successfully
     */
    bool recordFileUploadAudit(
        const std::string& username,
        const std::string& ipAddress,
        const std::string& fileName,
        const std::string& fileFormat,
        size_t fileSize,
        bool success,
        const std::string& errorMessage = "",
        int durationMs = 0
    );

    /**
     * @brief Record certificate search audit
     *
     * @param username Username
     * @param ipAddress Client IP
     * @param searchType "FINGERPRINT", "SUBJECT_DN", "COUNTRY", etc.
     * @param searchValue Search value
     * @param resultCount Number of results returned
     * @param durationMs Duration in milliseconds
     * @return bool True if recorded successfully
     */
    bool recordCertificateSearchAudit(
        const std::string& username,
        const std::string& ipAddress,
        const std::string& searchType,
        const std::string& searchValue,
        int resultCount,
        int durationMs = 0
    );

    /**
     * @brief Record PA verification audit
     *
     * @param username Username
     * @param ipAddress Client IP
     * @param verificationResult "VALID", "INVALID", "ERROR"
     * @param dscFingerprint DSC fingerprint used
     * @param errorMessage Error message (if failed)
     * @param durationMs Duration in milliseconds
     * @return bool True if recorded successfully
     */
    bool recordPaVerificationAudit(
        const std::string& username,
        const std::string& ipAddress,
        const std::string& verificationResult,
        const std::string& dscFingerprint,
        const std::string& errorMessage = "",
        int durationMs = 0
    );

    // ========================================================================
    // Audit Log Retrieval
    // ========================================================================

    /**
     * @brief Audit Log Filter Parameters
     */
    struct AuditLogFilter {
        int limit = 50;
        int offset = 0;
        std::string operationType;       // "FILE_UPLOAD", "CERTIFICATE_SEARCH", etc.
        std::string username;             // Filter by username
        std::string startDate;            // ISO 8601 format (e.g., "2026-01-29T00:00:00")
        std::string endDate;              // ISO 8601 format
        bool successOnly = false;         // Show only successful operations
        bool failedOnly = false;          // Show only failed operations
    };

    /**
     * @brief Get operation logs with filtering
     *
     * @param filter Filter and pagination parameters
     * @return Json::Value Paginated audit logs
     *
     * Response format:
     * {
     *   "success": true,
     *   "data": [...],
     *   "total": 1000,
     *   "count": 50,
     *   "limit": 50,
     *   "offset": 0
     * }
     *
     * Each log entry includes:
     * - operationType
     * - username
     * - ipAddress
     * - success
     * - errorMessage
     * - metadata (JSONB)
     * - durationMs
     * - createdAt
     */
    Json::Value getOperationLogs(const AuditLogFilter& filter);

    /**
     * @brief Get operation log by ID
     *
     * @param logId Log entry ID
     * @return Json::Value Log entry detail
     */
    Json::Value getOperationLogById(int logId);

    // ========================================================================
    // Operation Statistics
    // ========================================================================

    /**
     * @brief Get operation statistics
     *
     * @param startDate Start date (ISO 8601)
     * @param endDate End date (ISO 8601)
     * @return Json::Value Operation statistics
     *
     * Response format:
     * {
     *   "success": true,
     *   "data": {
     *     "totalOperations": 10000,
     *     "successfulOperations": 9500,
     *     "failedOperations": 500,
     *     "operationsByType": {
     *       "FILE_UPLOAD": 1000,
     *       "CERTIFICATE_SEARCH": 5000,
     *       "PA_VERIFY": 4000
     *     },
     *     "topUsers": [
     *       {"username": "admin", "count": 5000},
     *       {"username": "user1", "count": 3000}
     *     ],
     *     "averageDurationMs": 250
     *   }
     * }
     */
    Json::Value getOperationStatistics(
        const std::string& startDate = "",
        const std::string& endDate = ""
    );

    /**
     * @brief Get operation statistics by type
     *
     * @param operationType Operation type
     * @param startDate Start date (ISO 8601)
     * @param endDate End date (ISO 8601)
     * @return Json::Value Statistics for specific operation type
     */
    Json::Value getOperationStatisticsByType(
        const std::string& operationType,
        const std::string& startDate = "",
        const std::string& endDate = ""
    );

    // ========================================================================
    // User Activity Tracking
    // ========================================================================

    /**
     * @brief Get user activity summary
     *
     * @param username Username
     * @param startDate Start date (ISO 8601)
     * @param endDate End date (ISO 8601)
     * @return Json::Value User activity summary
     *
     * Response includes:
     * - Total operations
     * - Operations by type
     * - Success rate
     * - Average duration
     * - Recent operations (last 10)
     * - Most active hours
     */
    Json::Value getUserActivity(
        const std::string& username,
        const std::string& startDate = "",
        const std::string& endDate = ""
    );

    /**
     * @brief Get top active users
     *
     * @param limit Maximum number of users to return
     * @param startDate Start date (ISO 8601)
     * @param endDate End date (ISO 8601)
     * @return Json::Value Top active users with operation counts
     */
    Json::Value getTopActiveUsers(
        int limit = 10,
        const std::string& startDate = "",
        const std::string& endDate = ""
    );

    // ========================================================================
    // Security Event Monitoring
    // ========================================================================

    /**
     * @brief Get failed operations
     *
     * Retrieve operations that failed, useful for security monitoring.
     *
     * @param limit Maximum number of entries to return
     * @param offset Pagination offset
     * @return Json::Value Failed operations
     */
    Json::Value getFailedOperations(int limit = 50, int offset = 0);

    /**
     * @brief Get suspicious activities
     *
     * Detect suspicious patterns:
     * - Multiple failed login attempts from same IP
     * - Excessive search operations
     * - Unusual operation patterns
     *
     * @param startDate Start date (ISO 8601)
     * @param endDate End date (ISO 8601)
     * @return Json::Value Suspicious activities report
     */
    Json::Value getSuspiciousActivities(
        const std::string& startDate = "",
        const std::string& endDate = ""
    );

    /**
     * @brief Get operations by IP address
     *
     * Track operations from specific IP address.
     *
     * @param ipAddress IP address
     * @param limit Maximum number of entries to return
     * @param offset Pagination offset
     * @return Json::Value Operations from the IP
     */
    Json::Value getOperationsByIp(
        const std::string& ipAddress,
        int limit = 50,
        int offset = 0
    );

    // ========================================================================
    // Data Retention & Cleanup
    // ========================================================================

    /**
     * @brief Delete old audit logs
     *
     * Remove audit logs older than specified days.
     *
     * @param daysToKeep Number of days to keep (e.g., 90)
     * @return int Number of deleted entries
     *
     * Business Logic:
     * 1. Calculate cutoff date (NOW() - daysToKeep)
     * 2. Delete entries where created_at < cutoff_date
     * 3. Return deleted count
     */
    int deleteOldAuditLogs(int daysToKeep);

    /**
     * @brief Get audit log retention statistics
     *
     * @return Json::Value Retention statistics
     *
     * Response includes:
     * - Total log entries
     * - Oldest entry date
     * - Newest entry date
     * - Estimated size (MB)
     * - Entries by age (last 7 days, last 30 days, last 90 days, older)
     */
    Json::Value getRetentionStatistics();

private:
    // Repository Dependencies
    repositories::AuditRepository* auditRepo_;

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * @brief Convert JSON metadata to JSONB string
     */
    std::string jsonToJsonbString(const Json::Value& json);
};

} // namespace services

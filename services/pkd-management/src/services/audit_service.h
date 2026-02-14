#pragma once

#include <string>
#include <json/json.h>
#include "../repositories/audit_repository.h"

/**
 * @file audit_service.h
 * @brief Audit Service - Audit Log Business Logic Layer
 *
 * Handles audit log operations, statistics, and reporting.
 * Following DDD (Domain-Driven Design) and SRP (Single Responsibility Principle).
 *
 * Responsibilities:
 * - Audit log retrieval with filtering
 * - Audit log statistics calculation
 * - Operation type aggregation
 * - User activity tracking
 *
 * Does NOT handle:
 * - HTTP request/response (Controller's job)
 * - Direct database access (Repository's job)
 *
 * @date 2026-01-30
 */

namespace services {

/**
 * @brief Audit Service Class
 *
 * Encapsulates all business logic related to audit logging.
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

    /// @name Audit Log Retrieval
    /// @{

    /**
     * @brief Filter parameters for audit log query
     */
    struct AuditLogFilter {
        int limit = 50;
        int offset = 0;
        std::string operationType;
        std::string username;
        std::string success;  // "true", "false", or empty
    };

    /**
     * @brief Get audit log entries with filtering
     *
     * @param filter Filter and pagination parameters
     * @return Json::Value Audit log entries with pagination metadata
     *
     * Response format:
     * {
     *   "success": true,
     *   "data": [...],
     *   "count": 50,
     *   "total": 1000,
     *   "limit": 50,
     *   "offset": 0
     * }
     */
    Json::Value getOperationLogs(const AuditLogFilter& filter);

    /**
     * @brief Get audit log statistics
     *
     * @return Json::Value Statistics summary
     *
     * Response format:
     * {
     *   "success": true,
     *   "data": {
     *     "totalOperations": 10000,
     *     "successfulOperations": 9500,
     *     "failedOperations": 500,
     *     "operationsByType": {
     *       "UPLOAD": 5000,
     *       "PA_VERIFY": 3000,
     *       "CERTIFICATE_SEARCH": 2000
     *     },
     *     "topUsers": [
     *       {"username": "admin", "operationCount": 1000},
     *       {"username": "user1", "operationCount": 500}
     *     ],
     *     "averageDurationMs": 125
     *   }
     * }
     */
    Json::Value getOperationStatistics();

    /// @}

private:
    // Repository Dependencies
    repositories::AuditRepository* auditRepo_;

    /**
     * @brief Validate and normalize limit parameter
     * @param limit Input limit
     * @return Normalized limit (1-100)
     */
    int validateLimit(int limit);
};

} // namespace services

#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <vector>
#include <libpq-fe.h>
#include <json/json.h>
#include <spdlog/spdlog.h>
#include <drogon/HttpRequest.h>
#include "i_query_executor.h"

/**
 * @file audit_log.h
 * @brief Unified audit logging for ICAO PKD services
 *
 * This shared library consolidates audit logging across pkd-management,
 * pa-service, and pkd-relay services. All database operations are logged
 * to the operation_audit_log table with comprehensive context tracking.
 *
 * Version: 1.0.0
 * Created: 2026-02-03
 */

namespace icao {
namespace audit {

/**
 * @brief Operation types for audit logging across all services
 *
 * Consolidated from:
 * - pkd-management: FILE_UPLOAD, CERT_EXPORT, UPLOAD_DELETE, PA_VERIFY, SYNC_TRIGGER
 * - pa-service: PA_VERIFY
 * - pkd-relay: SYNC_TRIGGER
 */
enum class OperationType {
    // PKD Management Operations
    FILE_UPLOAD,        // LDIF/Master List file upload
    CERT_EXPORT,        // Certificate export by country
    UPLOAD_DELETE,      // Delete uploaded file and related data
    CERTIFICATE_SEARCH, // Certificate search operation

    // PA Service Operations
    PA_VERIFY,          // Passive Authentication verification
    PA_PARSE_SOD,       // Parse SOD (Security Object)
    PA_PARSE_DG1,       // Parse Data Group 1 (MRZ)
    PA_PARSE_DG2,       // Parse Data Group 2 (Face)

    // PKD Relay Operations
    SYNC_TRIGGER,       // Manual sync trigger
    SYNC_CHECK,         // Sync status check
    RECONCILE,          // DB-LDAP reconciliation
    REVALIDATE,         // Certificate re-validation

    // Common Operations
    CONFIG_UPDATE,      // Configuration update
    SYSTEM_HEALTH,      // Health check
    UNKNOWN             // Unknown operation type
};

/**
 * @brief Convert OperationType enum to string
 */
inline std::string operationTypeToString(OperationType type) {
    switch (type) {
        // PKD Management
        case OperationType::FILE_UPLOAD: return "FILE_UPLOAD";
        case OperationType::CERT_EXPORT: return "CERT_EXPORT";
        case OperationType::UPLOAD_DELETE: return "UPLOAD_DELETE";
        case OperationType::CERTIFICATE_SEARCH: return "CERTIFICATE_SEARCH";

        // PA Service
        case OperationType::PA_VERIFY: return "PA_VERIFY";
        case OperationType::PA_PARSE_SOD: return "PA_PARSE_SOD";
        case OperationType::PA_PARSE_DG1: return "PA_PARSE_DG1";
        case OperationType::PA_PARSE_DG2: return "PA_PARSE_DG2";

        // PKD Relay
        case OperationType::SYNC_TRIGGER: return "SYNC_TRIGGER";
        case OperationType::SYNC_CHECK: return "SYNC_CHECK";
        case OperationType::RECONCILE: return "RECONCILE";
        case OperationType::REVALIDATE: return "REVALIDATE";

        // Common
        case OperationType::CONFIG_UPDATE: return "CONFIG_UPDATE";
        case OperationType::SYSTEM_HEALTH: return "SYSTEM_HEALTH";
        case OperationType::UNKNOWN: return "UNKNOWN";

        default: return "UNKNOWN";
    }
}

/**
 * @brief Audit log entry structure
 *
 * Represents a single operation logged to operation_audit_log table.
 * All fields are optional except operationType to support flexible logging.
 */
struct AuditLogEntry {
    // User identification
    std::optional<std::string> userId;
    std::optional<std::string> username;

    // Operation details
    OperationType operationType;
    std::optional<std::string> operationSubtype;
    std::optional<std::string> resourceId;
    std::optional<std::string> resourceType;

    // Request context
    std::optional<std::string> ipAddress;
    std::optional<std::string> userAgent;
    std::optional<std::string> requestMethod;
    std::optional<std::string> requestPath;

    // Operation result
    bool success = true;
    std::optional<std::string> errorMessage;
    std::optional<std::string> errorCode;

    // Performance metrics
    std::optional<int> durationMs;

    // Additional context (stored as JSONB)
    std::optional<Json::Value> metadata;

    /**
     * @brief Default constructor
     */
    AuditLogEntry() = default;

    /**
     * @brief Constructor with operation type
     */
    explicit AuditLogEntry(OperationType type) : operationType(type) {}

    /**
     * @brief Convenience constructor with username
     */
    AuditLogEntry(OperationType type, const std::string& user)
        : operationType(type), username(user) {}
};

/**
 * @brief Log operation to operation_audit_log table
 *
 * @param conn PostgreSQL connection (must be valid)
 * @param entry Audit log entry with operation details
 * @return true if logging succeeded, false otherwise
 *
 * @note This function never throws exceptions. All errors are logged via spdlog.
 * @note If connection is null, the function returns false and logs a warning.
 *
 * Database Schema (operation_audit_log):
 * - id: SERIAL PRIMARY KEY
 * - user_id: VARCHAR(255)
 * - username: VARCHAR(255) NOT NULL DEFAULT 'anonymous'
 * - operation_type: VARCHAR(50) NOT NULL
 * - operation_subtype: VARCHAR(50)
 * - resource_id: VARCHAR(255)
 * - resource_type: VARCHAR(50)
 * - ip_address: VARCHAR(45)
 * - user_agent: TEXT
 * - request_method: VARCHAR(10)
 * - request_path: VARCHAR(500)
 * - success: BOOLEAN NOT NULL DEFAULT TRUE
 * - error_message: TEXT
 * - error_code: VARCHAR(50)
 * - duration_ms: INTEGER
 * - metadata: JSONB
 * - created_at: TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
 */
inline bool logOperation(PGconn* conn, const AuditLogEntry& entry) {
    if (!conn) {
        spdlog::warn("[AuditLog] Database connection not available for audit logging");
        return false;
    }

    try {
        // Build metadata JSON string for parameterized query
        std::string metadataStr;
        if (entry.metadata.has_value()) {
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";  // Compact JSON
            metadataStr = Json::writeString(writer, entry.metadata.value());
        } else {
            metadataStr = "{}";  // Empty JSON object
        }

        // Convert OperationType to string
        std::string opTypeStr = operationTypeToString(entry.operationType);

        // Prepare parameterized query (15 parameters)
        const char* query =
            "INSERT INTO operation_audit_log ("
            "user_id, username, operation_type, operation_subtype, "
            "resource_id, resource_type, ip_address, user_agent, "
            "request_method, request_path, success, error_message, "
            "error_code, duration_ms, metadata"
            ") VALUES ("
            "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15::jsonb"
            ")";

        // Prepare parameter values
        const char* paramValues[15];
        std::string userIdStr = entry.userId.value_or("");
        std::string usernameStr = entry.username.value_or("anonymous");  // Default to "anonymous"
        std::string opSubtypeStr = entry.operationSubtype.value_or("");
        std::string resourceIdStr = entry.resourceId.value_or("");
        std::string resourceTypeStr = entry.resourceType.value_or("");
        std::string ipStr = entry.ipAddress.value_or("");
        std::string userAgentStr = entry.userAgent.value_or("");
        std::string requestMethodStr = entry.requestMethod.value_or("");
        std::string requestPathStr = entry.requestPath.value_or("");
        std::string successStr = entry.success ? "TRUE" : "FALSE";
        std::string errorMsgStr = entry.errorMessage.value_or("");
        std::string errorCodeStr = entry.errorCode.value_or("");
        std::string durationStr = entry.durationMs.has_value()
            ? std::to_string(entry.durationMs.value())
            : "";

        // Assign pointers (empty strings are treated as NULL by PostgreSQL)
        paramValues[0] = userIdStr.empty() ? nullptr : userIdStr.c_str();
        paramValues[1] = usernameStr.c_str();  // Never NULL (defaults to "anonymous")
        paramValues[2] = opTypeStr.c_str();
        paramValues[3] = opSubtypeStr.empty() ? nullptr : opSubtypeStr.c_str();
        paramValues[4] = resourceIdStr.empty() ? nullptr : resourceIdStr.c_str();
        paramValues[5] = resourceTypeStr.empty() ? nullptr : resourceTypeStr.c_str();
        paramValues[6] = ipStr.empty() ? nullptr : ipStr.c_str();
        paramValues[7] = userAgentStr.empty() ? nullptr : userAgentStr.c_str();
        paramValues[8] = requestMethodStr.empty() ? nullptr : requestMethodStr.c_str();
        paramValues[9] = requestPathStr.empty() ? nullptr : requestPathStr.c_str();
        paramValues[10] = successStr.c_str();
        paramValues[11] = errorMsgStr.empty() ? nullptr : errorMsgStr.c_str();
        paramValues[12] = errorCodeStr.empty() ? nullptr : errorCodeStr.c_str();
        paramValues[13] = durationStr.empty() ? nullptr : durationStr.c_str();
        paramValues[14] = metadataStr.c_str();  // JSONB parameter

        // Execute parameterized query
        PGresult* res = PQexecParams(
            conn,
            query,
            15,              // Number of parameters
            nullptr,         // Let server infer types
            paramValues,     // Parameter values
            nullptr,         // Parameter lengths (not needed for text format)
            nullptr,         // Parameter formats (0 = text)
            0                // Result format (0 = text)
        );

        // Check result
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::error("[AuditLog] Failed to insert operation audit log: {}", PQerrorMessage(conn));
            PQclear(res);
            return false;
        }

        PQclear(res);

        // Log success (debug level to avoid clutter)
        spdlog::debug("[AuditLog] Operation logged: {} - {} (user: {}, success: {})",
                     opTypeStr,
                     entry.resourceId.value_or("N/A"),
                     entry.username.value_or("anonymous"),
                     entry.success ? "true" : "false");

        return true;

    } catch (const std::exception& e) {
        spdlog::error("[AuditLog] Exception while logging operation: {}", e.what());
        return false;
    }
}

/**
 * @brief Log operation to operation_audit_log table using Query Executor (database-agnostic)
 *
 * @param executor Query executor (PostgreSQL or Oracle)
 * @param entry Audit log entry with operation details
 * @return true if logging succeeded, false otherwise
 *
 * @note This function never throws exceptions. All errors are logged via spdlog.
 * @note Supports both PostgreSQL and Oracle via IQueryExecutor abstraction.
 */
inline bool logOperation(common::IQueryExecutor* executor, const AuditLogEntry& entry) {
    if (!executor) {
        spdlog::warn("[AuditLog] Query executor not available for audit logging");
        return false;
    }

    try {
        std::string dbType = executor->getDatabaseType();

        // Build metadata JSON string
        std::string metadataStr;
        if (entry.metadata.has_value()) {
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            metadataStr = Json::writeString(writer, entry.metadata.value());
        } else {
            metadataStr = "{}";
        }

        // Convert OperationType to string
        std::string opTypeStr = operationTypeToString(entry.operationType);

        // Database-aware boolean formatting
        std::string successStr;
        if (dbType == "oracle") {
            successStr = entry.success ? "1" : "0";
        } else {
            successStr = entry.success ? "TRUE" : "FALSE";
        }

        // Build query (without PostgreSQL-specific ::jsonb cast and NOW())
        std::string query =
            "INSERT INTO operation_audit_log ("
            "user_id, username, operation_type, operation_subtype, "
            "resource_id, resource_type, ip_address, user_agent, "
            "request_method, request_path, success, error_message, "
            "error_code, duration_ms, metadata"
            ") VALUES ("
            "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15"
            ")";

        // Build parameters
        std::vector<std::string> params = {
            entry.userId.value_or(""),
            entry.username.value_or("anonymous"),
            opTypeStr,
            entry.operationSubtype.value_or(""),
            entry.resourceId.value_or(""),
            entry.resourceType.value_or(""),
            entry.ipAddress.value_or(""),
            entry.userAgent.value_or(""),
            entry.requestMethod.value_or(""),
            entry.requestPath.value_or(""),
            successStr,
            entry.errorMessage.value_or(""),
            entry.errorCode.value_or(""),
            entry.durationMs.has_value() ? std::to_string(entry.durationMs.value()) : "",
            metadataStr
        };

        executor->executeCommand(query, params);

        spdlog::debug("[AuditLog] Operation logged: {} - {} (user: {}, success: {})",
                     opTypeStr,
                     entry.resourceId.value_or("N/A"),
                     entry.username.value_or("anonymous"),
                     entry.success ? "true" : "false");

        return true;

    } catch (const std::exception& e) {
        spdlog::error("[AuditLog] Exception while logging operation: {}", e.what());
        return false;
    }
}

/**
 * @brief Extract user information from HTTP request session
 *
 * @param req Drogon HTTP request
 * @return std::pair<userId, username> extracted from session, or empty if not found
 *
 * @note Requires JWT authentication middleware to populate session with:
 *       - "user_id" (string)
 *       - "username" (string)
 */
inline std::pair<std::optional<std::string>, std::optional<std::string>>
extractUserFromRequest(const drogon::HttpRequestPtr& req) {
    std::optional<std::string> userId;
    std::optional<std::string> username;

    auto session = req->getSession();
    if (!session) {
        return {userId, username};
    }

    try {
        userId = session->get<std::string>("user_id");
    } catch (...) {
        // user_id not found in session
    }

    try {
        username = session->get<std::string>("username");
    } catch (...) {
        // username not found in session
    }

    return {userId, username};
}

/**
 * @brief Extract client IP address from HTTP request
 *
 * @param req Drogon HTTP request
 * @return std::string IP address (IPv4 or IPv6), or "unknown" if not available
 *
 * @note Checks X-Forwarded-For header first (for proxied requests),
 *       then falls back to peer address.
 */
inline std::string extractIpAddress(const drogon::HttpRequestPtr& req) {
    // Check X-Forwarded-For header first (for nginx/haproxy)
    if (req->getHeader("X-Forwarded-For") != "") {
        return std::string(req->getHeader("X-Forwarded-For"));
    }

    // Fallback to peer address
    try {
        return req->getPeerAddr().toIp();
    } catch (...) {
        return "unknown";
    }
}

/**
 * @brief Create AuditLogEntry from HTTP request with common fields populated
 *
 * @param req Drogon HTTP request
 * @param opType Operation type
 * @return AuditLogEntry with request context filled
 *
 * Usage:
 * @code
 * auto entry = createAuditEntryFromRequest(req, OperationType::FILE_UPLOAD);
 * entry.success = true;
 * entry.resourceId = uploadId;
 * logOperation(dbConn, entry);
 * @endcode
 */
inline AuditLogEntry createAuditEntryFromRequest(
    const drogon::HttpRequestPtr& req,
    OperationType opType)
{
    AuditLogEntry entry(opType);

    // Extract user info
    auto [userId, username] = extractUserFromRequest(req);
    entry.userId = userId;
    entry.username = username;

    // Extract request context
    entry.ipAddress = extractIpAddress(req);
    entry.userAgent = std::string(req->getHeader("User-Agent"));
    entry.requestMethod = std::string(req->getMethodString());
    entry.requestPath = std::string(req->getPath());

    return entry;
}

} // namespace audit
} // namespace icao

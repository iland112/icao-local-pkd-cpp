#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <libpq-fe.h>
#include <json/json.h>
#include <spdlog/spdlog.h>
#include <drogon/HttpRequest.h>

namespace common {

/**
 * @brief Operation types for audit logging
 */
enum class OperationType {
    PA_VERIFY
};

/**
 * @brief Convert OperationType enum to string
 */
inline std::string operationTypeToString(OperationType type) {
    switch (type) {
        case OperationType::PA_VERIFY: return "PA_VERIFY";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Audit log entry structure
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
    std::optional<int> statusCode;
    std::optional<std::string> errorMessage;

    // Metadata
    std::optional<Json::Value> metadata;

    // Timing
    std::optional<int> durationMs;
};

/**
 * @brief RAII timer for measuring operation duration
 */
class AuditTimer {
public:
    AuditTimer() : startTime_(std::chrono::steady_clock::now()) {}

    int getDurationMs() const {
        auto endTime = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime_).count();
    }

private:
    std::chrono::steady_clock::time_point startTime_;
};

/**
 * @brief Log operation to operation_audit_log table
 *
 * @param conn PostgreSQL connection
 * @param entry Audit log entry
 * @return true if logged successfully, false otherwise
 */
inline bool logOperation(PGconn* conn, const AuditLogEntry& entry) {
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        spdlog::warn("[AuditLog] Database connection not available for audit logging");
        return false;
    }

    // Build metadata JSON string
    // Build metadata JSON string for parameterized query
    std::string metadataStr;
    if (entry.metadata.has_value()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        metadataStr = Json::writeString(builder, entry.metadata.value());
    }

    // Build fully parameterized query (no string interpolation)
    std::string query =
        "INSERT INTO operation_audit_log ("
        "user_id, username, "
        "operation_type, operation_subtype, resource_id, resource_type, "
        "ip_address, user_agent, request_method, request_path, "
        "success, status_code, error_message, metadata, duration_ms"
        ") VALUES ("
        "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14::jsonb, $15"
        ")";

    // Prepare parameter values
    std::string userIdStr = entry.userId.value_or("");
    std::string usernameStr = entry.username.value_or("anonymous");
    std::string operationTypeStr = operationTypeToString(entry.operationType);
    std::string operationSubtypeStr = entry.operationSubtype.value_or("");
    std::string resourceIdStr = entry.resourceId.value_or("");
    std::string resourceTypeStr = entry.resourceType.value_or("");
    std::string ipAddressStr = entry.ipAddress.value_or("");
    std::string userAgentStr = entry.userAgent.value_or("");
    std::string requestMethodStr = entry.requestMethod.value_or("");
    std::string requestPathStr = entry.requestPath.value_or("");
    std::string successStr = entry.success ? "true" : "false";
    std::string statusCodeStr = entry.statusCode.has_value() ? std::to_string(entry.statusCode.value()) : "";
    std::string errorMessageStr = entry.errorMessage.value_or("");
    std::string durationMsStr = entry.durationMs.has_value() ? std::to_string(entry.durationMs.value()) : "";

    const char* paramValues[15];
    paramValues[0] = userIdStr.empty() ? nullptr : userIdStr.c_str();
    paramValues[1] = usernameStr.c_str();  // Never null: defaults to "anonymous"
    paramValues[2] = operationTypeStr.c_str();
    paramValues[3] = operationSubtypeStr.empty() ? nullptr : operationSubtypeStr.c_str();
    paramValues[4] = resourceIdStr.empty() ? nullptr : resourceIdStr.c_str();
    paramValues[5] = resourceTypeStr.empty() ? nullptr : resourceTypeStr.c_str();
    paramValues[6] = ipAddressStr.empty() ? nullptr : ipAddressStr.c_str();
    paramValues[7] = userAgentStr.empty() ? nullptr : userAgentStr.c_str();
    paramValues[8] = requestMethodStr.empty() ? nullptr : requestMethodStr.c_str();
    paramValues[9] = requestPathStr.empty() ? nullptr : requestPathStr.c_str();
    paramValues[10] = successStr.c_str();
    paramValues[11] = statusCodeStr.empty() ? nullptr : statusCodeStr.c_str();
    paramValues[12] = errorMessageStr.empty() ? nullptr : errorMessageStr.c_str();
    paramValues[13] = metadataStr.empty() ? nullptr : metadataStr.c_str();
    paramValues[14] = durationMsStr.empty() ? nullptr : durationMsStr.c_str();

    // Execute query
    PGresult* res = PQexecParams(conn, query.c_str(), 15, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[AuditLog] Failed to insert operation audit log: {}", PQerrorMessage(conn));
        PQclear(res);
        return false;
    }

    PQclear(res);

    spdlog::debug("[AuditLog] Operation logged: {} - {} (user: {}, success: {})",
                 operationTypeStr,
                 entry.operationSubtype.value_or("N/A"),
                 entry.username.value_or("anonymous"),
                 entry.success);

    return true;
}

/**
 * @brief Helper function to extract user info from Drogon session
 *
 * @param session Drogon session
 * @return tuple of (userId, username)
 */
inline std::tuple<std::optional<std::string>, std::optional<std::string>>
getUserInfoFromSession(const drogon::SessionPtr& session) {
    std::optional<std::string> userId;
    std::optional<std::string> username;

    if (session) {
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
    }

    return {userId, username};
}

/**
 * @brief Helper function to extract IP address from Drogon request
 */
inline std::string getClientIpAddress(const drogon::HttpRequestPtr& req) {
    // Try X-Forwarded-For header first (proxy/load balancer)
    auto xForwardedFor = req->getHeader("X-Forwarded-For");
    if (!xForwardedFor.empty()) {
        // Take first IP if multiple (client IP)
        size_t commaPos = xForwardedFor.find(',');
        if (commaPos != std::string::npos) {
            return xForwardedFor.substr(0, commaPos);
        }
        return xForwardedFor;
    }

    // Fallback to peer address
    return req->peerAddr().toIp();
}

} // namespace common

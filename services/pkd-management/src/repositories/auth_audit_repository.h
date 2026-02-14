#pragma once

#include <string>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"
#include "../domain/models/auth_audit_log.h"

/**
 * @file auth_audit_repository.h
 * @brief AuthAudit Repository - Database Access Layer for auth_audit_log table
 *
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @date 2026-02-08
 */

namespace repositories {

class AuthAuditRepository {
public:
    /**
     * @brief Constructor
     * @param queryExecutor Query executor (PostgreSQL or Oracle, non-owning pointer)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit AuthAuditRepository(common::IQueryExecutor* queryExecutor);
    ~AuthAuditRepository() = default;

    /**
     * @brief Insert authentication audit log
     * @param userId User ID (optional for failed logins)
     * @param username Username
     * @param eventType Event type (LOGIN, LOGOUT, TOKEN_REFRESH, etc.)
     * @param success Success status
     * @param ipAddress IP address (optional)
     * @param userAgent User agent string (optional)
     * @param errorMessage Error message (optional)
     * @return true if insert successful, false otherwise
     */
    bool insert(
        const std::optional<std::string>& userId,
        const std::string& username,
        const std::string& eventType,
        bool success,
        const std::optional<std::string>& ipAddress,
        const std::optional<std::string>& userAgent,
        const std::optional<std::string>& errorMessage
    );

    /**
     * @brief Find audit logs with filter
     * @param limit Maximum number of records
     * @param offset Offset for pagination
     * @param userIdFilter Filter by user ID (empty = all)
     * @param usernameFilter Filter by username (empty = all)
     * @param eventTypeFilter Filter by event type (empty = all)
     * @param successFilter Filter by success status ("true", "false", or empty = all)
     * @param startDate Filter by start date (ISO 8601, empty = no filter)
     * @param endDate Filter by end date (ISO 8601, empty = no filter)
     * @return JSON array of audit logs
     */
    Json::Value findAll(
        int limit,
        int offset,
        const std::string& userIdFilter = "",
        const std::string& usernameFilter = "",
        const std::string& eventTypeFilter = "",
        const std::string& successFilter = "",
        const std::string& startDate = "",
        const std::string& endDate = ""
    );

    /**
     * @brief Count audit logs with filter
     * @param userIdFilter Filter by user ID (empty = all)
     * @param usernameFilter Filter by username (empty = all)
     * @param eventTypeFilter Filter by event type (empty = all)
     * @param successFilter Filter by success status ("true", "false", or empty = all)
     * @param startDate Filter by start date (ISO 8601, empty = no filter)
     * @param endDate Filter by end date (ISO 8601, empty = no filter)
     * @return Total count of matching records
     */
    int count(
        const std::string& userIdFilter = "",
        const std::string& usernameFilter = "",
        const std::string& eventTypeFilter = "",
        const std::string& successFilter = "",
        const std::string& startDate = "",
        const std::string& endDate = ""
    );

    /**
     * @brief Get authentication audit statistics
     * @return JSON object with statistics (total events, by event type, by user, failed logins, last 24h)
     */
    Json::Value getStatistics();

private:
    common::IQueryExecutor* queryExecutor_;  // Query executor (non-owning)

    /** @brief Convert snake_case to camelCase */
    std::string toCamelCase(const std::string& snake_case);
};

} // namespace repositories

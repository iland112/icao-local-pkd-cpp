#include "auth_audit_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace repositories {

// ============================================================================
// Constructor
// ============================================================================

AuthAuditRepository::AuthAuditRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("AuthAuditRepository: queryExecutor cannot be nullptr");
    }

    std::string dbType = queryExecutor_->getDatabaseType();
    spdlog::debug("[AuthAuditRepository] Initialized (DB type: {})", dbType);
}

// ============================================================================
// Public Methods
// ============================================================================

bool AuthAuditRepository::insert(
    const std::optional<std::string>& userId,
    const std::string& username,
    const std::string& eventType,
    bool success,
    const std::optional<std::string>& ipAddress,
    const std::optional<std::string>& userAgent,
    const std::optional<std::string>& errorMessage)
{
    try {
        spdlog::debug("[AuthAuditRepository] Inserting auth audit log: user={}, event={}",
                     username, eventType);

        const char* query =
            "INSERT INTO auth_audit_log "
            "(user_id, username, event_type, success, ip_address, user_agent, error_message) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7)";

        // Get database type for proper boolean formatting
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string successValue;
        if (dbType == "oracle") {
            successValue = success ? "1" : "0";  // Oracle expects NUMBER(1)
        } else {
            successValue = success ? "true" : "false";  // PostgreSQL BOOLEAN
        }

        std::vector<std::string> params = {
            userId.value_or(""),
            username,
            eventType,
            successValue,
            ipAddress.value_or(""),
            userAgent.value_or(""),
            errorMessage.value_or("")
        };

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        if (rowsAffected == 0) {
            spdlog::error("[AuthAuditRepository] Insert failed: no rows affected");
            return false;
        }

        spdlog::debug("[AuthAuditRepository] Auth audit log inserted successfully");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[AuthAuditRepository] insert failed: {}", e.what());
        return false;  // Don't throw - audit logging should not break application
    }
}

Json::Value AuthAuditRepository::findAll(
    int limit,
    int offset,
    const std::string& userIdFilter,
    const std::string& usernameFilter,
    const std::string& eventTypeFilter,
    const std::string& successFilter,
    const std::string& startDate,
    const std::string& endDate)
{
    try {
        spdlog::debug("[AuthAuditRepository] Finding audit logs (limit: {}, offset: {})",
                     limit, offset);

        // Build WHERE clause
        std::string whereClause = "WHERE 1=1";
        std::vector<std::string> params;
        int paramIndex = 1;

        if (!userIdFilter.empty()) {
            whereClause += " AND user_id = $" + std::to_string(paramIndex++);
            params.push_back(userIdFilter);
        }

        if (!usernameFilter.empty()) {
            whereClause += " AND username ILIKE $" + std::to_string(paramIndex++);
            params.push_back("%" + usernameFilter + "%");
        }

        if (!eventTypeFilter.empty()) {
            whereClause += " AND event_type = $" + std::to_string(paramIndex++);
            params.push_back(eventTypeFilter);
        }

        if (!successFilter.empty()) {
            whereClause += " AND success = $" + std::to_string(paramIndex++);
            params.push_back(successFilter == "true" ? "true" : "false");
        }

        if (!startDate.empty()) {
            whereClause += " AND created_at >= $" + std::to_string(paramIndex++);
            params.push_back(startDate);
        }

        if (!endDate.empty()) {
            whereClause += " AND created_at <= $" + std::to_string(paramIndex++);
            params.push_back(endDate);
        }

        // Main query
        std::string query =
            "SELECT id, user_id, username, event_type, ip_address, user_agent, "
            "success, error_message, created_at "
            "FROM auth_audit_log " + whereClause +
            " ORDER BY created_at DESC "
            "LIMIT $" + std::to_string(paramIndex++) +
            " OFFSET $" + std::to_string(paramIndex++);

        params.push_back(std::to_string(limit));
        params.push_back(std::to_string(offset));

        Json::Value result = queryExecutor_->executeQuery(query, params);
        spdlog::debug("[AuthAuditRepository] Found {} audit logs", result.size());

        return result;

    } catch (const std::exception& e) {
        spdlog::error("[AuthAuditRepository] findAll failed: {}", e.what());
        throw std::runtime_error("Failed to find auth audit logs: " + std::string(e.what()));
    }
}

int AuthAuditRepository::count(
    const std::string& userIdFilter,
    const std::string& usernameFilter,
    const std::string& eventTypeFilter,
    const std::string& successFilter,
    const std::string& startDate,
    const std::string& endDate)
{
    try {
        spdlog::debug("[AuthAuditRepository] Counting audit logs");

        // Build WHERE clause
        std::string whereClause = "WHERE 1=1";
        std::vector<std::string> params;
        int paramIndex = 1;

        if (!userIdFilter.empty()) {
            whereClause += " AND user_id = $" + std::to_string(paramIndex++);
            params.push_back(userIdFilter);
        }

        if (!usernameFilter.empty()) {
            whereClause += " AND username ILIKE $" + std::to_string(paramIndex++);
            params.push_back("%" + usernameFilter + "%");
        }

        if (!eventTypeFilter.empty()) {
            whereClause += " AND event_type = $" + std::to_string(paramIndex++);
            params.push_back(eventTypeFilter);
        }

        if (!successFilter.empty()) {
            whereClause += " AND success = $" + std::to_string(paramIndex++);
            params.push_back(successFilter == "true" ? "true" : "false");
        }

        if (!startDate.empty()) {
            whereClause += " AND created_at >= $" + std::to_string(paramIndex++);
            params.push_back(startDate);
        }

        if (!endDate.empty()) {
            whereClause += " AND created_at <= $" + std::to_string(paramIndex++);
            params.push_back(endDate);
        }

        std::string query = "SELECT COUNT(*) FROM auth_audit_log " + whereClause;

        Json::Value result = queryExecutor_->executeScalar(query, params);

        int count = result.asInt();
        spdlog::debug("[AuthAuditRepository] Total audit logs: {}", count);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[AuthAuditRepository] count failed: {}", e.what());
        throw std::runtime_error("Failed to count auth audit logs: " + std::string(e.what()));
    }
}

Json::Value AuthAuditRepository::getStatistics()
{
    try {
        spdlog::debug("[AuthAuditRepository] Getting statistics");

        Json::Value stats;

        // Total events
        {
            const char* query = "SELECT COUNT(*) FROM auth_audit_log";
            Json::Value result = queryExecutor_->executeScalar(query, {});
            stats["total_events"] = result.asInt();
        }

        // By event type
        {
            const char* query =
                "SELECT event_type, COUNT(*) as count FROM auth_audit_log "
                "GROUP BY event_type ORDER BY count DESC";

            Json::Value result = queryExecutor_->executeQuery(query, {});
            Json::Value byEventType;

            for (const auto& row : result) {
                byEventType[row["event_type"].asString()] = row["count"].asInt();
            }

            stats["by_event_type"] = byEventType;
        }

        // By user
        {
            const char* query =
                "SELECT username, COUNT(*) as count FROM auth_audit_log "
                "WHERE username != 'anonymous' "
                "GROUP BY username ORDER BY count DESC LIMIT 10";

            Json::Value result = queryExecutor_->executeQuery(query, {});
            Json::Value byUser;

            for (const auto& row : result) {
                byUser[row["username"].asString()] = row["count"].asInt();
            }

            stats["by_user"] = byUser;
        }

        // Recent failed logins
        {
            const char* query =
                "SELECT COUNT(*) FROM auth_audit_log "
                "WHERE event_type LIKE 'LOGIN%' AND success = false";

            Json::Value result = queryExecutor_->executeScalar(query, {});
            stats["recent_failed_logins"] = result.asInt();
        }

        // Last 24h events
        {
            std::string dbType = queryExecutor_->getDatabaseType();
            std::string query;

            if (dbType == "postgres") {
                query = "SELECT COUNT(*) FROM auth_audit_log WHERE created_at >= NOW() - INTERVAL '24 hours'";
            } else {
                query = "SELECT COUNT(*) FROM auth_audit_log WHERE created_at >= SYSTIMESTAMP - INTERVAL '1' DAY";
            }

            Json::Value result = queryExecutor_->executeScalar(query, {});
            stats["last_24h_events"] = result.asInt();
        }

        spdlog::debug("[AuthAuditRepository] Statistics retrieved successfully");
        return stats;

    } catch (const std::exception& e) {
        spdlog::error("[AuthAuditRepository] getStatistics failed: {}", e.what());
        throw std::runtime_error("Failed to get auth audit statistics: " + std::string(e.what()));
    }
}

} // namespace repositories

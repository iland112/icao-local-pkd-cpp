/** @file auth_audit_repository.cpp
 *  @brief AuthAuditRepository implementation
 */

#include "auth_audit_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace repositories {

// --- Constructor ---

AuthAuditRepository::AuthAuditRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("AuthAuditRepository: queryExecutor cannot be nullptr");
    }

    std::string dbType = queryExecutor_->getDatabaseType();
    spdlog::debug("[AuthAuditRepository] Initialized (DB type: {})", dbType);
}

// --- Public Methods ---

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
        std::string successValue = common::db::boolLiteral(dbType, success);

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

        std::string dbType = queryExecutor_->getDatabaseType();

        // Build WHERE clause
        std::string whereClause = "WHERE 1=1";
        std::vector<std::string> params;
        int paramIndex = 1;

        if (!userIdFilter.empty()) {
            whereClause += " AND user_id = $" + std::to_string(paramIndex++);
            params.push_back(userIdFilter);
        }

        if (!usernameFilter.empty()) {
            whereClause += " AND " + common::db::ilikeCond(dbType, "username", "$" + std::to_string(paramIndex++));
            params.push_back("%" + usernameFilter + "%");
        }

        if (!eventTypeFilter.empty()) {
            whereClause += " AND event_type = $" + std::to_string(paramIndex++);
            params.push_back(eventTypeFilter);
        }

        if (!successFilter.empty()) {
            bool isSuccess = (successFilter == "true" || successFilter == "1");
            std::string boolVal = common::db::boolLiteral(dbType, isSuccess);
            whereClause += " AND success = $" + std::to_string(paramIndex++);
            params.push_back(boolVal);
        }

        if (!startDate.empty()) {
            whereClause += " AND created_at >= $" + std::to_string(paramIndex++);
            params.push_back(startDate);
        }

        if (!endDate.empty()) {
            whereClause += " AND created_at <= $" + std::to_string(paramIndex++);
            params.push_back(endDate);
        }

        // Main query with database-specific pagination
        // Oracle: CLOB columns (user_agent, error_message) must be wrapped with
        // TO_CHAR() to avoid LOB/non-LOB mixed fetch issue (OCI returns only 1 row)
        std::string selectCols;
        if (dbType == "oracle") {
            selectCols = "SELECT id, user_id, username, event_type, ip_address, "
                         "TO_CHAR(user_agent) AS user_agent, "
                         "success, TO_CHAR(error_message) AS error_message, created_at ";
        } else {
            selectCols = "SELECT id, user_id, username, event_type, ip_address, user_agent, "
                         "success, error_message, created_at ";
        }
        std::string query = selectCols +
            "FROM auth_audit_log " + whereClause +
            " ORDER BY created_at DESC";

        if (dbType == "oracle") {
            int offsetIdx = paramIndex++;
            int limitIdx = paramIndex++;
            query += " OFFSET $" + std::to_string(offsetIdx) + " ROWS FETCH NEXT $" + std::to_string(limitIdx) + " ROWS ONLY";
            params.push_back(std::to_string(offset));
            params.push_back(std::to_string(limit));
        } else {
            int limitIdx = paramIndex++;
            int offsetIdx = paramIndex++;
            query += " LIMIT $" + std::to_string(limitIdx) + " OFFSET $" + std::to_string(offsetIdx);
            params.push_back(std::to_string(limit));
            params.push_back(std::to_string(offset));
        }

        Json::Value result = queryExecutor_->executeQuery(query, params);
        spdlog::debug("[AuthAuditRepository] Found {} audit logs", result.size());

        // Convert to camelCase and handle type conversions
        Json::Value array = Json::arrayValue;
        for (const auto& row : result) {
            Json::Value converted;
            for (const auto& key : row.getMemberNames()) {
                std::string camelKey = toCamelCase(key);

                if (key == "success") {
                    Json::Value val = row[key];
                    if (val.isBool()) {
                        converted[camelKey] = val.asBool();
                    } else if (val.isString()) {
                        std::string s = val.asString();
                        converted[camelKey] = (s == "t" || s == "true" || s == "1");
                    } else {
                        converted[camelKey] = val;
                    }
                } else {
                    converted[camelKey] = row[key];
                }
            }
            array.append(converted);
        }

        return array;

    } catch (const std::exception& e) {
        spdlog::error("[AuthAuditRepository] findAll failed: {}", e.what());
        throw std::runtime_error("Failed to find auth audit logs: " + std::string(e.what()));
    }
}

/** @brief Convert snake_case column name to camelCase */
std::string AuthAuditRepository::toCamelCase(const std::string& snake_case)
{
    std::string camelCase;
    bool capitalizeNext = false;
    for (char c : snake_case) {
        if (c == '_') {
            capitalizeNext = true;
        } else if (capitalizeNext) {
            camelCase += std::toupper(c);
            capitalizeNext = false;
        } else {
            camelCase += c;
        }
    }
    return camelCase;
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

        std::string dbType = queryExecutor_->getDatabaseType();

        // Build WHERE clause
        std::string whereClause = "WHERE 1=1";
        std::vector<std::string> params;
        int paramIndex = 1;

        if (!userIdFilter.empty()) {
            whereClause += " AND user_id = $" + std::to_string(paramIndex++);
            params.push_back(userIdFilter);
        }

        if (!usernameFilter.empty()) {
            whereClause += " AND " + common::db::ilikeCond(dbType, "username", "$" + std::to_string(paramIndex++));
            params.push_back("%" + usernameFilter + "%");
        }

        if (!eventTypeFilter.empty()) {
            whereClause += " AND event_type = $" + std::to_string(paramIndex++);
            params.push_back(eventTypeFilter);
        }

        if (!successFilter.empty()) {
            bool isSuccess = (successFilter == "true" || successFilter == "1");
            std::string boolVal = common::db::boolLiteral(dbType, isSuccess);
            whereClause += " AND success = $" + std::to_string(paramIndex++);
            params.push_back(boolVal);
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

        Json::Value result = params.empty() ?
            queryExecutor_->executeScalar(query) :
            queryExecutor_->executeScalar(query, params);

        // Oracle returns strings, PostgreSQL returns ints
        int count = common::db::scalarToInt(result);
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

        std::string dbType = queryExecutor_->getDatabaseType();

        // Helper to parse int from Oracle string or PostgreSQL int
        auto getInt = [](const Json::Value& val, int defaultVal) -> int {
            return common::db::scalarToInt(val, defaultVal);
        };

        Json::Value stats;

        // Total events
        {
            const char* query = "SELECT COUNT(*) FROM auth_audit_log";
            Json::Value result = queryExecutor_->executeScalar(query);
            stats["totalEvents"] = common::db::scalarToInt(result);
        }

        // By event type
        {
            const char* query =
                "SELECT event_type, COUNT(*) as cnt FROM auth_audit_log "
                "GROUP BY event_type ORDER BY cnt DESC";

            Json::Value result = queryExecutor_->executeQuery(query);
            Json::Value byEventType;

            for (const auto& row : result) {
                byEventType[row["event_type"].asString()] = getInt(row.get("cnt", 0), 0);
            }

            stats["byEventType"] = byEventType;
        }

        // Top users
        {
            std::string query =
                "SELECT username, COUNT(*) as cnt FROM auth_audit_log "
                "WHERE username != 'anonymous' "
                "GROUP BY username ORDER BY cnt DESC";
            query += common::db::limitClause(dbType, 10);

            Json::Value result = queryExecutor_->executeQuery(query);
            Json::Value topUsers = Json::arrayValue;

            for (const auto& row : result) {
                Json::Value user;
                user["username"] = row["username"].asString();
                user["count"] = getInt(row.get("cnt", 0), 0);
                topUsers.append(user);
            }

            stats["topUsers"] = topUsers;
        }

        // Failed logins count
        {
            std::string boolFalse = common::db::boolLiteral(dbType, false);
            std::string query =
                "SELECT COUNT(*) FROM auth_audit_log "
                "WHERE event_type LIKE 'LOGIN%' AND success = " + boolFalse;

            Json::Value result = queryExecutor_->executeScalar(query);
            stats["failedLogins"] = common::db::scalarToInt(result);
        }

        // Last 24h events
        {
            std::string query;
            if (dbType == "postgres") {
                query = "SELECT COUNT(*) FROM auth_audit_log WHERE created_at >= NOW() - INTERVAL '24 hours'";
            } else {
                query = "SELECT COUNT(*) FROM auth_audit_log WHERE created_at >= SYSTIMESTAMP - INTERVAL '1' DAY";
            }

            Json::Value result = queryExecutor_->executeScalar(query);
            stats["last24hEvents"] = common::db::scalarToInt(result);
        }

        spdlog::debug("[AuthAuditRepository] Statistics retrieved successfully");
        return stats;

    } catch (const std::exception& e) {
        spdlog::error("[AuthAuditRepository] getStatistics failed: {}", e.what());
        throw std::runtime_error("Failed to get auth audit statistics: " + std::string(e.what()));
    }
}

} // namespace repositories

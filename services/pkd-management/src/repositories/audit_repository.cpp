/** @file audit_repository.cpp
 *  @brief AuditRepository implementation
 */

#include "audit_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace repositories {

AuditRepository::AuditRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("AuditRepository: queryExecutor cannot be nullptr");
    }
    spdlog::debug("[AuditRepository] Initialized (DB type: {})", queryExecutor_->getDatabaseType());
}

bool AuditRepository::insert(
    const std::string& operationType,
    const std::string& username,
    const std::string& ipAddress,
    bool success,
    const std::string& errorMessage,
    const std::string& metadata,
    int durationMs
)
{
    spdlog::debug("[AuditRepository] Inserting audit log: {}", operationType);

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Remove ::jsonb cast and NOW() for Oracle compatibility
        // Oracle: metadata is CLOB, created_at has DEFAULT SYSTIMESTAMP
        // PostgreSQL: metadata is JSONB (QueryExecutor handles casting), created_at has DEFAULT CURRENT_TIMESTAMP
        const char* query =
            "INSERT INTO operation_audit_log "
            "(operation_type, username, ip_address, success, error_message, "
            "metadata, duration_ms) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7)";

        // Database-aware boolean formatting
        auto boolStr = [&dbType](bool val) -> std::string {
            return common::db::boolLiteral(dbType, val);
        };

        std::vector<std::string> params = {
            operationType,
            username,
            ipAddress,
            boolStr(success),
            errorMessage,
            metadata,
            std::to_string(durationMs)
        };

        queryExecutor_->executeCommand(query, params);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[AuditRepository] Insert failed: {}", e.what());
        return false;
    }
}

Json::Value AuditRepository::findAll(
    int limit,
    int offset,
    const std::string& operationType,
    const std::string& username,
    const std::string& successFilter
)
{
    spdlog::debug("[AuditRepository] Finding all (limit: {}, offset: {}, success: {})",
        limit, offset, successFilter);

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Build query with optional filters
        std::ostringstream query;
        query << "SELECT id, user_id, username, operation_type, operation_subtype, "
              << "resource_id, resource_type, ip_address, user_agent, "
              << "request_method, request_path, "
              << "success, status_code, error_message, metadata, duration_ms, created_at "
              << "FROM operation_audit_log WHERE 1=1";

        std::vector<std::string> params;
        int paramCount = 1;

        if (!operationType.empty()) {
            query << " AND operation_type = $" << paramCount++;
            params.push_back(operationType);
        }

        if (!username.empty()) {
            query << " AND username = $" << paramCount++;
            params.push_back(username);
        }

        // Add success filter to SQL WHERE clause (Oracle uses 1/0, PostgreSQL uses true/false)
        if (!successFilter.empty()) {
            std::string trueVal = common::db::boolLiteral(dbType, true);
            std::string falseVal = common::db::boolLiteral(dbType, false);
            if (successFilter == "true" || successFilter == "1") {
                query << " AND success = " << trueVal;
            } else if (successFilter == "false" || successFilter == "0") {
                query << " AND success = " << falseVal;
            }
        }

        query << " ORDER BY created_at DESC";

        // Database-specific pagination
        if (dbType == "oracle") {
            int offsetIdx = paramCount++;
            int limitIdx = paramCount++;
            query << " OFFSET $" << offsetIdx << " ROWS FETCH NEXT $" << limitIdx << " ROWS ONLY";
            params.push_back(std::to_string(offset));
            params.push_back(std::to_string(limit));
        } else {
            int limitIdx = paramCount++;
            int offsetIdx = paramCount++;
            query << " LIMIT $" << limitIdx << " OFFSET $" << offsetIdx;
            params.push_back(std::to_string(limit));
            params.push_back(std::to_string(offset));
        }

        Json::Value result = queryExecutor_->executeQuery(query.str(), params);

        // Convert field names to camelCase and handle type conversions
        Json::Value array = Json::arrayValue;
        for (const auto& row : result) {
            Json::Value convertedRow;
            for (const auto& key : row.getMemberNames()) {
                std::string camelKey = toCamelCase(key);

                // Handle boolean field (success)
                // PostgreSQL returns "t"/"f", Oracle returns "1"/"0"
                if (key == "success") {
                    Json::Value val = row[key];
                    if (val.isBool()) {
                        convertedRow[camelKey] = val.asBool();
                    } else if (val.isString()) {
                        std::string s = val.asString();
                        convertedRow[camelKey] = (s == "t" || s == "true" || s == "1");
                    } else {
                        convertedRow[camelKey] = val;
                    }
                }
                // Handle numeric fields
                else if (key == "duration_ms" || key == "status_code") {
                    Json::Value val = row[key];
                    if (val.isInt()) {
                        convertedRow[camelKey] = val.asInt();
                    } else if (val.isString()) {
                        try {
                            convertedRow[camelKey] = std::stoi(val.asString());
                        } catch (...) {
                            convertedRow[camelKey] = val;
                        }
                    } else {
                        convertedRow[camelKey] = val;
                    }
                }
                else {
                    convertedRow[camelKey] = row[key];
                }
            }
            array.append(convertedRow);
        }

        return array;

    } catch (const std::exception& e) {
        spdlog::error("[AuditRepository] Find all failed: {}", e.what());
        return Json::arrayValue;
    }
}

/** @brief Convert snake_case column name to camelCase */
std::string AuditRepository::toCamelCase(const std::string& snake_case)
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

int AuditRepository::countAll(
    const std::string& operationType,
    const std::string& username,
    const std::string& successFilter
)
{
    spdlog::debug("[AuditRepository] Counting all (operationType: {}, username: {}, success: {})",
        operationType, username, successFilter);

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Build query with optional filters
        std::ostringstream query;
        query << "SELECT COUNT(*) FROM operation_audit_log WHERE 1=1";

        std::vector<std::string> params;
        int paramCount = 1;

        if (!operationType.empty()) {
            query << " AND operation_type = $" << paramCount++;
            params.push_back(operationType);
        }

        if (!username.empty()) {
            query << " AND username = $" << paramCount++;
            params.push_back(username);
        }

        // Add success filter (Oracle uses 1/0, PostgreSQL uses true/false)
        if (!successFilter.empty()) {
            std::string trueVal = common::db::boolLiteral(dbType, true);
            std::string falseVal = common::db::boolLiteral(dbType, false);
            if (successFilter == "true" || successFilter == "1") {
                query << " AND success = " << trueVal;
            } else if (successFilter == "false" || successFilter == "0") {
                query << " AND success = " << falseVal;
            }
        }

        Json::Value result = params.empty() ?
            queryExecutor_->executeScalar(query.str()) :
            queryExecutor_->executeScalar(query.str(), params);

        // Oracle returns strings, PostgreSQL returns ints
        int count = common::db::scalarToInt(result);
        spdlog::debug("[AuditRepository] Count result: {}", count);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[AuditRepository] Count all failed: {}", e.what());
        return 0;
    }
}

int AuditRepository::countByOperationType(const std::string& operationType)
{
    spdlog::debug("[AuditRepository] Counting by operation type: {}", operationType);

    try {
        const char* query = "SELECT COUNT(*) FROM operation_audit_log WHERE operation_type = $1";
        std::vector<std::string> params = {operationType};

        Json::Value result = queryExecutor_->executeScalar(query, params);
        return common::db::scalarToInt(result);

    } catch (const std::exception& e) {
        spdlog::error("[AuditRepository] Count failed: {}", e.what());
        return 0;
    }
}

Json::Value AuditRepository::getStatistics(const std::string& startDate, const std::string& endDate)
{
    spdlog::debug("[AuditRepository] Getting statistics ({} to {})", startDate, endDate);

    Json::Value response;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Database-aware boolean comparison values
        std::string boolTrue = common::db::boolLiteral(dbType, true);
        std::string boolFalse = common::db::boolLiteral(dbType, false);

        // Helper to parse int from Oracle string or PostgreSQL int
        auto getInt = [](const Json::Value& val, int defaultVal) -> int {
            return common::db::scalarToInt(val, defaultVal);
        };

        // Total operations (use non-reserved alias names for Oracle compatibility)
        std::string countQuery = "SELECT COUNT(*) as total, "
                                "SUM(CASE WHEN success = " + boolTrue + " THEN 1 ELSE 0 END) as success_count, "
                                "SUM(CASE WHEN success = " + boolFalse + " THEN 1 ELSE 0 END) as fail_count, "
                                "AVG(duration_ms) as avg_duration "
                                "FROM operation_audit_log";

        std::vector<std::string> params;
        if (!startDate.empty() && !endDate.empty()) {
            countQuery += " WHERE created_at >= $1::timestamp AND created_at <= $2::timestamp";
            params.push_back(startDate);
            params.push_back(endDate);
        }

        Json::Value countResult = params.empty() ?
            queryExecutor_->executeQuery(countQuery) :
            queryExecutor_->executeQuery(countQuery, params);

        if (!countResult.empty()) {
            response["totalOperations"] = getInt(countResult[0].get("total", 0), 0);
            response["successfulOperations"] = getInt(countResult[0].get("success_count", 0), 0);
            response["failedOperations"] = getInt(countResult[0].get("fail_count", 0), 0);

            Json::Value avgVal = countResult[0].get("avg_duration", Json::nullValue);
            response["averageDurationMs"] = avgVal.isNull() ? 0 : getInt(avgVal, 0);
        }

        // Operations by type
        std::string typeQuery = "SELECT operation_type, COUNT(*) as count "
                               "FROM operation_audit_log";
        if (!startDate.empty() && !endDate.empty()) {
            typeQuery += " WHERE created_at >= $1::timestamp AND created_at <= $2::timestamp";
        }
        typeQuery += " GROUP BY operation_type ORDER BY count DESC";

        Json::Value typeResult = params.empty() ?
            queryExecutor_->executeQuery(typeQuery) :
            queryExecutor_->executeQuery(typeQuery, params);

        Json::Value operationsByType;
        for (const auto& row : typeResult) {
            std::string opType = row.get("operation_type", "").asString();
            int count = getInt(row.get("count", 0), 0);
            operationsByType[opType] = count;
        }
        response["operationsByType"] = operationsByType;

        // Top users (database-specific pagination)
        std::string userQuery = "SELECT username, COUNT(*) as count "
                               "FROM operation_audit_log";
        if (!startDate.empty() && !endDate.empty()) {
            userQuery += " WHERE created_at >= $1::timestamp AND created_at <= $2::timestamp";
        }
        userQuery += " GROUP BY username ORDER BY count DESC";
        userQuery += common::db::limitClause(dbType, 10);

        Json::Value userResult = params.empty() ?
            queryExecutor_->executeQuery(userQuery) :
            queryExecutor_->executeQuery(userQuery, params);

        Json::Value topUsers = Json::arrayValue;
        for (const auto& row : userResult) {
            Json::Value user;
            user["username"] = row.get("username", "").asString();
            user["operationCount"] = getInt(row.get("count", 0), 0);
            topUsers.append(user);
        }
        response["topUsers"] = topUsers;

    } catch (const std::exception& e) {
        spdlog::error("[AuditRepository] Get statistics failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

} // namespace repositories

#include "audit_repository.h"
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
        const char* query =
            "INSERT INTO operation_audit_log "
            "(operation_type, username, ip_address, success, error_message, "
            "metadata, duration_ms, created_at) "
            "VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7, NOW())";

        std::vector<std::string> params = {
            operationType,
            username,
            ipAddress,
            success ? "true" : "false",
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

        // Add success filter to SQL WHERE clause
        if (!successFilter.empty()) {
            if (successFilter == "true" || successFilter == "1") {
                query << " AND success = true";
            } else if (successFilter == "false" || successFilter == "0") {
                query << " AND success = false";
            }
        }

        query << " ORDER BY created_at DESC "
              << " LIMIT $" << paramCount++ << " OFFSET $" << paramCount++;
        params.push_back(std::to_string(limit));
        params.push_back(std::to_string(offset));

        Json::Value result = queryExecutor_->executeQuery(query.str(), params);

        // Convert field names to camelCase and handle type conversions
        Json::Value array = Json::arrayValue;
        for (const auto& row : result) {
            Json::Value convertedRow;
            for (const auto& key : row.getMemberNames()) {
                std::string camelKey = toCamelCase(key);

                // Handle boolean field (success)
                if (key == "success") {
                    Json::Value val = row[key];
                    if (val.isBool()) {
                        convertedRow[camelKey] = val.asBool();
                    } else if (val.isString()) {
                        convertedRow[camelKey] = (val.asString() == "t" || val.asString() == "true");
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

// Helper function for camelCase conversion
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

        // Add success filter to SQL WHERE clause
        if (!successFilter.empty()) {
            if (successFilter == "true" || successFilter == "1") {
                query << " AND success = true";
            } else if (successFilter == "false" || successFilter == "0") {
                query << " AND success = false";
            }
        }

        Json::Value result = params.empty() ?
            queryExecutor_->executeScalar(query.str()) :
            queryExecutor_->executeScalar(query.str(), params);

        int count = result.asInt();
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
        return result.asInt();

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
        // Total operations
        std::string countQuery = "SELECT COUNT(*) as total, "
                                "SUM(CASE WHEN success = TRUE THEN 1 ELSE 0 END) as successful, "
                                "SUM(CASE WHEN success = FALSE THEN 1 ELSE 0 END) as failed, "
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
            response["totalOperations"] = countResult[0].get("total", 0).asInt();
            response["successfulOperations"] = countResult[0].get("successful", 0).asInt();
            response["failedOperations"] = countResult[0].get("failed", 0).asInt();
            response["averageDurationMs"] = countResult[0].get("avg_duration", Json::nullValue).isNull() ?
                0 : countResult[0].get("avg_duration", 0).asInt();
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
            int count = row.get("count", 0).asInt();
            operationsByType[opType] = count;
        }
        response["operationsByType"] = operationsByType;

        // Top users
        std::string userQuery = "SELECT username, COUNT(*) as count "
                               "FROM operation_audit_log";
        if (!startDate.empty() && !endDate.empty()) {
            userQuery += " WHERE created_at >= $1::timestamp AND created_at <= $2::timestamp";
        }
        userQuery += " GROUP BY username ORDER BY count DESC LIMIT 10";

        Json::Value userResult = params.empty() ?
            queryExecutor_->executeQuery(userQuery) :
            queryExecutor_->executeQuery(userQuery, params);

        Json::Value topUsers = Json::arrayValue;
        for (const auto& row : userResult) {
            Json::Value user;
            user["username"] = row.get("username", "").asString();
            user["count"] = row.get("count", 0).asInt();
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

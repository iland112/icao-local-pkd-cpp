#include "audit_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

AuditRepository::AuditRepository(PGconn* dbConn)
    : dbConn_(dbConn)
{
    if (!dbConn_) {
        throw std::invalid_argument("AuditRepository: dbConn cannot be nullptr");
    }
    spdlog::debug("[AuditRepository] Initialized");
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

        PGresult* res = executeParamQuery(query, params);
        PQclear(res);

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
    const std::string& username
)
{
    spdlog::debug("[AuditRepository] Finding all (limit: {}, offset: {})", limit, offset);

    try {
        // Build query with optional filters
        std::ostringstream query;
        query << "SELECT id, username, operation_type, operation_subtype, "
              << "resource_id, resource_type, ip_address, "
              << "success, error_message, metadata, duration_ms, created_at "
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

        query << " ORDER BY created_at DESC "
              << " LIMIT $" << paramCount++ << " OFFSET $" << paramCount++;
        params.push_back(std::to_string(limit));
        params.push_back(std::to_string(offset));

        PGresult* res = executeParamQuery(query.str(), params);
        Json::Value array = pgResultToJson(res);
        PQclear(res);

        return array;

    } catch (const std::exception& e) {
        spdlog::error("[AuditRepository] Find all failed: {}", e.what());
        return Json::arrayValue;
    }
}

int AuditRepository::countByOperationType(const std::string& operationType)
{
    spdlog::debug("[AuditRepository] Counting by operation type: {}", operationType);

    try {
        const char* query = "SELECT COUNT(*) FROM operation_audit_log WHERE operation_type = $1";
        std::vector<std::string> params = {operationType};

        PGresult* res = executeParamQuery(query, params);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

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

        PGresult* countRes = params.empty() ? executeQuery(countQuery) : executeParamQuery(countQuery, params);

        response["totalOperations"] = std::atoi(PQgetvalue(countRes, 0, 0));
        response["successfulOperations"] = std::atoi(PQgetvalue(countRes, 0, 1));
        response["failedOperations"] = std::atoi(PQgetvalue(countRes, 0, 2));
        response["averageDurationMs"] = PQgetisnull(countRes, 0, 3) ? 0 : std::atoi(PQgetvalue(countRes, 0, 3));
        PQclear(countRes);

        // Operations by type
        std::string typeQuery = "SELECT operation_type, COUNT(*) as count "
                               "FROM operation_audit_log";
        if (!startDate.empty() && !endDate.empty()) {
            typeQuery += " WHERE created_at >= $1::timestamp AND created_at <= $2::timestamp";
        }
        typeQuery += " GROUP BY operation_type ORDER BY count DESC";

        PGresult* typeRes = params.empty() ? executeQuery(typeQuery) : executeParamQuery(typeQuery, params);

        Json::Value operationsByType;
        int typeRows = PQntuples(typeRes);
        for (int i = 0; i < typeRows; i++) {
            std::string opType = PQgetvalue(typeRes, i, 0);
            int count = std::atoi(PQgetvalue(typeRes, i, 1));
            operationsByType[opType] = count;
        }
        PQclear(typeRes);
        response["operationsByType"] = operationsByType;

        // Top users
        std::string userQuery = "SELECT username, COUNT(*) as count "
                               "FROM operation_audit_log";
        if (!startDate.empty() && !endDate.empty()) {
            userQuery += " WHERE created_at >= $1::timestamp AND created_at <= $2::timestamp";
        }
        userQuery += " GROUP BY username ORDER BY count DESC LIMIT 10";

        PGresult* userRes = params.empty() ? executeQuery(userQuery) : executeParamQuery(userQuery, params);
        Json::Value topUsers = Json::arrayValue;
        int userRows = PQntuples(userRes);
        for (int i = 0; i < userRows; i++) {
            Json::Value user;
            user["username"] = PQgetvalue(userRes, i, 0);
            user["count"] = std::atoi(PQgetvalue(userRes, i, 1));
            topUsers.append(user);
        }
        PQclear(userRes);
        response["topUsers"] = topUsers;

    } catch (const std::exception& e) {
        spdlog::error("[AuditRepository] Get statistics failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

PGresult* AuditRepository::executeParamQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
    }

    PGresult* res = PQexecParams(
        dbConn_,
        query.c_str(),
        params.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)) {
        std::string error = res ? PQerrorMessage(dbConn_) : "null result";
        if (res) PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

PGresult* AuditRepository::executeQuery(const std::string& query)
{
    PGresult* res = PQexec(dbConn_, query.c_str());

    if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)) {
        std::string error = res ? PQerrorMessage(dbConn_) : "null result";
        if (res) PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

Json::Value AuditRepository::pgResultToJson(PGresult* res)
{
    Json::Value array = Json::arrayValue;
    int rows = PQntuples(res);
    int cols = PQnfields(res);

    for (int i = 0; i < rows; ++i) {
        Json::Value row;
        for (int j = 0; j < cols; ++j) {
            const char* fieldName = PQfname(res, j);
            if (PQgetisnull(res, i, j)) {
                row[fieldName] = Json::nullValue;
            } else {
                row[fieldName] = PQgetvalue(res, i, j);
            }
        }
        array.append(row);
    }

    return array;
}

} // namespace repositories

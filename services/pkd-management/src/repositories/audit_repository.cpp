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

    // TODO: Implement find all with filters
    spdlog::warn("[AuditRepository] findAll - TODO: Implement");

    Json::Value response = Json::arrayValue;
    return response;
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
    spdlog::debug("[AuditRepository] Getting statistics");

    // TODO: Implement statistics query
    spdlog::warn("[AuditRepository] getStatistics - TODO: Implement");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
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

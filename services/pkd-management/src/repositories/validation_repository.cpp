#include "validation_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

ValidationRepository::ValidationRepository(PGconn* dbConn)
    : dbConn_(dbConn)
{
    if (!dbConn_) {
        throw std::invalid_argument("ValidationRepository: dbConn cannot be nullptr");
    }
    spdlog::debug("[ValidationRepository] Initialized");
}

bool ValidationRepository::save(
    const std::string& fingerprint,
    const std::string& uploadId,
    const std::string& certificateType,
    const std::string& validationStatus,
    bool trustChainValid,
    const std::string& trustChainPath,
    bool signatureValid,
    bool crlChecked,
    bool revoked
)
{
    spdlog::debug("[ValidationRepository] Saving validation for: {}...", fingerprint.substr(0, 16));

    // TODO: Implement validation result save
    spdlog::warn("[ValidationRepository] save - TODO: Implement");

    return false;
}

Json::Value ValidationRepository::findByFingerprint(const std::string& fingerprint)
{
    spdlog::debug("[ValidationRepository] Finding by fingerprint: {}...", fingerprint.substr(0, 16));

    // TODO: Implement find by fingerprint
    spdlog::warn("[ValidationRepository] findByFingerprint - TODO: Implement");

    return Json::nullValue;
}

Json::Value ValidationRepository::findByUploadId(
    const std::string& uploadId,
    int limit,
    int offset,
    const std::string& statusFilter
)
{
    spdlog::debug("[ValidationRepository] Finding by upload ID: {}", uploadId);

    // TODO: Implement find by upload ID
    spdlog::warn("[ValidationRepository] findByUploadId - TODO: Implement");

    Json::Value response = Json::arrayValue;
    return response;
}

int ValidationRepository::countByStatus(const std::string& status)
{
    spdlog::debug("[ValidationRepository] Counting by status: {}", status);

    try {
        const char* query = "SELECT COUNT(*) FROM validation_result WHERE validation_status = $1";
        std::vector<std::string> params = {status};

        PGresult* res = executeParamQuery(query, params);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Count by status failed: {}", e.what());
        return 0;
    }
}

PGresult* ValidationRepository::executeParamQuery(
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

PGresult* ValidationRepository::executeQuery(const std::string& query)
{
    PGresult* res = PQexec(dbConn_, query.c_str());

    if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)) {
        std::string error = res ? PQerrorMessage(dbConn_) : "null result";
        if (res) PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

Json::Value ValidationRepository::pgResultToJson(PGresult* res)
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

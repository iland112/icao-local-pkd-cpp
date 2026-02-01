#include "statistics_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

StatisticsRepository::StatisticsRepository(PGconn* dbConn)
    : dbConn_(dbConn)
{
    if (!dbConn_) {
        throw std::invalid_argument("StatisticsRepository: dbConn cannot be nullptr");
    }
    spdlog::debug("[StatisticsRepository] Initialized");
}

Json::Value StatisticsRepository::getUploadStatistics()
{
    spdlog::debug("[StatisticsRepository] Getting upload statistics");

    // TODO: Implement upload statistics query
    spdlog::warn("[StatisticsRepository] getUploadStatistics - TODO: Implement");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value StatisticsRepository::getCertificateStatistics()
{
    spdlog::debug("[StatisticsRepository] Getting certificate statistics");

    // TODO: Implement certificate statistics query
    spdlog::warn("[StatisticsRepository] getCertificateStatistics - TODO: Implement");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value StatisticsRepository::getCountryStatistics()
{
    spdlog::debug("[StatisticsRepository] Getting country statistics");

    // TODO: Implement country statistics query
    spdlog::warn("[StatisticsRepository] getCountryStatistics - TODO: Implement");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value StatisticsRepository::getDetailedCountryStatistics(int limit)
{
    spdlog::debug("[StatisticsRepository] Getting detailed country statistics (limit: {})", limit);

    // TODO: Implement detailed country statistics query
    // This is a complex query joining certificate table with multiple conditions
    spdlog::warn("[StatisticsRepository] getDetailedCountryStatistics - TODO: Implement");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value StatisticsRepository::getValidationStatistics()
{
    spdlog::debug("[StatisticsRepository] Getting validation statistics");

    // TODO: Implement validation statistics query
    spdlog::warn("[StatisticsRepository] getValidationStatistics - TODO: Implement");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value StatisticsRepository::getSystemStatistics()
{
    spdlog::debug("[StatisticsRepository] Getting system statistics");

    // TODO: Implement system statistics query
    spdlog::warn("[StatisticsRepository] getSystemStatistics - TODO: Implement");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

PGresult* StatisticsRepository::executeQuery(const std::string& query)
{
    PGresult* res = PQexec(dbConn_, query.c_str());

    if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)) {
        std::string error = res ? PQerrorMessage(dbConn_) : "null result";
        if (res) PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

Json::Value StatisticsRepository::pgResultToJson(PGresult* res)
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
                Oid type = PQftype(res, j);
                if (type == 23 || type == 20) {  // INT4 or INT8
                    row[fieldName] = std::atoi(PQgetvalue(res, i, j));
                } else if (type == 700 || type == 701) {  // FLOAT4 or FLOAT8
                    row[fieldName] = std::atof(PQgetvalue(res, i, j));
                } else if (type == 16) {  // BOOL
                    row[fieldName] = (PQgetvalue(res, i, j)[0] == 't');
                } else {
                    row[fieldName] = PQgetvalue(res, i, j);
                }
            }
        }
        array.append(row);
    }

    return array;
}

} // namespace repositories

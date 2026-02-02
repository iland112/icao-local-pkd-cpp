/**
 * @file data_group_repository.cpp
 * @brief Implementation of DataGroupRepository
 */

#include "data_group_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <iomanip>
#include <sstream>

namespace repositories {

DataGroupRepository::DataGroupRepository(PGconn* conn)
    : dbConn_(conn)
{
    if (!dbConn_) {
        throw std::invalid_argument("Database connection cannot be null");
    }
    spdlog::debug("DataGroupRepository initialized");
}

// ==========================================================================
// Query Methods
// ==========================================================================

Json::Value DataGroupRepository::findByVerificationId(const std::string& verificationId) {
    spdlog::debug("Finding data groups for verification: {}", verificationId);

    const char* query = R"SQL(
        SELECT id, verification_id, dg_number, expected_hash, actual_hash,
               hash_algorithm, hash_valid, dg_binary, length(dg_binary) as data_size
        FROM pa_data_group
        WHERE verification_id = $1
        ORDER BY dg_number ASC
    )SQL";

    std::vector<std::string> params = {verificationId};
    PGresult* res = executeParamQuery(query, params);

    if (!res) {
        throw std::runtime_error("Failed to execute query");
    }

    Json::Value result = Json::arrayValue;
    int rows = PQntuples(res);

    for (int i = 0; i < rows; i++) {
        result.append(resultToDataGroupJson(res, i));
    }

    PQclear(res);

    spdlog::debug("Found {} data groups for verification {}", rows, verificationId);
    return result;
}

Json::Value DataGroupRepository::findById(const std::string& id) {
    spdlog::debug("Finding data group by ID: {}", id);

    const char* query = R"SQL(
        SELECT id, verification_id, dg_number, expected_hash, actual_hash,
               hash_algorithm, hash_valid, length(dg_binary) as data_size
        FROM pa_data_group
        WHERE id = $1
    )SQL";

    std::vector<std::string> params = {id};
    PGresult* res = executeParamQuery(query, params);

    if (!res) {
        throw std::runtime_error("Failed to execute query");
    }

    Json::Value result = Json::nullValue;
    if (PQntuples(res) > 0) {
        result = resultToDataGroupJson(res, 0);
    }

    PQclear(res);
    return result;
}

std::string DataGroupRepository::insert(
    const domain::models::DataGroup& dg,
    const std::string& verificationId)
{
    spdlog::debug("Inserting data group {} for verification {}", dg.dgNumber, verificationId);

    // Extract DG number from string (e.g., "DG1" -> 1)
    int dgNumber = 0;
    if (dg.dgNumber.find("DG") == 0) {
        dgNumber = std::stoi(dg.dgNumber.substr(2));
    }

    const char* query = R"SQL(
        INSERT INTO pa_data_group (
            verification_id, dg_number, expected_hash, actual_hash,
            hash_algorithm, hash_valid, dg_binary
        ) VALUES (
            $1, $2, $3, $4, $5, $6, $7
        ) RETURNING id
    )SQL";

    // Prepare parameters
    std::vector<std::string> params;
    params.push_back(verificationId);
    params.push_back(std::to_string(dgNumber));
    params.push_back(dg.expectedHash);
    params.push_back(dg.actualHash);
    params.push_back(dg.hashAlgorithm);
    params.push_back(dg.hashValid ? "true" : "false");

    // Handle binary data (empty if not present)
    std::string binaryData;
    if (dg.rawData.has_value() && !dg.rawData.value().empty()) {
        // Convert vector<uint8_t> to hex string for PostgreSQL bytea
        const auto& data = dg.rawData.value();
        std::ostringstream oss;
        oss << "\\x";  // PostgreSQL hex format
        for (uint8_t byte : data) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        binaryData = oss.str();
    }
    params.push_back(binaryData);

    // Execute query with parameters
    const char* paramValues[7];
    for (size_t i = 0; i < params.size(); i++) {
        paramValues[i] = params[i].c_str();
    }

    PGresult* res = PQexecParams(
        dbConn_,
        query,
        7,  // number of parameters
        nullptr,
        paramValues,
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(dbConn_);
        PQclear(res);
        throw std::runtime_error("Failed to insert data group: " + error);
    }

    std::string id = PQgetvalue(res, 0, 0);
    PQclear(res);

    spdlog::debug("Inserted data group with ID: {}", id);
    return id;
}

int DataGroupRepository::deleteByVerificationId(const std::string& verificationId) {
    spdlog::debug("Deleting data groups for verification: {}", verificationId);

    const char* query = "DELETE FROM pa_data_group WHERE verification_id = $1";

    std::vector<std::string> params = {verificationId};
    PGresult* res = executeParamQuery(query, params);

    if (!res) {
        throw std::runtime_error("Failed to delete data groups");
    }

    std::string deletedCount = PQcmdTuples(res);
    PQclear(res);

    int count = deletedCount.empty() ? 0 : std::stoi(deletedCount);
    spdlog::debug("Deleted {} data groups", count);
    return count;
}

// ==========================================================================
// Helper Methods
// ==========================================================================

PGresult* DataGroupRepository::executeParamQuery(
    const std::string& query,
    const std::vector<std::string>& params)
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

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(dbConn_);
        PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

Json::Value DataGroupRepository::resultToDataGroupJson(PGresult* res, int row) {
    Json::Value dg;

    // Extract columns
    dg["id"] = PQgetvalue(res, row, 0);
    dg["verificationId"] = PQgetvalue(res, row, 1);

    int dgNumber = std::stoi(PQgetvalue(res, row, 2));
    dg["dgNumber"] = dgNumber;

    dg["expectedHash"] = PQgetvalue(res, row, 3);
    dg["actualHash"] = PQgetvalue(res, row, 4);
    dg["hashAlgorithm"] = PQgetvalue(res, row, 5);

    // Boolean field
    std::string hashValidStr = PQgetvalue(res, row, 6);
    dg["hashValid"] = (hashValidStr == "t" || hashValidStr == "true");

    // Binary data (column 7) - convert to hex string
    if (!PQgetisnull(res, row, 7)) {
        char* binaryStr = PQgetvalue(res, row, 7);
        dg["dgBinary"] = binaryStr;  // PostgreSQL bytea returns as hex format
    } else {
        dg["dgBinary"] = "";
    }

    // Data size (column 8)
    if (!PQgetisnull(res, row, 8)) {
        dg["dataSize"] = std::stoi(PQgetvalue(res, row, 8));
    } else {
        dg["dataSize"] = 0;
    }

    return dg;
}

} // namespace repositories

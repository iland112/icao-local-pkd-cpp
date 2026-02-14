/**
 * @file data_group_repository.cpp
 * @brief Implementation of DataGroupRepository (Query Executor Pattern)
 * @updated 2026-02-05
 */

#include "data_group_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <map>

namespace repositories {

// --- Constructor ---

DataGroupRepository::DataGroupRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("DataGroupRepository: queryExecutor cannot be nullptr");
    }

    spdlog::debug("[DataGroupRepository] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

// --- Query Methods ---

Json::Value DataGroupRepository::findByVerificationId(const std::string& verificationId) {
    spdlog::debug("[DataGroupRepository] Finding data groups for verification: {}", verificationId);

    try {
        const char* query = R"SQL(
            SELECT id, verification_id, dg_number, expected_hash, actual_hash,
                   hash_algorithm, hash_valid, dg_binary,
                   length(dg_binary) as data_size
            FROM pa_data_group
            WHERE verification_id = $1
            ORDER BY dg_number ASC
        )SQL";

        std::vector<std::string> params = {verificationId};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        // Apply field name conversion for all rows
        Json::Value dataArray = Json::arrayValue;
        for (const auto& row : result) {
            dataArray.append(toCamelCase(row));
        }

        spdlog::debug("[DataGroupRepository] Found {} data groups for verification {}",
            dataArray.size(), verificationId);

        return dataArray;

    } catch (const std::exception& e) {
        spdlog::error("[DataGroupRepository] Find by verification ID failed: {}", e.what());
        throw;
    }
}

Json::Value DataGroupRepository::findById(const std::string& id) {
    spdlog::debug("[DataGroupRepository] Finding data group by ID: {}", id);

    try {
        const char* query = R"SQL(
            SELECT id, verification_id, dg_number, expected_hash, actual_hash,
                   hash_algorithm, hash_valid,
                   length(dg_binary) as data_size
            FROM pa_data_group
            WHERE id = $1
        )SQL";

        std::vector<std::string> params = {id};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::debug("[DataGroupRepository] Data group not found: {}", id);
            return Json::Value::null;
        }

        return toCamelCase(result[0]);

    } catch (const std::exception& e) {
        spdlog::error("[DataGroupRepository] Find by ID failed: {}", e.what());
        return Json::Value::null;
    }
}

std::string DataGroupRepository::insert(
    const icao::models::DataGroup& dg,
    const std::string& verificationId)
{
    spdlog::debug("[DataGroupRepository] Inserting data group {} for verification {}",
        dg.dgNumber, verificationId);

    try {
        // Extract DG number from string (supports "DG1" -> 1 or "1" -> 1)
        int dgNumber = 0;
        if (dg.dgNumber.find("DG") == 0) {
            dgNumber = std::stoi(dg.dgNumber.substr(2));
        } else {
            try { dgNumber = std::stoi(dg.dgNumber); } catch (...) {}
        }

        // Step 1: Generate UUID using database-specific function
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string uuidQuery;

        if (dbType == "postgres") {
            uuidQuery = "SELECT uuid_generate_v4()::text as id";
        } else {
            // Oracle: Convert SYS_GUID() to UUID format
            uuidQuery = "SELECT LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), "
                       "'([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', "
                       "'\\1-\\2-\\3-\\4-\\5')) as id FROM DUAL";
        }

        Json::Value uuidResult = queryExecutor_->executeQuery(uuidQuery, {});
        if (uuidResult.empty()) {
            throw std::runtime_error("Failed to generate UUID");
        }
        std::string generatedId = uuidResult[0]["id"].asString();

        // Step 2: Insert with generated UUID (no RETURNING clause needed)
        const char* insertQuery = R"SQL(
            INSERT INTO pa_data_group (
                id, verification_id, dg_number, expected_hash, actual_hash,
                hash_algorithm, hash_valid, dg_binary
            ) VALUES (
                $1, $2, $3, $4, $5, $6, $7, $8
            )
        )SQL";

        // Database-aware boolean formatting
        auto boolStr = [&dbType](bool val) -> std::string {
            return (dbType == "oracle") ? (val ? "1" : "0") : (val ? "true" : "false");
        };

        // Prepare parameters
        std::vector<std::string> params;
        params.push_back(generatedId);
        params.push_back(verificationId);
        params.push_back(std::to_string(dgNumber));
        params.push_back(dg.expectedHash);
        params.push_back(dg.actualHash);
        params.push_back(dg.hashAlgorithm);
        params.push_back(boolStr(dg.hashValid));

        // Handle binary data (empty if not present)
        // \\x prefix is required for both PostgreSQL (bytea hex) and Oracle (BLOB detection)
        std::string binaryData;
        if (dg.rawData.has_value() && !dg.rawData.value().empty()) {
            const auto& data = dg.rawData.value();
            std::ostringstream oss;
            oss << "\\x";  // Hex prefix: PostgreSQL bytea format + Oracle BLOB detection trigger
            for (uint8_t byte : data) {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            binaryData = oss.str();
        }
        params.push_back(binaryData);

        int rowsAffected = queryExecutor_->executeCommand(insertQuery, params);

        // Oracle may return 0 for successful INSERTs without RETURNING clause
        if (rowsAffected == 0 && dbType == "postgres") {
            throw std::runtime_error("Insert failed: no rows affected");
        }

        spdlog::info("[DataGroupRepository] Data group inserted with ID: {}", generatedId);
        return generatedId;

    } catch (const std::exception& e) {
        spdlog::error("[DataGroupRepository] Insert failed: {}", e.what());
        throw;
    }
}

int DataGroupRepository::deleteByVerificationId(const std::string& verificationId) {
    spdlog::debug("[DataGroupRepository] Deleting data groups for verification: {}", verificationId);

    try {
        const char* query = "DELETE FROM pa_data_group WHERE verification_id = $1";

        std::vector<std::string> params = {verificationId};
        int affectedRows = queryExecutor_->executeCommand(query, params);

        spdlog::debug("[DataGroupRepository] Deleted {} data groups", affectedRows);
        return affectedRows;

    } catch (const std::exception& e) {
        spdlog::error("[DataGroupRepository] Delete failed: {}", e.what());
        throw;
    }
}

// --- Helper Methods ---

Json::Value DataGroupRepository::toCamelCase(const Json::Value& dbRow) {
    // Field name mapping: snake_case (DB) -> camelCase (Frontend)
    static const std::map<std::string, std::string> fieldMapping = {
        {"id", "id"},
        {"verification_id", "verificationId"},
        {"dg_number", "dgNumber"},
        {"expected_hash", "expectedHash"},
        {"actual_hash", "actualHash"},
        {"hash_algorithm", "hashAlgorithm"},
        {"hash_valid", "hashValid"},
        {"dg_binary", "dgBinary"},
        {"data_size", "dataSize"}
    };

    Json::Value camelCaseRow;

    for (const auto& key : dbRow.getMemberNames()) {
        std::string camelKey = key;

        // Apply mapping if exists
        auto it = fieldMapping.find(key);
        if (it != fieldMapping.end()) {
            camelKey = it->second;
        }

        const Json::Value& value = dbRow[key];

        // Handle NULL values
        if (value.isNull()) {
            camelCaseRow[camelKey] = Json::Value::null;
            continue;
        }

        // Handle boolean fields (PostgreSQL returns 't'/'f' strings)
        if (key == "hash_valid") {
            if (value.isString()) {
                std::string strVal = value.asString();
                camelCaseRow[camelKey] = (strVal == "t" || strVal == "true" || strVal == "1");
            } else {
                camelCaseRow[camelKey] = value.asBool();
            }
        }
        // Handle numeric fields
        else if (key == "dg_number" || key == "data_size") {
            if (value.isString()) {
                camelCaseRow[camelKey] = std::stoi(value.asString());
            } else {
                camelCaseRow[camelKey] = value.asInt();
            }
        }
        // All other fields
        else {
            camelCaseRow[camelKey] = value;
        }
    }

    return camelCaseRow;
}

} // namespace repositories

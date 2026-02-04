// Suppress OTL library warnings (third-party code)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"

#include "oracle_query_executor.h"

#pragma GCC diagnostic pop

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <regex>
#include <sstream>

namespace common {

// ============================================================================
// Constructor
// ============================================================================

OracleQueryExecutor::OracleQueryExecutor(OracleConnectionPool* pool)
    : pool_(pool)
{
    if (!pool_) {
        throw std::invalid_argument("OracleQueryExecutor: pool cannot be nullptr");
    }
    spdlog::debug("[OracleQueryExecutor] Initialized");
}

// ============================================================================
// Public Interface Implementation
// ============================================================================

Json::Value OracleQueryExecutor::executeQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] Executing SELECT query");

    try {
        // Acquire connection from pool (RAII)
        auto conn = pool_->acquire();
        if (!conn.isValid()) {
            throw std::runtime_error("Failed to acquire Oracle connection from pool");
        }

        // Convert PostgreSQL placeholders to Oracle format
        std::string oracleQuery = convertPlaceholders(query);

        // Get OTL connection (cast from void*)
        otl_connect* otl_conn = static_cast<otl_connect*>(conn.get());

        // Create OTL stream for query execution
        otl_stream otlStream;
        otlStream.open(1,  // Buffer size
                    oracleQuery.c_str(),
                    *otl_conn);

        // Bind parameters
        for (size_t i = 0; i < params.size(); ++i) {
            otlStream << params[i];
        }

        // Convert results to JSON
        Json::Value result = otlStreamToJson(otlStream);

        otlStream.close();
        return result;

    } catch (const otl_exception& e) {
        std::string error = std::string(reinterpret_cast<const char*>(e.msg));
        spdlog::error("[OracleQueryExecutor] OTL exception: {}", error);
        throw std::runtime_error("Oracle query failed: " + error);
    }

    // Should not reach here
    return Json::arrayValue;
}

int OracleQueryExecutor::executeCommand(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] Executing command");

    try {
        auto conn = pool_->acquire();
        if (!conn.isValid()) {
            throw std::runtime_error("Failed to acquire Oracle connection from pool");
        }

        std::string oracleQuery = convertPlaceholders(query);

        // Get OTL connection (cast from void*)
        otl_connect* otl_conn = static_cast<otl_connect*>(conn.get());

        otl_stream otlStream;
        otlStream.open(1, oracleQuery.c_str(), *otl_conn);

        // Bind parameters
        for (size_t i = 0; i < params.size(); ++i) {
            otlStream << params[i];
        }

        // Flush to execute the command
        otlStream.flush();

        // Get affected rows count
        long affectedRows = otlStream.get_rpc();

        otlStream.close();

        spdlog::debug("[OracleQueryExecutor] Command executed, affected rows: {}", affectedRows);
        return static_cast<int>(affectedRows);

    } catch (const otl_exception& e) {
        std::string error = std::string(reinterpret_cast<const char*>(e.msg));
        spdlog::error("[OracleQueryExecutor] OTL exception: {}", error);
        throw std::runtime_error("Oracle command failed: " + error);
    }

    // Should not reach here
    return 0;
}

Json::Value OracleQueryExecutor::executeScalar(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] Executing scalar query");

    try {
        auto conn = pool_->acquire();
        if (!conn.isValid()) {
            throw std::runtime_error("Failed to acquire Oracle connection from pool");
        }

        std::string oracleQuery = convertPlaceholders(query);

        // Get OTL connection (cast from void*)
        otl_connect* otl_conn = static_cast<otl_connect*>(conn.get());

        otl_stream otlStream;
        otlStream.open(1, oracleQuery.c_str(), *otl_conn);

        // Bind parameters
        for (size_t i = 0; i < params.size(); ++i) {
            otlStream << params[i];
        }

        // Check if we have any rows
        if (otlStream.eof()) {
            otlStream.close();
            throw std::runtime_error("Scalar query returned no rows");
        }

        // Get column count
        int colCount = 0;
        otl_column_desc* desc = otlStream.describe_select(colCount);
        if (colCount != 1) {
            otlStream.close();
            throw std::runtime_error("Scalar query must return exactly one column");
        }

        // Read single value
        std::string value;
        otlStream >> value;

        otlStream.close();

        // Try to determine type and convert
        // For scalar queries, we assume numeric if it parses as number
        try {
            size_t pos;
            int intVal = std::stoi(value, &pos);
            if (pos == value.length()) {
                return Json::Value(intVal);
            }
        } catch (...) {
            // Not an integer, try double
            try {
                size_t pos;
                double doubleVal = std::stod(value, &pos);
                if (pos == value.length()) {
                    return Json::Value(doubleVal);
                }
            } catch (...) {
                // Not a number, return as string
            }
        }

        return Json::Value(value);

    } catch (const otl_exception& e) {
        std::string error = std::string(reinterpret_cast<const char*>(e.msg));
        spdlog::error("[OracleQueryExecutor] OTL exception: {}", error);
        throw std::runtime_error("Oracle scalar query failed: " + error);
    }

    // Should not reach here
    return Json::nullValue;
}

// ============================================================================
// Private Implementation
// ============================================================================

std::string OracleQueryExecutor::convertPlaceholders(const std::string& query)
{
    // Convert PostgreSQL $1, $2, $3 to Oracle :1, :2, :3
    std::string result = query;
    std::regex placeholder_regex(R"(\$(\d+))");
    result = std::regex_replace(result, placeholder_regex, ":$1");

    spdlog::debug("[OracleQueryExecutor] Converted query: {}", result);
    return result;
}

Json::Value OracleQueryExecutor::otlStreamToJson(otl_stream& otlStream)
{
    Json::Value array = Json::arrayValue;

    // Get column descriptions
    int colCount = 0;
    otl_column_desc* desc = otlStream.describe_select(colCount);

    // Read all rows
    while (!otlStream.eof()) {
        Json::Value row;

        for (int i = 0; i < colCount; ++i) {
            std::string colName = desc[i].name;

            // Read value as string (OTL handles conversion)
            std::string value;

            try {
                otlStream >> value;

                // Check for NULL (OTL uses empty string for NULL with indicators)
                if (otlStream.is_null()) {
                    row[colName] = Json::nullValue;
                } else {
                    // Try type conversion based on Oracle type
                    int oracleType = desc[i].dbtype;

                    // Oracle type codes (from otl_value.h)
                    // SQLT_INT = 3, SQLT_FLT = 4, SQLT_NUM = 2
                    if (oracleType == 2 || oracleType == 3) {  // NUMBER or INT
                        try {
                            // Try integer first
                            int intVal = std::stoi(value);
                            row[colName] = intVal;
                        } catch (...) {
                            // If integer fails, try double
                            try {
                                double doubleVal = std::stod(value);
                                row[colName] = doubleVal;
                            } catch (...) {
                                // If both fail, use string
                                row[colName] = value;
                            }
                        }
                    } else {
                        // Default: string
                        row[colName] = value;
                    }
                }
            } catch (const otl_exception& e) {
                spdlog::warn("[OracleQueryExecutor] Error reading column {}: {}",
                            colName, reinterpret_cast<const char*>(e.msg));
                row[colName] = Json::nullValue;
            }
        }

        array.append(row);
    }

    return array;
}

// Currently unused - logic is in otlStreamToJson above
// Kept for potential future refactoring
// Json::Value OracleQueryExecutor::otlValueToJson(otl_stream& otlStream, int colIndex, int colType)
// {
//     return Json::nullValue;
// }

} // namespace common

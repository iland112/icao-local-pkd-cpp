#include "postgresql_query_executor.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>

namespace common {

// ============================================================================
// Constructor
// ============================================================================

PostgreSQLQueryExecutor::PostgreSQLQueryExecutor(DbConnectionPool* pool)
    : pool_(pool)
{
    if (!pool_) {
        throw std::invalid_argument("PostgreSQLQueryExecutor: pool cannot be nullptr");
    }
    spdlog::debug("[PostgreSQLQueryExecutor] Initialized");
}

// ============================================================================
// Public Interface Implementation
// ============================================================================

Json::Value PostgreSQLQueryExecutor::executeQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[PostgreSQLQueryExecutor] Executing SELECT query");
    spdlog::debug("[PostgreSQLQueryExecutor] Query: {}", query);
    spdlog::debug("[PostgreSQLQueryExecutor] Params count: {}", params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        spdlog::debug("[PostgreSQLQueryExecutor] Param[{}]: '{}'", i, params[i]);
    }

    // Acquire connection from pool (RAII - held until function returns)
    auto conn = pool_->acquire();
    if (!conn.isValid()) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Failed to acquire connection from pool");
    }

    // Prepare parameter values for libpq
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.empty() ? nullptr : param.c_str());
    }

    // Execute parameterized query
    PGresult* res = PQexecParams(
        conn.get(),
        query.c_str(),
        params.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query execution failed: null result");
    }

    // Check execution status
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(conn.get());
        PQclear(res);
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query failed: " + error);
    }

    // Convert to JSON while connection is still valid
    Json::Value result = pgResultToJson(res);
    PQclear(res);
    // conn automatically returned to pool here

    return result;
}

int PostgreSQLQueryExecutor::executeCommand(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[PostgreSQLQueryExecutor] Executing command");

    // Acquire connection from pool (RAII - held until function returns)
    auto conn = pool_->acquire();
    if (!conn.isValid()) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Failed to acquire connection from pool");
    }

    // Prepare parameter values for libpq
    // Empty strings are treated as NULL (consistent with Oracle behavior)
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.empty() ? nullptr : param.c_str());
    }

    // Execute parameterized query
    PGresult* res = PQexecParams(
        conn.get(),
        query.c_str(),
        params.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query execution failed: null result");
    }

    // Check execution status
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(conn.get());
        PQclear(res);
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query failed: " + error);
    }

    // Get number of affected rows
    const char* affectedRowsStr = PQcmdTuples(res);
    int affectedRows = 0;
    if (affectedRowsStr && affectedRowsStr[0] != '\0') {
        affectedRows = std::atoi(affectedRowsStr);
    }

    PQclear(res);
    // conn automatically returned to pool here

    spdlog::debug("[PostgreSQLQueryExecutor] Command executed, affected rows: {}", affectedRows);
    return affectedRows;
}

Json::Value PostgreSQLQueryExecutor::executeScalar(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[PostgreSQLQueryExecutor] Executing scalar query");

    // Acquire connection from pool (RAII - held until function returns)
    auto conn = pool_->acquire();
    if (!conn.isValid()) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Failed to acquire connection from pool");
    }

    // Prepare parameter values for libpq
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.empty() ? nullptr : param.c_str());
    }

    // Execute parameterized query
    PGresult* res = PQexecParams(
        conn.get(),
        query.c_str(),
        params.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query execution failed: null result");
    }

    // Check execution status
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(conn.get());
        PQclear(res);
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query failed: " + error);
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        throw std::runtime_error("Scalar query returned no rows");
    }

    if (PQnfields(res) != 1) {
        PQclear(res);
        throw std::runtime_error("Scalar query must return exactly one column");
    }

    // Get single value
    if (PQgetisnull(res, 0, 0)) {
        PQclear(res);
        return Json::nullValue;
    }

    const char* value = PQgetvalue(res, 0, 0);
    Oid type = PQftype(res, 0);

    Json::Value result;
    if (type == 23 || type == 20) {  // INT4 or INT8
        result = std::atoi(value);
    } else if (type == 700 || type == 701) {  // FLOAT4 or FLOAT8
        result = std::atof(value);
    } else if (type == 16) {  // BOOL
        result = (value[0] == 't');
    } else {
        result = value;
    }

    PQclear(res);
    // conn automatically returned to pool here
    return result;
}

// ============================================================================
// Private Implementation
// ============================================================================

PGresult* PostgreSQLQueryExecutor::executeRawQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    // Acquire connection from pool (RAII - automatically released on scope exit)
    auto conn = pool_->acquire();

    if (!conn.isValid()) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Failed to acquire connection from pool");
    }

    // Prepare parameter values for libpq
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.empty() ? nullptr : param.c_str());
    }

    // Execute parameterized query
    PGresult* res = PQexecParams(
        conn.get(),                    // Connection
        query.c_str(),                 // Query string
        params.size(),                 // Number of parameters
        nullptr,                       // Parameter types (nullptr = infer)
        paramValues.data(),            // Parameter values
        nullptr,                       // Parameter lengths (nullptr = text)
        nullptr,                       // Parameter formats (nullptr = text)
        0                              // Result format (0 = text)
    );

    if (!res) {
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query execution failed: null result");
    }

    // Check execution status
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(conn.get());
        PQclear(res);
        throw std::runtime_error("[PostgreSQLQueryExecutor] Query failed: " + error);
    }

    return res;
}

Json::Value PostgreSQLQueryExecutor::pgResultToJson(PGresult* res)
{
    Json::Value array = Json::arrayValue;

    int rows = PQntuples(res);
    int cols = PQnfields(res);

    spdlog::debug("[PostgreSQLQueryExecutor] pgResultToJson: rows={}, cols={}", rows, cols);

    for (int i = 0; i < rows; ++i) {
        Json::Value row;
        for (int j = 0; j < cols; ++j) {
            const char* fieldName = PQfname(res, j);

            // Handle NULL values
            if (PQgetisnull(res, i, j)) {
                row[fieldName] = Json::nullValue;
                continue;
            }

            const char* value = PQgetvalue(res, i, j);
            Oid type = PQftype(res, j);

            // Type conversion based on PostgreSQL type OID
            if (type == 23 || type == 20) {  // INT4 (23) or INT8 (20)
                row[fieldName] = std::atoi(value);
            } else if (type == 700 || type == 701) {  // FLOAT4 (700) or FLOAT8 (701)
                row[fieldName] = std::atof(value);
            } else if (type == 16) {  // BOOL (16)
                row[fieldName] = (value[0] == 't');
            } else {
                // Default: treat as string
                row[fieldName] = value;
            }
        }
        array.append(row);
    }

    return array;
}

} // namespace common

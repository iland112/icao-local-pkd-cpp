// Suppress OTL library warnings (third-party code)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"

#include "oracle_query_executor.h"

#pragma GCC diagnostic pop

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <mutex>

// Mutex for serializing OCI connection creation (OCI is not thread-safe during env setup)
static std::mutex g_ociConnectionMutex;

namespace common {

// ============================================================================
// Constructor & Destructor
// ============================================================================

OracleQueryExecutor::OracleQueryExecutor(OracleConnectionPool* pool)
    : pool_(pool), ociEnv_(nullptr), ociErr_(nullptr),
      ociSvcCtx_(nullptr), ociServer_(nullptr), ociSession_(nullptr)
{
    if (!pool_) {
        throw std::invalid_argument("OracleQueryExecutor: pool cannot be nullptr");
    }

    // Build connection string from environment variables
    const char* host = std::getenv("ORACLE_HOST");
    const char* port = std::getenv("ORACLE_PORT");
    const char* service = std::getenv("ORACLE_SERVICE_NAME");
    const char* user = std::getenv("ORACLE_USER");
    const char* password = std::getenv("ORACLE_PASSWORD");

    if (!host || !port || !service || !user || !password) {
        throw std::runtime_error("Missing Oracle connection environment variables");
    }

    // Oracle connection string format: user/password@host:port/service
    connString_ = std::string(user) + "/" + password + "@" + host + ":" + port + "/" + service;

    // Initialize OCI for stable VARCHAR2 TIMESTAMP handling
    try {
        initializeOCI();
        spdlog::debug("[OracleQueryExecutor] Initialized with OCI support");
    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI initialization failed: {}", e.what());
        throw;
    }
}

OracleQueryExecutor::~OracleQueryExecutor()
{
    cleanupOCI();
}

// ============================================================================
// Public Interface Implementation
// ============================================================================

Json::Value OracleQueryExecutor::executeQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] Executing SELECT query with thread-safe OCI connection");

    // Create thread-local OCI connection
    OciConnection ociConn;

    try {
        // Create new OCI connection for this thread
        createOciConnection(ociConn);

        // Convert PostgreSQL placeholders to OCI positional binding format
        std::string oracleQuery = query;
        std::regex pg_placeholder(R"(\$(\d+))");
        oracleQuery = std::regex_replace(oracleQuery, pg_placeholder, ":$1");

        // Convert PostgreSQL-specific NULLIF()::INTEGER to Oracle CASE expression
        // NULLIF(param, '')::INTEGER → CASE WHEN param IS NULL OR param = '' THEN NULL ELSE TO_NUMBER(param) END
        std::regex nullif_integer(R"(NULLIF\(([^,]+),\s*''\s*\)::INTEGER)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, nullif_integer, "CASE WHEN $1 IS NULL OR $1 = '' THEN NULL ELSE TO_NUMBER($1) END");

        // Convert LIMIT/OFFSET syntax
        std::regex limit_offset_regex(R"(\s+LIMIT\s+(\d+)\s+OFFSET\s+(\d+)\s*$)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, limit_offset_regex, " OFFSET $2 ROWS FETCH NEXT $1 ROWS ONLY");

        std::regex limit_regex(R"(\s+LIMIT\s+(\d+)\s*$)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, limit_regex, " FETCH FIRST $1 ROWS ONLY");

        // Handle DML with RETURNING clause
        // Oracle requires RETURNING ... INTO :bind_var with OCI_DATA_AT_EXEC callbacks,
        // which is extremely complex. Instead, strip the RETURNING clause and execute as DML.
        // Callers needing the returned ID should pre-generate UUID before INSERT.
        bool isDmlReturning = false;
        std::regex returning_strip(R"(\s+RETURNING\s+\w+(\s+INTO\s+:\d+)?\s*$)", std::regex::icase);
        if (std::regex_search(oracleQuery, returning_strip)) {
            isDmlReturning = true;
            oracleQuery = std::regex_replace(oracleQuery, returning_strip, "");
            spdlog::info("[OracleQueryExecutor] Stripped RETURNING clause for Oracle DML");
        }

        // Log full query for debugging (split into chunks if too long)
        if (oracleQuery.length() <= 500) {
            spdlog::debug("[OracleQueryExecutor] Full OCI query: {}", oracleQuery);
        } else {
            spdlog::debug("[OracleQueryExecutor] OCI query part 1: {}", oracleQuery.substr(0, 500));
            spdlog::debug("[OracleQueryExecutor] OCI query part 2: {}", oracleQuery.substr(500, 500));
            if (oracleQuery.length() > 1000) {
                spdlog::debug("[OracleQueryExecutor] OCI query part 3: {}", oracleQuery.substr(1000));
            }
        }

        // Allocate statement handle using OCI directly
        OCIStmt* stmt = nullptr;
        sword status = OCIHandleAlloc(ociConn.env, reinterpret_cast<void**>(&stmt),
                                      OCI_HTYPE_STMT, 0, nullptr);
        if (status != OCI_SUCCESS) {
            throw std::runtime_error("Failed to allocate OCI statement handle");
        }

        // Prepare statement
        status = OCIStmtPrepare(stmt, ociConn.err,
                               reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                               oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to prepare OCI statement");
        }

        // Bind parameters using OCIBindByName (handles duplicate named binds correctly)
        // OCIBindByPos counts by occurrence, not by unique name, causing ORA-01008
        // when the same :N appears multiple times (e.g., CASE WHEN :18 ... :18 ... :18 ...)
        std::vector<char*> paramBuffers;  // Keep string buffers alive during execution
        std::vector<std::vector<uint8_t>> binaryBuffers;  // Keep binary buffers alive
        std::vector<std::string> bindNames;  // Keep bind name strings alive

        for (size_t i = 0; i < params.size(); ++i) {
            OCIBind* bind = nullptr;

            // Create bind variable name ":1", ":2", etc.
            bindNames.push_back(":" + std::to_string(i + 1));
            const std::string& bindName = bindNames.back();

            // Detect PostgreSQL bytea hex format (\\x...) for BLOB columns
            bool isBinary = false;
            size_t hexStart = 0;
            if (params[i].size() > 3 && params[i][0] == '\\' && params[i][1] == '\\' && params[i][2] == 'x') {
                isBinary = true;
                hexStart = 3;
            } else if (params[i].size() > 2 && params[i][0] == '\\' && params[i][1] == 'x') {
                isBinary = true;
                hexStart = 2;
            }

            if (isBinary) {
                // Decode hex string to raw binary bytes for BLOB binding
                std::vector<uint8_t> rawBytes;
                rawBytes.reserve((params[i].size() - hexStart) / 2);
                for (size_t j = hexStart; j + 1 < params[i].size(); j += 2) {
                    char hex[3] = { params[i][j], params[i][j + 1], '\0' };
                    rawBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
                }

                binaryBuffers.push_back(std::move(rawBytes));
                auto& buf = binaryBuffers.back();

                spdlog::debug("[OracleQueryExecutor] Param {} ({}) bound as BLOB ({} bytes)",
                             i + 1, bindName, buf.size());

                status = OCIBindByName(stmt, &bind, ociConn.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      buf.data(), static_cast<sb4>(buf.size()),
                                      SQLT_LBI, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            } else {
                // Standard string binding
                char* buffer = new char[params[i].length() + 1];
                strcpy(buffer, params[i].c_str());
                paramBuffers.push_back(buffer);

                status = OCIBindByName(stmt, &bind, ociConn.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      buffer, params[i].length() + 1,
                                      SQLT_STR, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            }

            if (status != OCI_SUCCESS) {
                for (char* buf : paramBuffers) delete[] buf;
                OCIHandleFree(stmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to bind parameter " + std::to_string(i + 1));
            }
        }

        // Execute statement
        // For DML with RETURNING (INSERT/UPDATE/DELETE ... RETURNING), iters must be 1
        // For regular SELECT, iters must be 0
        ub4 iters = isDmlReturning ? 1 : 0;
        status = OCIStmtExecute(ociConn.svcCtx, stmt, ociConn.err, iters, 0,
                               nullptr, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
            // Get detailed Oracle error message
            char errbuf[512];
            sb4 errcode = 0;
            OCIErrorGet(ociConn.err, 1, nullptr, &errcode,
                       reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);

            for (char* buf : paramBuffers) delete[] buf;
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error(std::string("OCI statement execution failed (code ") +
                                   std::to_string(errcode) + "): " + errbuf);
        }

        // For DML with stripped RETURNING: commit and return empty result
        // The caller should have pre-generated the UUID before INSERT
        if (isDmlReturning) {
            OCITransCommit(ociConn.svcCtx, ociConn.err, OCI_DEFAULT);
            for (char* buf : paramBuffers) delete[] buf;
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            disconnectOci(ociConn);
            freeOciHandles(ociConn);
            spdlog::info("[OracleQueryExecutor] DML executed successfully (RETURNING stripped)");
            return Json::arrayValue;
        }

        // Get column count
        ub4 colCount = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &colCount, nullptr,
                  OCI_ATTR_PARAM_COUNT, ociConn.err);

        // Build JSON result
        Json::Value result = Json::arrayValue;

        // Define output buffers for each column
        std::vector<char*> colBuffers(colCount);
        std::vector<sb2> indicators(colCount);
        std::vector<std::string> colNames(colCount);

        for (ub4 i = 0; i < colCount; ++i) {
            // Get column descriptor
            OCIParam* col = nullptr;
            OCIParamGet(stmt, OCI_HTYPE_STMT, ociConn.err, reinterpret_cast<void**>(&col), i + 1);

            // Get column name
            OraText* colName = nullptr;
            ub4 colNameLen = 0;
            OCIAttrGet(col, OCI_DTYPE_PARAM, &colName, &colNameLen,
                      OCI_ATTR_NAME, ociConn.err);
            std::string columnName(reinterpret_cast<char*>(colName), colNameLen);

            // Convert Oracle's UPPERCASE column names to lowercase for consistency
            std::transform(columnName.begin(), columnName.end(), columnName.begin(),
                          [](unsigned char c){ return std::tolower(c); });
            colNames[i] = columnName;

            // Allocate buffer (4000 bytes for VARCHAR2)
            colBuffers[i] = new char[4001];
            memset(colBuffers[i], 0, 4001);

            // Define column
            OCIDefine* def = nullptr;
            OCIDefineByPos(stmt, &def, ociConn.err, i + 1,
                          colBuffers[i], 4000, SQLT_STR,
                          &indicators[i], nullptr, nullptr, OCI_DEFAULT);
        }

        // Fetch all rows
        while (true) {
            status = OCIStmtFetch2(stmt, ociConn.err, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
            if (status == OCI_NO_DATA) break;
            if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) break;

            Json::Value row;
            for (ub4 i = 0; i < colCount; ++i) {
                if (indicators[i] == -1) {
                    spdlog::debug("[OracleQueryExecutor] Column {} ({}) is NULL (indicator=-1)", i, colNames[i]);
                    row[colNames[i]] = Json::nullValue;
                } else {
                    std::string value(colBuffers[i]);
                    spdlog::debug("[OracleQueryExecutor] Column {} ({}) = '{}' (indicator={}, buflen={})",
                                 i, colNames[i], value, indicators[i], value.length());
                    row[colNames[i]] = Json::Value(colBuffers[i]);
                }
            }
            result.append(row);
        }

        // Cleanup
        for (char* buf : colBuffers) delete[] buf;
        for (char* buf : paramBuffers) delete[] buf;
        OCIHandleFree(stmt, OCI_HTYPE_STMT);

        // Disconnect and free OCI handles
        disconnectOci(ociConn);
        freeOciHandles(ociConn);

        spdlog::debug("[OracleQueryExecutor] OCI query returned {} rows", result.size());
        return result;

    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI exception: {}", e.what());
        // Ensure cleanup on exception
        disconnectOci(ociConn);
        freeOciHandles(ociConn);
        throw;
    }

    return Json::arrayValue;
}

int OracleQueryExecutor::executeCommand(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] Executing command with thread-safe OCI connection");

    // Create thread-local OCI connection
    OciConnection ociConn;

    try {
        // Create new OCI connection for this thread
        createOciConnection(ociConn);

        // Convert PostgreSQL syntax to Oracle syntax
        std::string oracleQuery = query;
        std::regex pg_placeholder(R"(\$(\d+))");
        oracleQuery = std::regex_replace(oracleQuery, pg_placeholder, ":$1");

        // Convert NULLIF()::INTEGER to Oracle CASE expression
        std::regex nullif_integer(R"(NULLIF\(([^,]+),\s*''\s*\)::INTEGER)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, nullif_integer, "CASE WHEN $1 IS NULL OR $1 = '' THEN NULL ELSE TO_NUMBER($1) END");

        // Convert PostgreSQL CURRENT_TIMESTAMP to Oracle SYSTIMESTAMP
        std::regex current_timestamp_regex(R"(CURRENT_TIMESTAMP)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, current_timestamp_regex, "SYSTIMESTAMP");

        // Convert PostgreSQL NOW() to Oracle SYSDATE
        std::regex now_regex(R"(NOW\(\))", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, now_regex, "SYSDATE");

        // Remove PostgreSQL type casts (::typename)
        std::regex typecast_regex(R"(::[a-zA-Z_][a-zA-Z0-9_]*)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, typecast_regex, "");

        // Handle PostgreSQL ON CONFLICT clause (not supported in Oracle)
        // ON CONFLICT (...) DO NOTHING → strip entirely (let ORA-00001 propagate)
        // ON CONFLICT (...) DO UPDATE SET ... → strip entirely (duplicate handling deferred)
        std::regex on_conflict_regex(R"(\s+ON\s+CONFLICT\s*\([^)]*\)\s+DO\s+(NOTHING|UPDATE\s+SET\s+.*)$)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, on_conflict_regex, "");

        spdlog::debug("[OracleQueryExecutor] OCI command: {}", oracleQuery.substr(0, 300));

        // Allocate OCI statement handle
        OCIStmt* stmt = nullptr;
        sword status = OCIHandleAlloc(ociConn.env, reinterpret_cast<void**>(&stmt),
                                      OCI_HTYPE_STMT, 0, nullptr);
        if (status != OCI_SUCCESS) {
            throw std::runtime_error("Failed to allocate OCI statement handle");
        }

        // Prepare statement
        status = OCIStmtPrepare(stmt, ociConn.err,
                               reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                               oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to prepare OCI statement");
        }

        // Bind parameters using OCIBindByName (handles duplicate named binds correctly)
        std::vector<char*> paramBuffers;
        std::vector<std::vector<uint8_t>> binaryBuffers;  // Keep binary buffers alive
        std::vector<std::string> bindNames;  // Keep bind name strings alive

        for (size_t i = 0; i < params.size(); ++i) {
            OCIBind* bind = nullptr;

            // Create bind variable name ":1", ":2", etc.
            bindNames.push_back(":" + std::to_string(i + 1));
            const std::string& bindName = bindNames.back();

            // Detect PostgreSQL bytea hex format (\\x...) for BLOB columns
            bool isBinary = false;
            size_t hexStart = 0;
            if (params[i].size() > 3 && params[i][0] == '\\' && params[i][1] == '\\' && params[i][2] == 'x') {
                isBinary = true;
                hexStart = 3;
            } else if (params[i].size() > 2 && params[i][0] == '\\' && params[i][1] == 'x') {
                isBinary = true;
                hexStart = 2;
            }

            if (isBinary) {
                // Decode hex string to raw binary bytes for BLOB binding
                std::vector<uint8_t> rawBytes;
                rawBytes.reserve((params[i].size() - hexStart) / 2);
                for (size_t j = hexStart; j + 1 < params[i].size(); j += 2) {
                    char hex[3] = { params[i][j], params[i][j + 1], '\0' };
                    rawBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
                }

                binaryBuffers.push_back(std::move(rawBytes));
                auto& buf = binaryBuffers.back();

                spdlog::debug("[OracleQueryExecutor] Param {} ({}) bound as BLOB ({} bytes)",
                             i + 1, bindName, buf.size());

                status = OCIBindByName(stmt, &bind, ociConn.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      buf.data(), static_cast<sb4>(buf.size()),
                                      SQLT_LBI, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            } else {
                // Standard string binding
                char* buffer = new char[params[i].length() + 1];
                strcpy(buffer, params[i].c_str());
                paramBuffers.push_back(buffer);

                status = OCIBindByName(stmt, &bind, ociConn.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      buffer, params[i].length() + 1,
                                      SQLT_STR, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            }

            if (status != OCI_SUCCESS) {
                for (char* buf : paramBuffers) delete[] buf;
                OCIHandleFree(stmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to bind parameter " + std::to_string(i + 1));
            }
        }

        // Execute statement (iters=1 for DML commands)
        status = OCIStmtExecute(ociConn.svcCtx, stmt, ociConn.err, 1, 0,
                               nullptr, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
            // Get error message
            char errbuf[512];
            sb4 errcode = 0;
            OCIErrorGet(ociConn.err, 1, nullptr, &errcode,
                       reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);

            for (char* buf : paramBuffers) delete[] buf;
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error(std::string("OCI statement execution failed: ") + errbuf);
        }

        // Get affected rows count
        ub4 affectedRows = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &affectedRows, nullptr,
                  OCI_ATTR_ROW_COUNT, ociConn.err);

        // Commit transaction
        OCITransCommit(ociConn.svcCtx, ociConn.err, OCI_DEFAULT);

        // Cleanup buffers
        for (char* buf : paramBuffers) delete[] buf;
        OCIHandleFree(stmt, OCI_HTYPE_STMT);

        // Disconnect and free OCI handles
        disconnectOci(ociConn);
        freeOciHandles(ociConn);

        spdlog::debug("[OracleQueryExecutor] Command executed, affected rows: {}", affectedRows);
        return static_cast<int>(affectedRows);

    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI exception: {}", e.what());
        // Ensure cleanup on exception
        disconnectOci(ociConn);
        freeOciHandles(ociConn);
        throw;
    }

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
        otlStream.open(50, oracleQuery.c_str(), *otl_conn);  // Buffer size increased

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

        // Read single value based on Oracle data type
        Json::Value result;
        int dbtype = desc[0].dbtype;

        // Oracle NUMBER types (including COUNT(*), SUM, etc.)
        // OTL maps NUMBER to otl_var_int, otl_var_long, otl_var_double depending on precision
        if (dbtype == otl_var_int || dbtype == otl_var_unsigned_int ||
            dbtype == otl_var_long_int || dbtype == otl_var_bigint) {
            long value;
            otlStream >> value;
            result = Json::Value(static_cast<int>(value));
        }
        else if (dbtype == otl_var_double || dbtype == otl_var_float) {
            double value;
            otlStream >> value;
            result = Json::Value(value);
        }
        else if (dbtype == otl_var_timestamp) {
            // Oracle TIMESTAMP columns - read as string (Oracle auto-converts)
            std::string value;
            otlStream >> value;
            result = Json::Value(value);
        }
        else {
            // VARCHAR2, CHAR, DATE, etc. - read as string
            std::string value;
            otlStream >> value;
            result = Json::Value(value);
        }

        otlStream.close();
        return result;

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
    // Convert PostgreSQL $1, $2, $3 to Oracle :v1<char[4000]>, :v2<char[4000]>, :v3<char[4000]>
    // Note: OTL requires typed placeholders for SELECT queries
    std::string result = query;
    std::regex placeholder_regex(R"(\$(\d+))");
    result = std::regex_replace(result, placeholder_regex, ":v$1<char[4000]>");

    // Convert LIMIT ... OFFSET to Oracle syntax (order: LIMIT before OFFSET)
    // Pattern: LIMIT <number> OFFSET <number>
    std::regex limit_offset_regex(R"(\s+LIMIT\s+(\d+|:v\d+<char\[4000\]>)\s+OFFSET\s+(\d+|:v\d+<char\[4000\]>)\s*$)", std::regex::icase);
    result = std::regex_replace(result, limit_offset_regex, " OFFSET $2 ROWS FETCH NEXT $1 ROWS ONLY");

    // Convert OFFSET ... LIMIT to OFFSET ... FETCH FIRST (order: OFFSET before LIMIT)
    // Pattern: OFFSET <number> LIMIT <number>
    std::regex offset_limit_regex(R"(\s+OFFSET\s+(\d+|:v\d+<char\[4000\]>)\s+LIMIT\s+(\d+|:v\d+<char\[4000\]>)\s*$)", std::regex::icase);
    result = std::regex_replace(result, offset_limit_regex, " OFFSET $1 ROWS FETCH NEXT $2 ROWS ONLY");

    // Convert LIMIT clause alone to Oracle FETCH FIRST syntax
    // Pattern: LIMIT <number> or LIMIT :param (must be after LIMIT...OFFSET patterns!)
    std::regex limit_regex(R"(\s+LIMIT\s+(\d+|:v\d+<char\[4000\]>)\s*$)", std::regex::icase);
    result = std::regex_replace(result, limit_regex, " FETCH FIRST $1 ROWS ONLY");

    // Convert PostgreSQL NOW() function to Oracle SYSDATE
    std::regex now_regex(R"(NOW\(\))", std::regex::icase);
    result = std::regex_replace(result, now_regex, "SYSDATE");

    // Convert PostgreSQL CURRENT_TIMESTAMP to Oracle SYSTIMESTAMP
    std::regex current_timestamp_regex(R"(CURRENT_TIMESTAMP)", std::regex::icase);
    result = std::regex_replace(result, current_timestamp_regex, "SYSTIMESTAMP");

    // Remove PostgreSQL type casts (::typename) - Oracle doesn't support this syntax
    // Pattern: ::type_name (e.g., ::jsonb, ::text, ::integer)
    std::regex typecast_regex(R"(::[a-zA-Z_][a-zA-Z0-9_]*)", std::regex::icase);
    result = std::regex_replace(result, typecast_regex, "");

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
            int dbtype = desc[i].dbtype;

            // Debug: log column name and type
            spdlog::debug("[OracleQueryExecutor] Column '{}' has dbtype={}", colName, dbtype);

            try {
                // Read value based on Oracle data type to avoid "Incompatible data types" error
                // Oracle NUMBER types (including COUNT(*), SUM, etc.)
                if (dbtype == otl_var_int || dbtype == otl_var_unsigned_int ||
                    dbtype == otl_var_long_int || dbtype == otl_var_bigint) {
                    long value;
                    otlStream >> value;

                    if (otlStream.is_null()) {
                        row[colName] = Json::nullValue;
                    } else {
                        row[colName] = Json::Value(static_cast<int>(value));
                    }
                }
                else if (dbtype == otl_var_double || dbtype == otl_var_float) {
                    double value;
                    otlStream >> value;

                    if (otlStream.is_null()) {
                        row[colName] = Json::nullValue;
                    } else {
                        row[colName] = Json::Value(value);
                    }
                }
                else {
                    // VARCHAR2, CHAR, CLOB, DATE, TIMESTAMP, etc. - read as string
                    // Note: TIMESTAMP handling removed - treating as string
                    std::string value;
                    otlStream >> value;

                    if (otlStream.is_null()) {
                        row[colName] = Json::nullValue;
                    } else {
                        row[colName] = Json::Value(value);
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

// ============================================================================
// OCI Thread-Safe Connection Management (Per-Query)
// ============================================================================

void OracleQueryExecutor::createOciConnection(OciConnection& conn)
{
    // Serialize OCI connection creation to prevent SIGSEGV from concurrent env setup
    std::lock_guard<std::mutex> lock(g_ociConnectionMutex);

    sword status;

    // Create OCI environment handle (OCI_THREADED for multi-threaded safety)
    status = OCIEnvCreate(&conn.env, OCI_THREADED, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    if (status != OCI_SUCCESS) {
        throw std::runtime_error("Failed to create OCI environment");
    }

    // Allocate OCI error handle
    status = OCIHandleAlloc(conn.env, reinterpret_cast<void**>(&conn.err),
                           OCI_HTYPE_ERROR, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(conn.env, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI error handle");
    }

    // Allocate service context handle
    status = OCIHandleAlloc(conn.env, reinterpret_cast<void**>(&conn.svcCtx),
                           OCI_HTYPE_SVCCTX, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        OCIHandleFree(conn.env, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI service context");
    }

    // Allocate server handle
    status = OCIHandleAlloc(conn.env, reinterpret_cast<void**>(&conn.server),
                           OCI_HTYPE_SERVER, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(conn.svcCtx, OCI_HTYPE_SVCCTX);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        OCIHandleFree(conn.env, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI server handle");
    }

    // Allocate session handle
    status = OCIHandleAlloc(conn.env, reinterpret_cast<void**>(&conn.session),
                           OCI_HTYPE_SESSION, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(conn.server, OCI_HTYPE_SERVER);
        OCIHandleFree(conn.svcCtx, OCI_HTYPE_SVCCTX);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        OCIHandleFree(conn.env, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI session handle");
    }

    // Connect to Oracle server (parse connection string for host:port/service)
    size_t atPos = connString_.find('@');
    if (atPos == std::string::npos) {
        freeOciHandles(conn);
        throw std::runtime_error("Invalid connection string format (missing @)");
    }

    std::string dbString = connString_.substr(atPos + 1); // host:port/service
    status = OCIServerAttach(conn.server, conn.err,
                            reinterpret_cast<const OraText*>(dbString.c_str()),
                            dbString.length(), OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        freeOciHandles(conn);
        throw std::runtime_error("Failed to attach to Oracle server");
    }

    // Set server handle in service context
    status = OCIAttrSet(conn.svcCtx, OCI_HTYPE_SVCCTX, conn.server, 0,
                       OCI_ATTR_SERVER, conn.err);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(conn.server, conn.err, OCI_DEFAULT);
        freeOciHandles(conn);
        throw std::runtime_error("Failed to set server in service context");
    }

    // Parse username and password from connection string
    std::string credentials = connString_.substr(0, atPos); // user/password
    size_t slashPos = credentials.find('/');
    if (slashPos == std::string::npos) {
        OCIServerDetach(conn.server, conn.err, OCI_DEFAULT);
        freeOciHandles(conn);
        throw std::runtime_error("Invalid connection string format (missing /)");
    }

    std::string username = credentials.substr(0, slashPos);
    std::string password = credentials.substr(slashPos + 1);

    // Set username in session
    status = OCIAttrSet(conn.session, OCI_HTYPE_SESSION,
                       const_cast<char*>(username.c_str()), username.length(),
                       OCI_ATTR_USERNAME, conn.err);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(conn.server, conn.err, OCI_DEFAULT);
        freeOciHandles(conn);
        throw std::runtime_error("Failed to set username");
    }

    // Set password in session
    status = OCIAttrSet(conn.session, OCI_HTYPE_SESSION,
                       const_cast<char*>(password.c_str()), password.length(),
                       OCI_ATTR_PASSWORD, conn.err);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(conn.server, conn.err, OCI_DEFAULT);
        freeOciHandles(conn);
        throw std::runtime_error("Failed to set password");
    }

    // Begin session
    status = OCISessionBegin(conn.svcCtx, conn.err, conn.session,
                            OCI_CRED_RDBMS, OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(conn.server, conn.err, OCI_DEFAULT);
        freeOciHandles(conn);
        throw std::runtime_error("Failed to begin Oracle session");
    }

    // Set session handle in service context
    status = OCIAttrSet(conn.svcCtx, OCI_HTYPE_SVCCTX, conn.session, 0,
                       OCI_ATTR_SESSION, conn.err);
    if (status != OCI_SUCCESS) {
        OCISessionEnd(conn.svcCtx, conn.err, conn.session, OCI_DEFAULT);
        OCIServerDetach(conn.server, conn.err, OCI_DEFAULT);
        freeOciHandles(conn);
        throw std::runtime_error("Failed to set session in service context");
    }

    // Set NLS session parameters for consistent date/timestamp handling
    // OpenSSL ASN1_TIME_print outputs "Jan 15 10:30:00 2024 GMT" format
    // We convert to ISO format in repository code, so set Oracle to expect ISO
    {
        OCIStmt* nlsStmt = nullptr;
        sword nlsStatus = OCIHandleAlloc(conn.env, reinterpret_cast<void**>(&nlsStmt),
                                          OCI_HTYPE_STMT, 0, nullptr);
        if (nlsStatus == OCI_SUCCESS) {
            // Set timestamp format to ISO 8601
            const char* nlsTs = "ALTER SESSION SET NLS_TIMESTAMP_FORMAT = 'YYYY-MM-DD HH24:MI:SS'";
            nlsStatus = OCIStmtPrepare(nlsStmt, conn.err,
                                       reinterpret_cast<const OraText*>(nlsTs),
                                       strlen(nlsTs), OCI_NTV_SYNTAX, OCI_DEFAULT);
            if (nlsStatus == OCI_SUCCESS) {
                OCIStmtExecute(conn.svcCtx, nlsStmt, conn.err, 1, 0, nullptr, nullptr, OCI_DEFAULT);
            }

            // Also set date format for DATE columns
            const char* nlsDt = "ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD HH24:MI:SS'";
            nlsStatus = OCIStmtPrepare(nlsStmt, conn.err,
                                       reinterpret_cast<const OraText*>(nlsDt),
                                       strlen(nlsDt), OCI_NTV_SYNTAX, OCI_DEFAULT);
            if (nlsStatus == OCI_SUCCESS) {
                OCIStmtExecute(conn.svcCtx, nlsStmt, conn.err, 1, 0, nullptr, nullptr, OCI_DEFAULT);
            }

            OCIHandleFree(nlsStmt, OCI_HTYPE_STMT);
        }
    }

    spdlog::debug("[OracleQueryExecutor] Created new OCI connection for current thread");
}

void OracleQueryExecutor::disconnectOci(OciConnection& conn)
{
    // End session
    if (conn.session && conn.svcCtx && conn.err) {
        OCISessionEnd(conn.svcCtx, conn.err, conn.session, OCI_DEFAULT);
    }

    // Detach from server
    if (conn.server && conn.err) {
        OCIServerDetach(conn.server, conn.err, OCI_DEFAULT);
    }
}

void OracleQueryExecutor::freeOciHandles(OciConnection& conn)
{
    if (conn.session) {
        OCIHandleFree(conn.session, OCI_HTYPE_SESSION);
        conn.session = nullptr;
    }

    if (conn.server) {
        OCIHandleFree(conn.server, OCI_HTYPE_SERVER);
        conn.server = nullptr;
    }

    if (conn.svcCtx) {
        OCIHandleFree(conn.svcCtx, OCI_HTYPE_SVCCTX);
        conn.svcCtx = nullptr;
    }

    if (conn.err) {
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        conn.err = nullptr;
    }

    if (conn.env) {
        OCIHandleFree(conn.env, OCI_HTYPE_ENV);
        conn.env = nullptr;
    }
}

// ============================================================================
// OCI Implementation (Legacy - for executeQueryWithOCI)
// ============================================================================

void OracleQueryExecutor::initializeOCI()
{
    sword status;

    // Create OCI environment handle (OCI_THREADED for multi-threaded safety)
    status = OCIEnvCreate(&ociEnv_, OCI_THREADED, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    if (status != OCI_SUCCESS) {
        throw std::runtime_error("Failed to create OCI environment");
    }

    // Allocate OCI error handle
    status = OCIHandleAlloc(ociEnv_, reinterpret_cast<void**>(&ociErr_),
                           OCI_HTYPE_ERROR, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(ociEnv_, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI error handle");
    }

    // Allocate service context handle
    status = OCIHandleAlloc(ociEnv_, reinterpret_cast<void**>(&ociSvcCtx_),
                           OCI_HTYPE_SVCCTX, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(ociErr_, OCI_HTYPE_ERROR);
        OCIHandleFree(ociEnv_, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI service context");
    }

    // Allocate server handle
    status = OCIHandleAlloc(ociEnv_, reinterpret_cast<void**>(&ociServer_),
                           OCI_HTYPE_SERVER, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(ociSvcCtx_, OCI_HTYPE_SVCCTX);
        OCIHandleFree(ociErr_, OCI_HTYPE_ERROR);
        OCIHandleFree(ociEnv_, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI server handle");
    }

    // Allocate session handle
    status = OCIHandleAlloc(ociEnv_, reinterpret_cast<void**>(&ociSession_),
                           OCI_HTYPE_SESSION, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(ociServer_, OCI_HTYPE_SERVER);
        OCIHandleFree(ociSvcCtx_, OCI_HTYPE_SVCCTX);
        OCIHandleFree(ociErr_, OCI_HTYPE_ERROR);
        OCIHandleFree(ociEnv_, OCI_HTYPE_ENV);
        throw std::runtime_error("Failed to allocate OCI session handle");
    }

    // Connect to Oracle server (parse connection string for host:port/service)
    size_t atPos = connString_.find('@');
    if (atPos == std::string::npos) {
        cleanupOCI();
        throw std::runtime_error("Invalid connection string format (missing @)");
    }

    std::string dbString = connString_.substr(atPos + 1); // host:port/service
    status = OCIServerAttach(ociServer_, ociErr_,
                            reinterpret_cast<const OraText*>(dbString.c_str()),
                            dbString.length(), OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        cleanupOCI();
        throw std::runtime_error("Failed to attach to Oracle server");
    }

    // Set server handle in service context
    status = OCIAttrSet(ociSvcCtx_, OCI_HTYPE_SVCCTX, ociServer_, 0,
                       OCI_ATTR_SERVER, ociErr_);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(ociServer_, ociErr_, OCI_DEFAULT);
        cleanupOCI();
        throw std::runtime_error("Failed to set server in service context");
    }

    // Parse username and password from connection string
    std::string credentials = connString_.substr(0, atPos); // user/password
    size_t slashPos = credentials.find('/');
    if (slashPos == std::string::npos) {
        OCIServerDetach(ociServer_, ociErr_, OCI_DEFAULT);
        cleanupOCI();
        throw std::runtime_error("Invalid connection string format (missing /)");
    }

    std::string username = credentials.substr(0, slashPos);
    std::string password = credentials.substr(slashPos + 1);

    // Set username in session
    status = OCIAttrSet(ociSession_, OCI_HTYPE_SESSION,
                       const_cast<char*>(username.c_str()), username.length(),
                       OCI_ATTR_USERNAME, ociErr_);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(ociServer_, ociErr_, OCI_DEFAULT);
        cleanupOCI();
        throw std::runtime_error("Failed to set username");
    }

    // Set password in session
    status = OCIAttrSet(ociSession_, OCI_HTYPE_SESSION,
                       const_cast<char*>(password.c_str()), password.length(),
                       OCI_ATTR_PASSWORD, ociErr_);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(ociServer_, ociErr_, OCI_DEFAULT);
        cleanupOCI();
        throw std::runtime_error("Failed to set password");
    }

    // Begin session
    status = OCISessionBegin(ociSvcCtx_, ociErr_, ociSession_,
                            OCI_CRED_RDBMS, OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(ociServer_, ociErr_, OCI_DEFAULT);
        cleanupOCI();
        throw std::runtime_error("Failed to begin Oracle session");
    }

    // Set session handle in service context
    status = OCIAttrSet(ociSvcCtx_, OCI_HTYPE_SVCCTX, ociSession_, 0,
                       OCI_ATTR_SESSION, ociErr_);
    if (status != OCI_SUCCESS) {
        OCISessionEnd(ociSvcCtx_, ociErr_, ociSession_, OCI_DEFAULT);
        OCIServerDetach(ociServer_, ociErr_, OCI_DEFAULT);
        cleanupOCI();
        throw std::runtime_error("Failed to set session in service context");
    }

    spdlog::debug("[OracleQueryExecutor] OCI initialized with connection");
}

void OracleQueryExecutor::cleanupOCI()
{
    // End session
    if (ociSession_ && ociSvcCtx_ && ociErr_) {
        OCISessionEnd(ociSvcCtx_, ociErr_, ociSession_, OCI_DEFAULT);
    }

    // Detach from server
    if (ociServer_ && ociErr_) {
        OCIServerDetach(ociServer_, ociErr_, OCI_DEFAULT);
    }

    // Free handles
    if (ociSession_) {
        OCIHandleFree(ociSession_, OCI_HTYPE_SESSION);
        ociSession_ = nullptr;
    }

    if (ociServer_) {
        OCIHandleFree(ociServer_, OCI_HTYPE_SERVER);
        ociServer_ = nullptr;
    }

    if (ociSvcCtx_) {
        OCIHandleFree(ociSvcCtx_, OCI_HTYPE_SVCCTX);
        ociSvcCtx_ = nullptr;
    }

    if (ociErr_) {
        OCIHandleFree(ociErr_, OCI_HTYPE_ERROR);
        ociErr_ = nullptr;
    }

    if (ociEnv_) {
        OCIHandleFree(ociEnv_, OCI_HTYPE_ENV);
        ociEnv_ = nullptr;
    }

    spdlog::debug("[OracleQueryExecutor] OCI connection and handles cleaned up");
}

Json::Value OracleQueryExecutor::executeQueryWithOCI(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] executeQueryWithOCI called");

    try {
        // Convert PostgreSQL placeholders to OCI positional binding format
        // $1, $2, $3 → :1, :2, :3 (NOT :v1<char[4000]> which is OTL syntax)
        std::string oracleQuery = query;
        std::regex pg_placeholder(R"(\$(\d+))");
        oracleQuery = std::regex_replace(oracleQuery, pg_placeholder, ":$1");

        // Convert LIMIT ... OFFSET to Oracle syntax
        std::regex limit_offset_regex(R"(\s+LIMIT\s+(\d+)\s+OFFSET\s+(\d+)\s*$)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, limit_offset_regex, " OFFSET $2 ROWS FETCH NEXT $1 ROWS ONLY");

        // Convert LIMIT clause alone to Oracle FETCH FIRST syntax
        std::regex limit_regex(R"(\s+LIMIT\s+(\d+)\s*$)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, limit_regex, " FETCH FIRST $1 ROWS ONLY");

        // Convert PostgreSQL NOW() to Oracle SYSDATE
        std::regex now_regex(R"(NOW\(\))", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, now_regex, "SYSDATE");

        // Convert PostgreSQL CURRENT_TIMESTAMP to Oracle SYSTIMESTAMP
        std::regex current_timestamp_regex(R"(CURRENT_TIMESTAMP)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, current_timestamp_regex, "SYSTIMESTAMP");

        // Remove PostgreSQL type casts (::typename)
        std::regex typecast_regex(R"(::[a-zA-Z_][a-zA-Z0-9_]*)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, typecast_regex, "");

        spdlog::debug("[OracleQueryExecutor] OCI query: {}", oracleQuery.substr(0, 200));

        // Allocate statement handle
        OCIStmt* ociStmt = nullptr;
        sword status = OCIHandleAlloc(ociEnv_, reinterpret_cast<void**>(&ociStmt),
                                     OCI_HTYPE_STMT, 0, nullptr);
        if (status != OCI_SUCCESS) {
            throw std::runtime_error("Failed to allocate OCI statement handle");
        }

        // Prepare statement
        status = OCIStmtPrepare(ociStmt, ociErr_,
                               reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                               oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to prepare OCI statement");
        }

        // Bind parameters (if any)
        for (size_t i = 0; i < params.size(); ++i) {
            OCIBind* bindHandle = nullptr;
            const std::string& param = params[i];

            status = OCIBindByPos(ociStmt, &bindHandle, ociErr_, i + 1,
                                 const_cast<char*>(param.c_str()), param.length(),
                                 SQLT_STR, nullptr, nullptr, nullptr, 0, nullptr, OCI_DEFAULT);
            if (status != OCI_SUCCESS) {
                OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to bind parameter " + std::to_string(i + 1));
            }
        }

        // Execute query
        status = OCIStmtExecute(ociSvcCtx_, ociStmt, ociErr_, 0, 0, nullptr, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
            // Get detailed Oracle error message
            char errbuf[512];
            sb4 errcode = 0;
            OCIErrorGet(ociErr_, 1, nullptr, &errcode,
                       reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);

            OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
            throw std::runtime_error(std::string("OCI query execution failed (code ") +
                                   std::to_string(errcode) + "): " + errbuf);
        }

        // Get column count
        ub4 colCount = 0;
        status = OCIAttrGet(ociStmt, OCI_HTYPE_STMT, &colCount, nullptr,
                           OCI_ATTR_PARAM_COUNT, ociErr_);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to get column count");
        }

        spdlog::debug("[OracleQueryExecutor] OCI query has {} columns", colCount);

        // Get column names and prepare defines
        std::vector<std::string> colNames;
        std::vector<char*> colBuffers;
        std::vector<sb2> indicators;

        for (ub4 i = 1; i <= colCount; ++i) {
            OCIParam* colParam = nullptr;
            status = OCIParamGet(ociStmt, OCI_HTYPE_STMT, ociErr_,
                                reinterpret_cast<void**>(&colParam), i);
            if (status != OCI_SUCCESS) {
                for (auto buf : colBuffers) delete[] buf;
                OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to get column parameter");
            }

            // Get column name
            OraText* colName = nullptr;
            ub4 colNameLen = 0;
            status = OCIAttrGet(colParam, OCI_DTYPE_PARAM, &colName, &colNameLen,
                               OCI_ATTR_NAME, ociErr_);
            if (status == OCI_SUCCESS) {
                colNames.push_back(std::string(reinterpret_cast<char*>(colName), colNameLen));
            } else {
                colNames.push_back("COL" + std::to_string(i));
            }

            // Allocate buffer for column data (4000 bytes for VARCHAR2)
            char* buffer = new char[4001];
            colBuffers.push_back(buffer);
            indicators.push_back(0);

            // Define column
            OCIDefine* defHandle = nullptr;
            status = OCIDefineByPos(ociStmt, &defHandle, ociErr_, i,
                                   buffer, 4000, SQLT_STR,
                                   &indicators[i - 1], nullptr, nullptr, OCI_DEFAULT);
            if (status != OCI_SUCCESS) {
                for (auto buf : colBuffers) delete[] buf;
                OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to define column " + std::to_string(i));
            }
        }

        // Fetch rows and build JSON result
        Json::Value result = Json::arrayValue;
        int rowCount = 0;

        while (true) {
            status = OCIStmtFetch2(ociStmt, ociErr_, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);

            if (status == OCI_NO_DATA) {
                break;  // No more rows
            }

            if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
                for (auto buf : colBuffers) delete[] buf;
                OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to fetch row");
            }

            Json::Value row;
            for (size_t i = 0; i < colCount; ++i) {
                if (indicators[i] == -1) {
                    // NULL value
                    row[colNames[i]] = Json::nullValue;
                } else {
                    // String value (VARCHAR2, TIMESTAMP converted to string)
                    row[colNames[i]] = Json::Value(colBuffers[i]);
                }
            }

            result.append(row);
            rowCount++;
        }

        spdlog::debug("[OracleQueryExecutor] OCI fetched {} rows", rowCount);

        // Cleanup
        for (auto buf : colBuffers) {
            delete[] buf;
        }
        OCIHandleFree(ociStmt, OCI_HTYPE_STMT);

        return result;

    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI query failed: {}", e.what());
        throw std::runtime_error("OCI query failed: " + std::string(e.what()));
    }
}

} // namespace common

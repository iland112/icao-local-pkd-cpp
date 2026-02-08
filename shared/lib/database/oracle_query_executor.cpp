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
    spdlog::debug("[OracleQueryExecutor] Executing SELECT query with OCI API (bypassing OTL)");

    try {
        // Acquire connection from pool (get OTL connection for OCI handle access)
        auto conn = pool_->acquire();
        if (!conn.isValid()) {
            throw std::runtime_error("Failed to acquire Oracle connection from pool");
        }

        // Get OTL connection to access underlying OCI handles
        otl_connect* otl_conn = static_cast<otl_connect*>(conn.get());

        // Convert PostgreSQL placeholders to OCI positional binding format
        std::string oracleQuery = query;
        std::regex pg_placeholder(R"(\$(\d+))");
        oracleQuery = std::regex_replace(oracleQuery, pg_placeholder, ":$1");

        // Convert LIMIT/OFFSET syntax
        std::regex limit_offset_regex(R"(\s+LIMIT\s+(\d+)\s+OFFSET\s+(\d+)\s*$)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, limit_offset_regex, " OFFSET $2 ROWS FETCH NEXT $1 ROWS ONLY");

        std::regex limit_regex(R"(\s+LIMIT\s+(\d+)\s*$)", std::regex::icase);
        oracleQuery = std::regex_replace(oracleQuery, limit_regex, " FETCH FIRST $1 ROWS ONLY");

        spdlog::debug("[OracleQueryExecutor] OCI query: {}", oracleQuery.substr(0, 200));

        // Allocate statement handle using OCI directly
        OCIStmt* stmt = nullptr;
        sword status = OCIHandleAlloc(ociEnv_, reinterpret_cast<void**>(&stmt),
                                      OCI_HTYPE_STMT, 0, nullptr);
        if (status != OCI_SUCCESS) {
            throw std::runtime_error("Failed to allocate OCI statement handle");
        }

        // Prepare statement
        status = OCIStmtPrepare(stmt, ociErr_,
                               reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                               oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to prepare OCI statement");
        }

        // Bind parameters using OCIBindByPos
        std::vector<char*> paramBuffers;  // Keep buffers alive during execution
        for (size_t i = 0; i < params.size(); ++i) {
            OCIBind* bind = nullptr;
            // Create null-terminated copy
            char* buffer = new char[params[i].length() + 1];
            strcpy(buffer, params[i].c_str());
            paramBuffers.push_back(buffer);

            status = OCIBindByPos(stmt, &bind, ociErr_, i + 1,
                                 buffer, params[i].length() + 1,
                                 SQLT_STR, nullptr, nullptr, nullptr,
                                 0, nullptr, OCI_DEFAULT);
            if (status != OCI_SUCCESS) {
                // Cleanup
                for (char* buf : paramBuffers) delete[] buf;
                OCIHandleFree(stmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to bind parameter " + std::to_string(i + 1));
            }
        }

        // Execute statement
        status = OCIStmtExecute(ociSvcCtx_, stmt, ociErr_, 0, 0,
                               nullptr, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
            for (char* buf : paramBuffers) delete[] buf;
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to execute OCI statement");
        }

        // Get column count
        ub4 colCount = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &colCount, nullptr,
                  OCI_ATTR_PARAM_COUNT, ociErr_);

        // Build JSON result
        Json::Value result = Json::arrayValue;

        // Define output buffers for each column
        std::vector<char*> colBuffers(colCount);
        std::vector<sb2> indicators(colCount);
        std::vector<std::string> colNames(colCount);

        for (ub4 i = 0; i < colCount; ++i) {
            // Get column descriptor
            OCIParam* col = nullptr;
            OCIParamGet(stmt, OCI_HTYPE_STMT, ociErr_, reinterpret_cast<void**>(&col), i + 1);

            // Get column name
            OraText* colName = nullptr;
            ub4 colNameLen = 0;
            OCIAttrGet(col, OCI_DTYPE_PARAM, &colName, &colNameLen,
                      OCI_ATTR_NAME, ociErr_);
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
            OCIDefineByPos(stmt, &def, ociErr_, i + 1,
                          colBuffers[i], 4000, SQLT_STR,
                          &indicators[i], nullptr, nullptr, OCI_DEFAULT);
        }

        // Fetch all rows
        while (true) {
            status = OCIStmtFetch2(stmt, ociErr_, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
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

        spdlog::debug("[OracleQueryExecutor] OCI query returned {} rows", result.size());
        return result;

    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI exception: {}", e.what());
        throw;
    }

    return Json::arrayValue;
}

int OracleQueryExecutor::executeCommand(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] Executing command with OCI API");

    try {
        // Acquire connection from pool
        auto conn = pool_->acquire();
        if (!conn.isValid()) {
            throw std::runtime_error("Failed to acquire Oracle connection from pool");
        }

        // Convert PostgreSQL placeholders to OCI format ($1 → :1)
        std::string oracleQuery = query;
        std::regex pg_placeholder(R"(\$(\d+))");
        oracleQuery = std::regex_replace(oracleQuery, pg_placeholder, ":$1");

        spdlog::debug("[OracleQueryExecutor] OCI command: {}", oracleQuery);

        // Allocate OCI statement handle
        OCIStmt* stmt = nullptr;
        sword status = OCIHandleAlloc(ociEnv_, reinterpret_cast<void**>(&stmt),
                                      OCI_HTYPE_STMT, 0, nullptr);
        if (status != OCI_SUCCESS) {
            throw std::runtime_error("Failed to allocate OCI statement handle");
        }

        // Prepare statement
        status = OCIStmtPrepare(stmt, ociErr_,
                               reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                               oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to prepare OCI statement");
        }

        // Bind parameters using OCIBindByPos
        std::vector<char*> paramBuffers;
        for (size_t i = 0; i < params.size(); ++i) {
            OCIBind* bind = nullptr;
            char* buffer = new char[params[i].length() + 1];
            strcpy(buffer, params[i].c_str());
            paramBuffers.push_back(buffer);

            status = OCIBindByPos(stmt, &bind, ociErr_, i + 1,
                                 buffer, params[i].length() + 1,
                                 SQLT_STR, nullptr, nullptr, nullptr,
                                 0, nullptr, OCI_DEFAULT);
            if (status != OCI_SUCCESS) {
                for (char* buf : paramBuffers) delete[] buf;
                OCIHandleFree(stmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to bind parameter");
            }
        }

        // Execute statement (iters=1 for DML commands)
        status = OCIStmtExecute(ociSvcCtx_, stmt, ociErr_, 1, 0,
                               nullptr, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
            // Get error message
            char errbuf[512];
            sb4 errcode = 0;
            OCIErrorGet(ociErr_, 1, nullptr, &errcode,
                       reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);

            for (char* buf : paramBuffers) delete[] buf;
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error(std::string("OCI statement execution failed: ") + errbuf);
        }

        // Get affected rows count
        ub4 affectedRows = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &affectedRows, nullptr,
                  OCI_ATTR_ROW_COUNT, ociErr_);

        // Commit transaction
        OCITransCommit(ociSvcCtx_, ociErr_, OCI_DEFAULT);

        // Cleanup buffers
        for (char* buf : paramBuffers) delete[] buf;
        OCIHandleFree(stmt, OCI_HTYPE_STMT);

        spdlog::debug("[OracleQueryExecutor] Command executed, affected rows: {}", affectedRows);
        return static_cast<int>(affectedRows);

    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI exception: {}", e.what());
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
// OCI Implementation (for stable VARCHAR2 TIMESTAMP handling)
// ============================================================================

void OracleQueryExecutor::initializeOCI()
{
    sword status;

    // Create OCI environment handle
    status = OCIEnvCreate(&ociEnv_, OCI_DEFAULT, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
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
            OCIHandleFree(ociStmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to execute OCI statement");
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

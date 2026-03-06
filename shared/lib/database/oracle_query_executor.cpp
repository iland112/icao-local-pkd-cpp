/**
 * @file oracle_query_executor.cpp
 * @brief Oracle Query Executor implementation using OCI Session Pool
 */

#include "oracle_query_executor.h"

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>

// Pre-compiled regex patterns for PostgreSQL→Oracle query transformation
// Compiled once at static init instead of per-query (~150K compilations eliminated during bulk upload)
static const std::regex s_pgPlaceholder(R"(\$(\d+))");
static const std::regex s_nullifInteger(R"(NULLIF\(([^,]+),\s*''\s*\)::INTEGER)", std::regex::icase);
static const std::regex s_limitOffset(R"(\s+LIMIT\s+(\d+|:\d+)\s+OFFSET\s+(\d+|:\d+)\s*$)", std::regex::icase);
static const std::regex s_limitOnly(R"(\s+LIMIT\s+(\d+|:\d+)\s*$)", std::regex::icase);
static const std::regex s_returningClause(R"(\s+RETURNING\s+\w+(\s+INTO\s+:\d+)?\s*$)", std::regex::icase);
// DML command additional patterns (used in executeCommand)
static const std::regex s_nowFunc(R"(NOW\(\))", std::regex::icase);
static const std::regex s_currentTimestamp(R"(CURRENT_TIMESTAMP)", std::regex::icase);
static const std::regex s_typecast(R"(::[a-zA-Z_][a-zA-Z0-9_]*)", std::regex::icase);
static const std::regex s_onConflict(R"(\s+ON\s+CONFLICT\s*\([^)]*\)\s+DO\s+(NOTHING|UPDATE\s+SET\s+.*)$)", std::regex::icase);

namespace common {

// --- Constructor & Destructor ---

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

    // Initialize OCI Session Pool for high-performance connection reuse
    try {
        initializeSessionPool();
    } catch (const std::exception& e) {
        spdlog::warn("[OracleQueryExecutor] Session pool init failed: {}", e.what());
        // Non-fatal at construction, but executeQuery/executeCommand will throw if !sessionPoolReady_
    }
}

OracleQueryExecutor::~OracleQueryExecutor()
{
    destroySessionPool();
    cleanupOCI();
}

// --- Public Interface Implementation ---

Json::Value OracleQueryExecutor::executeQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    if (!sessionPoolReady_) {
        throw std::runtime_error("OCI session pool is not available");
    }

    spdlog::debug("[OracleQueryExecutor] Executing SELECT query via session pool");

    PooledSession session;
    std::vector<char*> colBuffers;       // Declared outside try for cleanup in catch
    std::vector<OCILobLocator*> lobLocators;  // Declared outside try for cleanup in catch

    try {
        // Acquire pre-authenticated session from pool (1 round-trip vs 8-10)
        session = acquirePooledSession();

        // Convert PostgreSQL placeholders to OCI positional binding format
        // Uses pre-compiled static regex patterns (see file top) for performance
        std::string oracleQuery = query;
        oracleQuery = std::regex_replace(oracleQuery, s_pgPlaceholder, ":$1");

        // Convert PostgreSQL-specific NULLIF()::INTEGER to Oracle CASE expression
        oracleQuery = std::regex_replace(oracleQuery, s_nullifInteger, "CASE WHEN $1 IS NULL OR $1 = '' THEN NULL ELSE TO_NUMBER($1) END");

        // Convert LIMIT/OFFSET syntax (handles both literal numbers and :N bind variables)
        oracleQuery = std::regex_replace(oracleQuery, s_limitOffset, " OFFSET $2 ROWS FETCH NEXT $1 ROWS ONLY");
        oracleQuery = std::regex_replace(oracleQuery, s_limitOnly, " FETCH FIRST $1 ROWS ONLY");

        // Handle DML with RETURNING clause
        bool isDmlReturning = false;
        if (std::regex_search(oracleQuery, s_returningClause)) {
            isDmlReturning = true;
            oracleQuery = std::regex_replace(oracleQuery, s_returningClause, "");
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

        // Allocate statement handle from pool environment
        OCIStmt* stmt = nullptr;
        sword status = OCIHandleAlloc(poolEnv_, reinterpret_cast<void**>(&stmt),
                                      OCI_HTYPE_STMT, 0, nullptr);
        if (status != OCI_SUCCESS) {
            throw std::runtime_error("Failed to allocate OCI statement handle");
        }

        // Prepare statement
        status = OCIStmtPrepare(stmt, session.err,
                               reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                               oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to prepare OCI statement");
        }

        // Bind parameters using OCIBindByName (handles duplicate named binds correctly)
        // RAII: paramStringBuffers and binaryBuffers own the memory; no manual delete needed
        std::vector<std::string> paramStringBuffers;
        std::vector<std::vector<uint8_t>> binaryBuffers;
        std::vector<std::string> bindNames;
        paramStringBuffers.reserve(params.size());
        binaryBuffers.reserve(params.size());
        bindNames.reserve(params.size());

        for (size_t i = 0; i < params.size(); ++i) {
            OCIBind* bind = nullptr;

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

                status = OCIBindByName(stmt, &bind, session.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      buf.data(), static_cast<sb4>(buf.size()),
                                      SQLT_LBI, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            } else {
                paramStringBuffers.push_back(params[i]);
                std::string& strBuf = paramStringBuffers.back();

                status = OCIBindByName(stmt, &bind, session.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      strBuf.data(), static_cast<sb4>(strBuf.size() + 1),
                                      SQLT_STR, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            }

            if (status != OCI_SUCCESS) {
                OCIHandleFree(stmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to bind parameter " + std::to_string(i + 1));
            }
        }

        // Execute statement
        ub4 iters = isDmlReturning ? 1 : 0;
        status = OCIStmtExecute(session.svcCtx, stmt, session.err, iters, 0,
                               nullptr, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
            char errbuf[512];
            sb4 errcode = 0;
            OCIErrorGet(session.err, 1, nullptr, &errcode,
                       reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);

            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error(std::string("OCI statement execution failed (code ") +
                                   std::to_string(errcode) + "): " + errbuf);
        }

        // For DML with stripped RETURNING: commit and return empty result
        if (isDmlReturning) {
            OCITransCommit(session.svcCtx, session.err, OCI_DEFAULT);
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            releasePooledSession(session);
            spdlog::info("[OracleQueryExecutor] DML executed successfully (RETURNING stripped)");
            return Json::arrayValue;
        }

        // Get column count
        ub4 colCount = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &colCount, nullptr,
                  OCI_ATTR_PARAM_COUNT, session.err);

        // Build JSON result
        Json::Value result = Json::arrayValue;

        // Define output buffers for each column (with BLOB/CLOB LOB locator support)
        colBuffers.assign(colCount, nullptr);
        std::vector<sb2> indicators(colCount);
        std::vector<std::string> colNames(colCount);
        std::vector<ub2> colTypes(colCount, 0);
        lobLocators.assign(colCount, nullptr);

        for (ub4 i = 0; i < colCount; ++i) {
            OCIParam* col = nullptr;
            OCIParamGet(stmt, OCI_HTYPE_STMT, session.err, reinterpret_cast<void**>(&col), i + 1);

            OraText* colName = nullptr;
            ub4 colNameLen = 0;
            OCIAttrGet(col, OCI_DTYPE_PARAM, &colName, &colNameLen,
                      OCI_ATTR_NAME, session.err);
            std::string columnName(reinterpret_cast<char*>(colName), colNameLen);

            // Convert Oracle's UPPERCASE column names to lowercase for consistency
            std::transform(columnName.begin(), columnName.end(), columnName.begin(),
                          [](unsigned char c){ return std::tolower(c); });
            colNames[i] = columnName;

            // Detect column data type for BLOB/CLOB handling
            ub2 dataType = 0;
            OCIAttrGet(col, OCI_DTYPE_PARAM, &dataType, nullptr,
                      OCI_ATTR_DATA_TYPE, session.err);
            colTypes[i] = dataType;

            OCIDefine* def = nullptr;

            if (dataType == SQLT_BLOB || dataType == SQLT_CLOB) {
                // BLOB/CLOB: use LOB locator for proper large data reading
                OCIDescriptorAlloc(poolEnv_, reinterpret_cast<void**>(&lobLocators[i]),
                                  OCI_DTYPE_LOB, 0, nullptr);
                OCIDefineByPos(stmt, &def, session.err, i + 1,
                              &lobLocators[i], sizeof(OCILobLocator*),
                              dataType,
                              &indicators[i], nullptr, nullptr, OCI_DEFAULT);
                spdlog::debug("[OracleQueryExecutor] Column {} ({}) defined as LOB type {}",
                             i + 1, columnName, dataType);
            } else {
                // Regular columns: use string buffer
                colBuffers[i] = new char[4001];
                memset(colBuffers[i], 0, 4001);
                OCIDefineByPos(stmt, &def, session.err, i + 1,
                              colBuffers[i], 4000, SQLT_STR,
                              &indicators[i], nullptr, nullptr, OCI_DEFAULT);
            }
        }

        // Fetch all rows
        while (true) {
            status = OCIStmtFetch2(stmt, session.err, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
            if (status == OCI_NO_DATA) break;
            if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) break;

            Json::Value row;
            for (ub4 i = 0; i < colCount; ++i) {
                if (indicators[i] == -1) {
                    row[colNames[i]] = Json::nullValue;
                } else if (colTypes[i] == SQLT_BLOB) {
                    // Read BLOB binary data via LOB locator, convert to PostgreSQL hex format
                    ub4 lobLen = 0;
                    OCILobGetLength(session.svcCtx, session.err, lobLocators[i], &lobLen);

                    if (lobLen > 0) {
                        std::vector<uint8_t> lobBuf(lobLen);
                        ub4 amtp = lobLen;
                        OCILobRead(session.svcCtx, session.err, lobLocators[i],
                                  &amtp, 1, lobBuf.data(), lobLen,
                                  nullptr, nullptr, 0, SQLCS_IMPLICIT);

                        // Convert to PostgreSQL bytea hex format (\x...) for parseCertificateDataFromHex()
                        std::string hexStr = "\\x";
                        hexStr.reserve(2 + amtp * 2);
                        static const char hexChars[] = "0123456789abcdef";
                        for (ub4 j = 0; j < amtp; ++j) {
                            hexStr += hexChars[lobBuf[j] >> 4];
                            hexStr += hexChars[lobBuf[j] & 0x0f];
                        }
                        row[colNames[i]] = hexStr;
                    } else {
                        row[colNames[i]] = "";
                    }
                } else if (colTypes[i] == SQLT_CLOB) {
                    // Read CLOB text data via LOB locator
                    ub4 lobLen = 0;
                    OCILobGetLength(session.svcCtx, session.err, lobLocators[i], &lobLen);

                    if (lobLen > 0) {
                        std::vector<char> lobBuf(lobLen + 1, 0);
                        ub4 amtp = lobLen;
                        OCILobRead(session.svcCtx, session.err, lobLocators[i],
                                  &amtp, 1, lobBuf.data(), lobLen,
                                  nullptr, nullptr, 0, SQLCS_IMPLICIT);
                        row[colNames[i]] = std::string(lobBuf.data(), amtp);
                    } else {
                        row[colNames[i]] = "";
                    }
                } else {
                    row[colNames[i]] = Json::Value(colBuffers[i]);
                }
            }
            result.append(row);
        }

        // Cleanup buffers and LOB locators, track whether LOBs were used
        bool hadLobs = false;
        for (ub4 i = 0; i < colCount; ++i) {
            if (colBuffers[i]) delete[] colBuffers[i];
            if (lobLocators[i]) {
                hadLobs = true;
                OCIDescriptorFree(lobLocators[i], OCI_DTYPE_LOB);
            }
        }
        OCIHandleFree(stmt, OCI_HTYPE_STMT);

        // Release session back to pool. If LOB operations occurred, drop the session
        // to prevent ORA-03127 on reuse (Oracle retains internal LOB state on the
        // session even after locator descriptors are freed).
        releasePooledSession(session, hadLobs);

        spdlog::debug("[OracleQueryExecutor] OCI query returned {} rows{}", result.size(),
                     hadLobs ? " (session dropped after LOB)" : "");
        return result;

    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI exception: {}", e.what());
        // Cleanup allocated column buffers and LOB locators to prevent memory leak
        for (ub4 i = 0; i < colBuffers.size(); ++i) {
            if (colBuffers[i]) delete[] colBuffers[i];
            if (i < lobLocators.size() && lobLocators[i]) {
                OCIDescriptorFree(lobLocators[i], OCI_DTYPE_LOB);
            }
        }
        // Ensure session is released back to pool on exception (drop to be safe)
        releasePooledSession(session, true);
        throw;
    }
}

int OracleQueryExecutor::executeCommand(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    if (!sessionPoolReady_) {
        throw std::runtime_error("OCI session pool is not available");
    }

    // Convert PostgreSQL syntax to Oracle syntax
    // Uses pre-compiled static regex patterns (see file top) for performance
    std::string oracleQuery = query;
    oracleQuery = std::regex_replace(oracleQuery, s_pgPlaceholder, ":$1");
    oracleQuery = std::regex_replace(oracleQuery, s_nullifInteger, "CASE WHEN $1 IS NULL OR $1 = '' THEN NULL ELSE TO_NUMBER($1) END");
    oracleQuery = std::regex_replace(oracleQuery, s_currentTimestamp, "SYSTIMESTAMP");
    oracleQuery = std::regex_replace(oracleQuery, s_nowFunc, "SYSDATE");
    oracleQuery = std::regex_replace(oracleQuery, s_typecast, "");

    // Handle PostgreSQL ON CONFLICT clause (not supported in Oracle)
    oracleQuery = std::regex_replace(oracleQuery, s_onConflict, "");

    // ── Batch mode: pinned session + cached statement + deferred commit ──
    if (batchMode_) {
        try {
            // Look up cached statement or prepare new one
            OCIStmt* stmt = nullptr;
            auto cacheIt = stmtCache_.find(oracleQuery);
            if (cacheIt != stmtCache_.end()) {
                stmt = cacheIt->second;
            } else {
                sword status = OCIHandleAlloc(poolEnv_, reinterpret_cast<void**>(&stmt),
                                              OCI_HTYPE_STMT, 0, nullptr);
                if (status != OCI_SUCCESS) {
                    throw std::runtime_error("Failed to allocate OCI statement handle (batch)");
                }
                status = OCIStmtPrepare(stmt, batchSession_.err,
                                        reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                                        oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
                if (status != OCI_SUCCESS) {
                    OCIHandleFree(stmt, OCI_HTYPE_STMT);
                    throw std::runtime_error("Failed to prepare OCI statement (batch)");
                }
                stmtCache_[oracleQuery] = stmt;
            }

            // Bind parameters
            // RAII: paramStringBuffers and binaryBuffers own the memory; no manual delete needed
            std::vector<std::string> paramStringBuffers;
            std::vector<std::vector<uint8_t>> binaryBuffers;
            std::vector<std::string> bindNames;
            paramStringBuffers.reserve(params.size());
            binaryBuffers.reserve(params.size());
            bindNames.reserve(params.size());

            for (size_t i = 0; i < params.size(); ++i) {
                OCIBind* bind = nullptr;
                bindNames.push_back(":" + std::to_string(i + 1));
                const std::string& bindName = bindNames.back();

                bool isBinary = false;
                size_t hexStart = 0;
                if (params[i].size() > 3 && params[i][0] == '\\' && params[i][1] == '\\' && params[i][2] == 'x') {
                    isBinary = true;
                    hexStart = 3;
                } else if (params[i].size() > 2 && params[i][0] == '\\' && params[i][1] == 'x') {
                    isBinary = true;
                    hexStart = 2;
                }

                sword status;
                if (isBinary) {
                    std::vector<uint8_t> rawBytes;
                    rawBytes.reserve((params[i].size() - hexStart) / 2);
                    for (size_t j = hexStart; j + 1 < params[i].size(); j += 2) {
                        char hex[3] = { params[i][j], params[i][j + 1], '\0' };
                        rawBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
                    }
                    binaryBuffers.push_back(std::move(rawBytes));
                    auto& buf = binaryBuffers.back();
                    status = OCIBindByName(stmt, &bind, batchSession_.err,
                                          reinterpret_cast<const OraText*>(bindName.c_str()),
                                          static_cast<sb4>(bindName.length()),
                                          buf.data(), static_cast<sb4>(buf.size()),
                                          SQLT_LBI, nullptr, nullptr, nullptr,
                                          0, nullptr, OCI_DEFAULT);
                } else {
                    paramStringBuffers.push_back(params[i]);
                    std::string& strBuf = paramStringBuffers.back();
                    status = OCIBindByName(stmt, &bind, batchSession_.err,
                                          reinterpret_cast<const OraText*>(bindName.c_str()),
                                          static_cast<sb4>(bindName.length()),
                                          strBuf.data(), static_cast<sb4>(strBuf.size() + 1),
                                          SQLT_STR, nullptr, nullptr, nullptr,
                                          0, nullptr, OCI_DEFAULT);
                }

                if (status != OCI_SUCCESS) {
                    throw std::runtime_error("Failed to bind parameter " + std::to_string(i + 1) + " (batch)");
                }
            }

            // Execute (no commit — deferred to endBatch)
            sword status = OCIStmtExecute(batchSession_.svcCtx, stmt, batchSession_.err, 1, 0,
                                          nullptr, nullptr, OCI_DEFAULT);
            if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
                char errbuf[512];
                sb4 errcode = 0;
                OCIErrorGet(batchSession_.err, 1, nullptr, &errcode,
                           reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);
                throw std::runtime_error(std::string("OCI batch execution failed: ") + errbuf);
            }

            ub4 affectedRows = 0;
            OCIAttrGet(stmt, OCI_HTYPE_STMT, &affectedRows, nullptr,
                      OCI_ATTR_ROW_COUNT, batchSession_.err);

            return static_cast<int>(affectedRows);

        } catch (const std::exception& e) {
            spdlog::error("[OracleQueryExecutor] Batch command exception: {}", e.what());
            throw;
        }
    }

    // ── Normal mode: acquire session per call, commit per call ──
    spdlog::debug("[OracleQueryExecutor] Executing command via session pool");

    PooledSession session;

    try {
        // Acquire pre-authenticated session from pool
        session = acquirePooledSession();

        spdlog::debug("[OracleQueryExecutor] OCI command: {}", oracleQuery.substr(0, 300));

        // Allocate OCI statement handle from pool environment
        OCIStmt* stmt = nullptr;
        sword status = OCIHandleAlloc(poolEnv_, reinterpret_cast<void**>(&stmt),
                                      OCI_HTYPE_STMT, 0, nullptr);
        if (status != OCI_SUCCESS) {
            throw std::runtime_error("Failed to allocate OCI statement handle");
        }

        // Prepare statement
        status = OCIStmtPrepare(stmt, session.err,
                               reinterpret_cast<const OraText*>(oracleQuery.c_str()),
                               oracleQuery.length(), OCI_NTV_SYNTAX, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error("Failed to prepare OCI statement");
        }

        // Bind parameters using OCIBindByName
        // RAII: paramStringBuffers and binaryBuffers own the memory; no manual delete needed
        std::vector<std::string> paramStringBuffers;
        std::vector<std::vector<uint8_t>> binaryBuffers;
        std::vector<std::string> bindNames;
        paramStringBuffers.reserve(params.size());
        binaryBuffers.reserve(params.size());
        bindNames.reserve(params.size());

        for (size_t i = 0; i < params.size(); ++i) {
            OCIBind* bind = nullptr;

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

                status = OCIBindByName(stmt, &bind, session.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      buf.data(), static_cast<sb4>(buf.size()),
                                      SQLT_LBI, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            } else {
                paramStringBuffers.push_back(params[i]);
                std::string& strBuf = paramStringBuffers.back();

                status = OCIBindByName(stmt, &bind, session.err,
                                      reinterpret_cast<const OraText*>(bindName.c_str()),
                                      static_cast<sb4>(bindName.length()),
                                      strBuf.data(), static_cast<sb4>(strBuf.size() + 1),
                                      SQLT_STR, nullptr, nullptr, nullptr,
                                      0, nullptr, OCI_DEFAULT);
            }

            if (status != OCI_SUCCESS) {
                OCIHandleFree(stmt, OCI_HTYPE_STMT);
                throw std::runtime_error("Failed to bind parameter " + std::to_string(i + 1));
            }
        }

        // Execute statement (iters=1 for DML commands)
        status = OCIStmtExecute(session.svcCtx, stmt, session.err, 1, 0,
                               nullptr, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
            char errbuf[512];
            sb4 errcode = 0;
            OCIErrorGet(session.err, 1, nullptr, &errcode,
                       reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);

            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            throw std::runtime_error(std::string("OCI statement execution failed: ") + errbuf);
        }

        // Get affected rows count
        ub4 affectedRows = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &affectedRows, nullptr,
                  OCI_ATTR_ROW_COUNT, session.err);

        // Commit transaction
        OCITransCommit(session.svcCtx, session.err, OCI_DEFAULT);

        OCIHandleFree(stmt, OCI_HTYPE_STMT);

        // Release session back to pool
        releasePooledSession(session);

        spdlog::debug("[OracleQueryExecutor] Command executed, affected rows: {}", affectedRows);
        return static_cast<int>(affectedRows);

    } catch (const std::exception& e) {
        spdlog::error("[OracleQueryExecutor] OCI exception: {}", e.what());
        // Ensure session is released back to pool on exception
        releasePooledSession(session);
        throw;
    }
}

// ── Batch mode lifecycle ──

void OracleQueryExecutor::beginBatch() {
    if (batchMode_) {
        spdlog::debug("[OracleQueryExecutor] beginBatch called but already in batch mode — ignoring");
        return;
    }
    if (!sessionPoolReady_) {
        spdlog::warn("[OracleQueryExecutor] beginBatch: session pool not ready — batch mode disabled");
        return;
    }

    batchSession_ = acquirePooledSession();
    batchMode_ = true;
    spdlog::info("[OracleQueryExecutor] Batch mode started (session pinned, commit deferred)");
}

void OracleQueryExecutor::endBatch() {
    if (!batchMode_) return;

    // Commit all pending operations
    if (batchSession_.svcCtx && batchSession_.err) {
        OCITransCommit(batchSession_.svcCtx, batchSession_.err, OCI_DEFAULT);
    }

    // Free all cached statements
    for (auto& [sql, stmt] : stmtCache_) {
        if (stmt) OCIHandleFree(stmt, OCI_HTYPE_STMT);
    }
    stmtCache_.clear();

    // Release pinned session
    releasePooledSession(batchSession_);
    batchMode_ = false;
    spdlog::info("[OracleQueryExecutor] Batch mode ended (committed + session released)");
}

Json::Value OracleQueryExecutor::executeScalar(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    spdlog::debug("[OracleQueryExecutor] Executing scalar query via OCI session pool");

    // Delegate to executeQuery() (OCI session pool) and extract first column of first row
    // This avoids OTL issues (ORA-00923, premature execution, etc.)
    Json::Value rows = executeQuery(query, params);

    if (rows.empty()) {
        throw std::runtime_error("Scalar query returned no rows");
    }

    const Json::Value& firstRow = rows[0];
    if (firstRow.empty()) {
        throw std::runtime_error("Scalar query returned empty row");
    }

    // Return the first column value
    auto members = firstRow.getMemberNames();
    if (members.empty()) {
        throw std::runtime_error("Scalar query returned no columns");
    }

    return firstRow[members[0]];
}

// --- OCI Startup Connectivity Check ---

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

// --- OCI Session Pool Implementation (High-Performance Connection Reuse) ---

void OracleQueryExecutor::initializeSessionPool()
{
    sword status;

    // Create dedicated OCI environment for session pool (OCI_THREADED for concurrency)
    status = OCIEnvCreate(&poolEnv_, OCI_THREADED, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    if (status != OCI_SUCCESS) {
        throw std::runtime_error("[SessionPool] Failed to create OCI environment");
    }

    // Allocate error handle for pool operations
    status = OCIHandleAlloc(poolEnv_, reinterpret_cast<void**>(&poolErr_),
                           OCI_HTYPE_ERROR, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(poolEnv_, OCI_HTYPE_ENV);
        poolEnv_ = nullptr;
        throw std::runtime_error("[SessionPool] Failed to allocate error handle");
    }

    // Allocate session pool handle
    status = OCIHandleAlloc(poolEnv_, reinterpret_cast<void**>(&sessionPool_),
                           OCI_HTYPE_SPOOL, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(poolErr_, OCI_HTYPE_ERROR);
        OCIHandleFree(poolEnv_, OCI_HTYPE_ENV);
        poolErr_ = nullptr;
        poolEnv_ = nullptr;
        throw std::runtime_error("[SessionPool] Failed to allocate session pool handle");
    }

    // Parse connection string: user/password@host:port/service
    size_t atPos = connString_.find('@');
    size_t slashPos = connString_.find('/');
    if (atPos == std::string::npos || slashPos == std::string::npos) {
        destroySessionPool();
        throw std::runtime_error("[SessionPool] Invalid connection string format");
    }

    std::string username = connString_.substr(0, slashPos);
    std::string password = connString_.substr(slashPos + 1, atPos - slashPos - 1);
    std::string dbString = connString_.substr(atPos + 1);  // host:port/service

    // Create session pool
    // sessMin=2: Always keep 2 sessions ready
    // sessMax=10: Allow up to 10 concurrent sessions
    // sessIncr=1: Grow by 1 session when needed
    status = OCISessionPoolCreate(
        poolEnv_, poolErr_, sessionPool_,
        &poolName_, &poolNameLen_,
        reinterpret_cast<const OraText*>(dbString.c_str()),
        static_cast<ub4>(dbString.length()),
        2,   // sessMin
        10,  // sessMax
        1,   // sessIncr
        reinterpret_cast<OraText*>(const_cast<char*>(username.c_str())),
        static_cast<ub4>(username.length()),
        reinterpret_cast<OraText*>(const_cast<char*>(password.c_str())),
        static_cast<ub4>(password.length()),
        OCI_SPC_HOMOGENEOUS  // All sessions use same credentials
    );

    if (status != OCI_SUCCESS) {
        char errbuf[512] = {0};
        sb4 errcode = 0;
        OCIErrorGet(poolErr_, 1, nullptr, &errcode,
                   reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);
        spdlog::error("[SessionPool] OCISessionPoolCreate failed (code {}): {}", errcode, errbuf);
        destroySessionPool();
        throw std::runtime_error(std::string("[SessionPool] Pool creation failed: ") + errbuf);
    }

    sessionPoolReady_ = true;
    spdlog::info("[OracleQueryExecutor] ✅ OCI Session Pool initialized (min=2, max=10, db={})", dbString);
}

void OracleQueryExecutor::destroySessionPool()
{
    if (sessionPool_ && poolErr_) {
        OCISessionPoolDestroy(sessionPool_, poolErr_, OCI_DEFAULT);
        spdlog::info("[OracleQueryExecutor] OCI Session Pool destroyed");
    }

    if (sessionPool_) {
        OCIHandleFree(sessionPool_, OCI_HTYPE_SPOOL);
        sessionPool_ = nullptr;
    }
    if (poolErr_) {
        OCIHandleFree(poolErr_, OCI_HTYPE_ERROR);
        poolErr_ = nullptr;
    }
    if (poolEnv_) {
        OCIHandleFree(poolEnv_, OCI_HTYPE_ENV);
        poolEnv_ = nullptr;
    }

    poolName_ = nullptr;
    poolNameLen_ = 0;
    sessionPoolReady_ = false;
}

OracleQueryExecutor::PooledSession OracleQueryExecutor::acquirePooledSession()
{
    if (!sessionPoolReady_) {
        throw std::runtime_error("[SessionPool] Session pool is not initialized");
    }

    PooledSession session;

    // Allocate per-session error handle (thread-safe)
    sword status = OCIHandleAlloc(poolEnv_, reinterpret_cast<void**>(&session.err),
                                  OCI_HTYPE_ERROR, 0, nullptr);
    if (status != OCI_SUCCESS) {
        throw std::runtime_error("[SessionPool] Failed to allocate error handle for session");
    }

    // Try to get a session with NLS tag (skip NLS setup if found)
    boolean found = FALSE;
    status = OCISessionGet(
        poolEnv_, session.err, &session.svcCtx,
        nullptr,  // authInfo: NULL for homogeneous pool
        poolName_, poolNameLen_,
        reinterpret_cast<const OraText*>(NLS_SESSION_TAG),
        static_cast<ub4>(strlen(NLS_SESSION_TAG)),
        nullptr, nullptr,  // retTagInfo (not needed)
        &found,
        OCI_SESSGET_SPOOL
    );

    if (status != OCI_SUCCESS) {
        char errbuf[512] = {0};
        sb4 errcode = 0;
        OCIErrorGet(session.err, 1, nullptr, &errcode,
                   reinterpret_cast<OraText*>(errbuf), sizeof(errbuf), OCI_HTYPE_ERROR);
        OCIHandleFree(session.err, OCI_HTYPE_ERROR);
        session.err = nullptr;
        throw std::runtime_error(std::string("[SessionPool] OCISessionGet failed (code ") +
                               std::to_string(errcode) + "): " + errbuf);
    }

    // If session doesn't have NLS tag, configure NLS settings
    if (!found) {
        OCIStmt* nlsStmt = nullptr;
        OCIHandleAlloc(poolEnv_, reinterpret_cast<void**>(&nlsStmt),
                      OCI_HTYPE_STMT, 0, nullptr);

        if (nlsStmt) {
            const char* nlsTs = "ALTER SESSION SET NLS_TIMESTAMP_FORMAT = 'YYYY-MM-DD HH24:MI:SS'";
            OCIStmtPrepare(nlsStmt, session.err,
                          reinterpret_cast<const OraText*>(nlsTs),
                          static_cast<ub4>(strlen(nlsTs)), OCI_NTV_SYNTAX, OCI_DEFAULT);
            OCIStmtExecute(session.svcCtx, nlsStmt, session.err, 1, 0,
                          nullptr, nullptr, OCI_DEFAULT);

            const char* nlsDt = "ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD HH24:MI:SS'";
            OCIStmtPrepare(nlsStmt, session.err,
                          reinterpret_cast<const OraText*>(nlsDt),
                          static_cast<ub4>(strlen(nlsDt)), OCI_NTV_SYNTAX, OCI_DEFAULT);
            OCIStmtExecute(session.svcCtx, nlsStmt, session.err, 1, 0,
                          nullptr, nullptr, OCI_DEFAULT);

            OCIHandleFree(nlsStmt, OCI_HTYPE_STMT);
            spdlog::debug("[SessionPool] NLS configured for new session");
        }
    }

    return session;
}

void OracleQueryExecutor::releasePooledSession(PooledSession& session, bool dropSession)
{
    if (session.svcCtx) {
        // Rollback any pending transaction state before returning session to pool.
        OCITransRollback(session.svcCtx, session.err, OCI_DEFAULT);

        // After LOB operations (BLOB/CLOB reads via OCILobRead), Oracle's OCI session
        // retains internal LOB state that isn't cleared by OCIDescriptorFree() or
        // OCITransRollback(). Reusing such a session causes ORA-03127 ("no new operations
        // allowed until the active operation ends"). The fix: destroy the session instead
        // of returning it to the pool when LOB operations occurred.
        ub4 mode = dropSession ? OCI_SESSRLS_DROPSESS : OCI_DEFAULT;

        OCISessionRelease(session.svcCtx, session.err,
                         reinterpret_cast<OraText*>(const_cast<char*>(NLS_SESSION_TAG)),
                         static_cast<ub4>(strlen(NLS_SESSION_TAG)),
                         mode);
        session.svcCtx = nullptr;

        if (dropSession) {
            spdlog::debug("[SessionPool] Session dropped after LOB operation (prevents ORA-03127)");
        }
    }
    if (session.err) {
        OCIHandleFree(session.err, OCI_HTYPE_ERROR);
        session.err = nullptr;
    }
}

} // namespace common

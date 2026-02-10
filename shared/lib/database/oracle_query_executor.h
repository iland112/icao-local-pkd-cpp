#pragma once

#include "i_query_executor.h"
#include "oracle_connection_pool.h"

// OCI (Oracle Call Interface) headers
#include <oci.h>

// Suppress OTL library warnings (third-party code)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"

// Define OTL configuration before including otlv4.h
#define OTL_ORA11G_R2  // Oracle 11g Release 2 and higher
#define OTL_ORA_TIMESTAMP  // Enable Oracle TIMESTAMP support
#define OTL_STL        // Enable STL features
#define OTL_ANSI_CPP   // Enable ANSI C++ mode
#include "external/otl/otlv4.h"

#pragma GCC diagnostic pop

/**
 * @file oracle_query_executor.h
 * @brief Oracle Query Executor - OTL-based implementation
 *
 * Implements IQueryExecutor using Oracle OTL (Oracle Template Library).
 * Handles connection acquisition from pool, query execution with OTL streams,
 * result parsing, and JSON conversion.
 *
 * @note Part of Oracle migration Phase 3
 * @date 2026-02-04
 */

namespace common {

/**
 * @brief Oracle-specific query executor
 *
 * Uses OracleConnectionPool for connection management and OTL for query execution.
 * Converts OTL otlStream results to Json::Value for database-agnostic Repository code.
 *
 * OTL Query Syntax:
 * - Parameterized queries use :param1<char[100]>, :param2<int>, etc.
 * - PostgreSQL $1, $2 syntax must be converted to :1, :2 for Oracle
 */
class OracleQueryExecutor : public IQueryExecutor {
public:
    /**
     * @brief Construct Oracle query executor
     * @param pool Oracle connection pool
     * @throws std::invalid_argument if pool is nullptr
     */
    explicit OracleQueryExecutor(OracleConnectionPool* pool);

    /**
     * @brief Destructor - cleanup OCI handles
     */
    ~OracleQueryExecutor() override;

    /**
     * @brief Execute SELECT query with parameterized binding
     *
     * Converts PostgreSQL-style $1, $2 placeholders to Oracle :1, :2 format.
     * Uses OTL otlStreams for query execution and result parsing.
     *
     * @param query SQL query (PostgreSQL $1 syntax, auto-converted to Oracle :1)
     * @param params Query parameters
     * @return JSON array of result rows
     * @throws std::runtime_error on execution or conversion failure
     */
    Json::Value executeQuery(
        const std::string& query,
        const std::vector<std::string>& params = {}
    ) override;

    /**
     * @brief Execute INSERT/UPDATE/DELETE command
     *
     * @param query SQL command (PostgreSQL $1 syntax, auto-converted)
     * @param params Query parameters
     * @return Number of affected rows (from OTL otlStream)
     * @throws std::runtime_error on execution failure
     */
    int executeCommand(
        const std::string& query,
        const std::vector<std::string>& params
    ) override;

    /**
     * @brief Execute scalar query (single value)
     *
     * @param query SQL query returning single value
     * @param params Query parameters
     * @return Single JSON value
     * @throws std::runtime_error if result is empty or has multiple columns
     */
    Json::Value executeScalar(
        const std::string& query,
        const std::vector<std::string>& params = {}
    ) override;

    /**
     * @brief Get database type
     * @return "oracle"
     */
    std::string getDatabaseType() const override { return "oracle"; }

private:
    OracleConnectionPool* pool_;  ///< Oracle connection pool

    // OCI handles for stable VARCHAR2 TIMESTAMP handling (deprecated - now per-query)
    OCIEnv* ociEnv_;      ///< OCI environment handle (legacy, for executeQueryWithOCI)
    OCIError* ociErr_;    ///< OCI error handle (legacy, for executeQueryWithOCI)
    OCISvcCtx* ociSvcCtx_; ///< OCI service context (legacy, for executeQueryWithOCI)
    OCIServer* ociServer_; ///< OCI server handle (legacy, for executeQueryWithOCI)
    OCISession* ociSession_; ///< OCI session handle (legacy, for executeQueryWithOCI)

    std::string connString_; ///< Oracle connection string

    /**
     * @brief OCI connection handles for per-query thread-safe usage
     */
    struct OciConnection {
        OCIEnv* env = nullptr;
        OCIError* err = nullptr;
        OCISvcCtx* svcCtx = nullptr;
        OCIServer* server = nullptr;
        OCISession* session = nullptr;
    };

    /**
     * @brief Create new OCI connection handles and connect to Oracle
     * @param[out] conn OCI connection structure to populate
     * @throws std::runtime_error on connection failure
     */
    void createOciConnection(OciConnection& conn);

    /**
     * @brief Disconnect OCI session and detach from server
     * @param conn OCI connection to disconnect
     */
    void disconnectOci(OciConnection& conn);

    /**
     * @brief Free all OCI handles
     * @param conn OCI connection to free
     */
    void freeOciHandles(OciConnection& conn);

    /**
     * @brief Initialize OCI environment and error handles
     * @throws std::runtime_error on OCI initialization failure
     */
    void initializeOCI();

    /**
     * @brief Cleanup OCI handles
     */
    void cleanupOCI();

    /**
     * @brief Execute SELECT query using OCI (for stable TIMESTAMP handling)
     *
     * Uses OCI instead of OTL for queries that need stable VARCHAR2 reading.
     * OTL has issues with describe_select() even for VARCHAR2 columns.
     *
     * @param query SQL query
     * @param params Query parameters
     * @return JSON array of result rows
     * @throws std::runtime_error on OCI execution failure
     */
    Json::Value executeQueryWithOCI(
        const std::string& query,
        const std::vector<std::string>& params
    );

    /**
     * @brief Convert PostgreSQL placeholder syntax to Oracle syntax
     *
     * PostgreSQL: SELECT * FROM users WHERE id = $1 AND name = $2
     * Oracle:     SELECT * FROM users WHERE id = :1 AND name = :2
     *
     * @param query PostgreSQL-style query
     * @return Oracle-style query
     */
    std::string convertPlaceholders(const std::string& query);

    /**
     * @brief Convert OTL otlStream result to JSON array
     *
     * Reads all rows from OTL otlStream and converts to JSON format.
     * Handles type detection based on column metadata.
     *
     * @param otlStream OTL otlStream with query results
     * @return JSON array of rows
     */
    Json::Value otlStreamToJson(otl_stream& otlStream);

    /**
     * @brief Detect and convert OTL column value to JSON
     * @note Currently unused - kept for potential future refactoring
     * @param otlStream OTL otlStream
     * @param colIndex Column index (0-based)
     * @param colType OTL column type
     * @return JSON value (string, number, bool, or null)
     */
    // Json::Value otlValueToJson(otl_stream& otlStream, int colIndex, int colType);
};

} // namespace common

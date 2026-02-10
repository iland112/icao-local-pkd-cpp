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
    OracleConnectionPool* pool_;  ///< Oracle connection pool (for executeScalar via OTL)

    // Legacy OCI handles (for startup connectivity check)
    OCIEnv* ociEnv_;
    OCIError* ociErr_;
    OCISvcCtx* ociSvcCtx_;
    OCIServer* ociServer_;
    OCISession* ociSession_;

    std::string connString_; ///< Oracle connection string (user/password@host:port/service)

    // =========================================================================
    // OCI Session Pool (high-performance connection reuse)
    // =========================================================================

    /**
     * @brief Pooled session handle returned by acquirePooledSession()
     */
    struct PooledSession {
        OCISvcCtx* svcCtx = nullptr;  ///< Service context from pool
        OCIError* err = nullptr;      ///< Per-session error handle
    };

    OCIEnv* poolEnv_ = nullptr;       ///< Shared OCI environment for session pool
    OCIError* poolErr_ = nullptr;     ///< Error handle for pool operations
    OCISPool* sessionPool_ = nullptr; ///< OCI Session Pool handle
    OraText* poolName_ = nullptr;     ///< Pool name (set by OCISessionPoolCreate)
    ub4 poolNameLen_ = 0;             ///< Pool name length
    bool sessionPoolReady_ = false;   ///< Whether session pool initialized successfully

    static constexpr const char* NLS_SESSION_TAG = "NLS_ISO";  ///< Tag for NLS-configured sessions

    /**
     * @brief Initialize OCI Session Pool
     * @throws std::runtime_error on pool creation failure
     */
    void initializeSessionPool();

    /**
     * @brief Destroy OCI Session Pool and free resources
     */
    void destroySessionPool();

    /**
     * @brief Acquire a session from the OCI Session Pool
     *
     * Returns a pre-authenticated session with NLS settings configured.
     * Uses session tagging to skip NLS setup on reused sessions.
     *
     * @return PooledSession with valid svcCtx and err handles
     * @throws std::runtime_error if pool is not ready or acquire fails
     */
    PooledSession acquirePooledSession();

    /**
     * @brief Release a session back to the OCI Session Pool
     * @param session Session to release (handles are nulled after release)
     */
    void releasePooledSession(PooledSession& session);

    // =========================================================================
    // Legacy per-query connection methods (kept as fallback)
    // =========================================================================

    struct OciConnection {
        OCIEnv* env = nullptr;
        OCIError* err = nullptr;
        OCISvcCtx* svcCtx = nullptr;
        OCIServer* server = nullptr;
        OCISession* session = nullptr;
    };

    void createOciConnection(OciConnection& conn);
    void disconnectOci(OciConnection& conn);
    void freeOciHandles(OciConnection& conn);

    // =========================================================================
    // OCI lifecycle and helpers
    // =========================================================================

    void initializeOCI();
    void cleanupOCI();

    Json::Value executeQueryWithOCI(
        const std::string& query,
        const std::vector<std::string>& params
    );

    std::string convertPlaceholders(const std::string& query);
    Json::Value otlStreamToJson(otl_stream& otlStream);
};

} // namespace common

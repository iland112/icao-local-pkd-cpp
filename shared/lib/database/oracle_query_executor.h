/**
 * @file oracle_query_executor.h
 * @brief Oracle Query Executor - OCI Session Pool implementation
 *
 * Implements IQueryExecutor using OCI (Oracle Call Interface) Session Pool.
 * Handles session acquisition from pool, query execution with OCI statements,
 * result parsing, and JSON conversion.
 *
 * @date 2026-02-04
 */

#pragma once

#include "i_query_executor.h"
#include "oracle_connection_pool.h"

// OCI (Oracle Call Interface) headers
#include <oci.h>
#include <unordered_map>

namespace common {

/**
 * @brief Oracle-specific query executor
 *
 * Uses OCI Session Pool for high-performance connection reuse.
 * Converts PostgreSQL-style $1, $2 placeholders to Oracle :1, :2 format.
 * Results are returned as Json::Value for database-agnostic Repository code.
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
     * Uses OCI Session Pool for query execution and result parsing.
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
     * @return Number of affected rows
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

    /**
     * @brief Begin batch mode — pin session, cache statements, defer commits
     */
    void beginBatch() override;

    /**
     * @brief End batch mode — commit once, free cached stmts, release session
     */
    void endBatch() override;

private:
    OracleConnectionPool* pool_;  ///< Oracle connection pool

    // Legacy OCI handles (for startup connectivity check)
    OCIEnv* ociEnv_;
    OCIError* ociErr_;
    OCISvcCtx* ociSvcCtx_;
    OCIServer* ociServer_;
    OCISession* ociSession_;

    std::string connString_; ///< Oracle connection string (user/password@host:port/service)

    /// @name OCI Session Pool (high-performance connection reuse)

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
     * @param dropSession If true, destroy session instead of returning to pool
     *        (used after LOB operations to prevent ORA-03127 on session reuse)
     */
    void releasePooledSession(PooledSession& session, bool dropSession = false);


    /// @name OCI lifecycle and helpers

    /** @brief Initialize OCI environment and connect to Oracle */
    void initializeOCI();
    /** @brief Clean up OCI connection and free handles */
    void cleanupOCI();


    /// @name Batch mode (v2.26.1 — session pinning + statement cache + deferred commit)

    bool batchMode_ = false;                              ///< Whether batch mode is active
    PooledSession batchSession_;                           ///< Pinned session for batch mode
    std::unordered_map<std::string, OCIStmt*> stmtCache_;  ///< Cached prepared statements (SQL → stmt)
};

} // namespace common

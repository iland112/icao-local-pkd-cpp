/**
 * @file db_connection_pool_factory.h
 * @brief Database Connection Pool Factory (Strategy Pattern)
 *
 * Creates appropriate connection pool based on database type
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include "db_connection_interface.h"
#include <string>
#include <memory>
#include <map>
#include <vector>

namespace common {

/**
 * @brief Connection pool configuration
 */
struct DbPoolConfig {
    std::string dbType;           // "postgres" or "oracle"

    // Common settings
    size_t minSize = 2;
    size_t maxSize = 10;
    int acquireTimeoutSec = 5;

    // PostgreSQL settings
    std::string pgHost;
    int pgPort = 5432;
    std::string pgDatabase;
    std::string pgUser;
    std::string pgPassword;

    // Oracle settings
    std::string oracleHost;
    int oraclePort = 1521;
    std::string oracleServiceName;
    std::string oracleUser;
    std::string oraclePassword;

    /**
     * @brief Build PostgreSQL connection string
     */
    std::string buildPostgresConnString() const;

    /**
     * @brief Build Oracle connection string
     */
    std::string buildOracleConnString() const;

    /**
     * @brief Create config from environment variables
     *
     * Reads:
     * - DB_TYPE (postgres/oracle)
     * - DB_POOL_MIN, DB_POOL_MAX, DB_POOL_TIMEOUT (connection pool size)
     * - DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASSWORD (PostgreSQL)
     * - ORACLE_HOST, ORACLE_PORT, ORACLE_SERVICE_NAME, ORACLE_USER, ORACLE_PASSWORD (Oracle)
     */
    static DbPoolConfig fromEnvironment();
};

/**
 * @brief Database Connection Pool Factory (Strategy Pattern)
 *
 * Creates appropriate connection pool based on database type
 *
 * Usage Example:
 * @code
 * DbPoolConfig config = DbPoolConfig::fromEnvironment();
 * auto pool = DbConnectionPoolFactory::create(config);
 * if (pool && pool->initialize()) {
 *     auto conn = pool->acquireGeneric();
 *     // Use connection
 * }
 * @endcode
 */
class DbConnectionPoolFactory {
public:
    /**
     * @brief Create connection pool based on config
     *
     * @param config Pool configuration
     * @return Connection pool or nullptr on error
     *
     * Supported database types:
     * - "postgres", "postgresql", "pg" → PostgreSQL pool
     * - "oracle", "ora" → Oracle pool
     */
    static std::shared_ptr<IDbConnectionPool> create(const DbPoolConfig& config);

    /**
     * @brief Create connection pool from environment variables
     *
     * @return Connection pool or nullptr on error
     */
    static std::shared_ptr<IDbConnectionPool> createFromEnv();

    /**
     * @brief Check if database type is supported
     */
    static bool isSupported(const std::string& dbType);

    /**
     * @brief Get list of supported database types
     */
    static std::vector<std::string> getSupportedTypes();

private:
    /**
     * @brief Normalize database type string
     * "postgres", "postgresql", "pg" → "postgres"
     * "oracle", "ora" → "oracle"
     */
    static std::string normalizeDbType(const std::string& dbType);
};

} // namespace common

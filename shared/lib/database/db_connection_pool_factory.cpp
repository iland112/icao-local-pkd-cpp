/**
 * @file db_connection_pool_factory.cpp
 * @brief Database Connection Pool Factory Implementation
 */

#include "db_connection_pool_factory.h"
#include "db_connection_pool.h"
#ifdef ENABLE_ORACLE
#include "oracle_connection_pool.h"
#endif
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace common {

// --- DbPoolConfig Implementation ---

std::string DbPoolConfig::buildPostgresConnString() const {
    std::ostringstream oss;
    oss << "host=" << pgHost
        << " port=" << pgPort
        << " dbname=" << pgDatabase
        << " user=" << pgUser
        << " password=" << pgPassword;
    return oss.str();
}

std::string DbPoolConfig::buildOracleConnString() const {
    // Format: user/password@host:port/service_name
    std::ostringstream oss;
    oss << oracleUser << "/"
        << oraclePassword << "@"
        << oracleHost << ":"
        << oraclePort << "/"
        << oracleServiceName;
    return oss.str();
}

DbPoolConfig DbPoolConfig::fromEnvironment() {
    DbPoolConfig config;

    // Read DB_TYPE
    const char* dbType = std::getenv("DB_TYPE");
    config.dbType = dbType ? dbType : "postgres";

    // Connection pool size settings (shared across DB types)
    if (auto* v = std::getenv("DB_POOL_MIN")) config.minSize = std::stoul(v);
    if (auto* v = std::getenv("DB_POOL_MAX")) config.maxSize = std::stoul(v);
    if (auto* v = std::getenv("DB_POOL_TIMEOUT")) config.acquireTimeoutSec = std::stoi(v);

    // PostgreSQL settings
    const char* pgHost = std::getenv("DB_HOST");
    config.pgHost = pgHost ? pgHost : "localhost";

    const char* pgPort = std::getenv("DB_PORT");
    config.pgPort = (pgPort && pgPort[0] != '\0') ? std::stoi(pgPort) : 5432;

    const char* pgDb = std::getenv("DB_NAME");
    config.pgDatabase = pgDb ? pgDb : "localpkd";

    const char* pgUser = std::getenv("DB_USER");
    config.pgUser = pgUser ? pgUser : "pkd";

    const char* pgPass = std::getenv("DB_PASSWORD");
    config.pgPassword = pgPass ? pgPass : "";

    // Oracle settings
    const char* oracleHost = std::getenv("ORACLE_HOST");
    config.oracleHost = oracleHost ? oracleHost : "localhost";

    const char* oraclePort = std::getenv("ORACLE_PORT");
    config.oraclePort = (oraclePort && oraclePort[0] != '\0') ? std::stoi(oraclePort) : 1521;

    const char* oracleSvc = std::getenv("ORACLE_SERVICE_NAME");
    config.oracleServiceName = oracleSvc ? oracleSvc : "XEPDB1";

    const char* oracleUser = std::getenv("ORACLE_USER");
    config.oracleUser = oracleUser ? oracleUser : "pkd";

    const char* oraclePass = std::getenv("ORACLE_PASSWORD");
    config.oraclePassword = oraclePass ? oraclePass : "";

    return config;
}

// --- DbConnectionPoolFactory Implementation ---

std::shared_ptr<IDbConnectionPool> DbConnectionPoolFactory::create(const DbPoolConfig& config) {
    std::string normalizedType = normalizeDbType(config.dbType);

    if (normalizedType == "postgres") {
        std::string connStr = config.buildPostgresConnString();
        return std::make_shared<DbConnectionPool>(
            connStr,
            config.minSize,
            config.maxSize,
            config.acquireTimeoutSec
        );
    }
#ifdef ENABLE_ORACLE
    else if (normalizedType == "oracle") {
        std::string connStr = config.buildOracleConnString();
        return std::make_shared<OracleConnectionPool>(
            connStr,
            config.minSize,
            config.maxSize,
            config.acquireTimeoutSec
        );
    }
#endif
    else {
        throw std::runtime_error("Unsupported database type: " + config.dbType);
    }
}

std::shared_ptr<IDbConnectionPool> DbConnectionPoolFactory::createFromEnv() {
    DbPoolConfig config = DbPoolConfig::fromEnvironment();
    return create(config);
}

bool DbConnectionPoolFactory::isSupported(const std::string& dbType) {
    std::string normalized = normalizeDbType(dbType);
    if (normalized == "postgres") return true;
#ifdef ENABLE_ORACLE
    if (normalized == "oracle") return true;
#endif
    return false;
}

std::vector<std::string> DbConnectionPoolFactory::getSupportedTypes() {
    std::vector<std::string> types = {"postgres", "postgresql", "pg"};
#ifdef ENABLE_ORACLE
    types.insert(types.end(), {"oracle", "ora"});
#endif
    return types;
}

std::string DbConnectionPoolFactory::normalizeDbType(const std::string& dbType) {
    std::string lower = dbType;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "postgres" || lower == "postgresql" || lower == "pg") {
        return "postgres";
    }
    else if (lower == "oracle" || lower == "ora") {
        return "oracle";
    }
    else {
        return lower;  // Return as-is for error handling
    }
}

} // namespace common

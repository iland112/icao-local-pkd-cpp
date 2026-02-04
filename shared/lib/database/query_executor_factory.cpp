#include "i_query_executor.h"
#include "postgresql_query_executor.h"
#include "oracle_query_executor.h"
#include "db_connection_interface.h"
#include "db_connection_pool.h"
#include "oracle_connection_pool.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

/**
 * @file query_executor_factory.cpp
 * @brief Query Executor Factory Implementation
 *
 * Creates appropriate QueryExecutor based on connection pool type.
 *
 * @note Part of Oracle migration Phase 3
 * @date 2026-02-04
 */

namespace common {

std::unique_ptr<IQueryExecutor> createQueryExecutor(IDbConnectionPool* pool)
{
    if (!pool) {
        throw std::invalid_argument("createQueryExecutor: pool cannot be nullptr");
    }

    std::string dbType = pool->getDatabaseType();
    spdlog::debug("[QueryExecutorFactory] Creating executor for database type: {}", dbType);

    if (dbType == "postgres") {
        // Downcast to PostgreSQL pool
        auto pgPool = dynamic_cast<DbConnectionPool*>(pool);
        if (!pgPool) {
            throw std::runtime_error("createQueryExecutor: Failed to cast to PostgreSQL pool");
        }
        return std::make_unique<PostgreSQLQueryExecutor>(pgPool);
    }
    else if (dbType == "oracle") {
        // Downcast to Oracle pool
        auto oraclePool = dynamic_cast<OracleConnectionPool*>(pool);
        if (!oraclePool) {
            throw std::runtime_error("createQueryExecutor: Failed to cast to Oracle pool");
        }
        return std::make_unique<OracleQueryExecutor>(oraclePool);
    }
    else {
        throw std::runtime_error("createQueryExecutor: Unsupported database type: " + dbType);
    }
}

} // namespace common

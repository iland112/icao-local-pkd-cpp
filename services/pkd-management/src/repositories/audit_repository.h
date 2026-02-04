#pragma once

#include <string>
#include <vector>
#include <json/json.h>
#include "i_query_executor.h"

/**
 * @file audit_repository.h
 * @brief Audit Repository - Database Access Layer for operation_audit_log table
 *
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @note Part of Oracle migration Phase 3: Query Executor Pattern
 * @date 2026-02-04
 */

namespace repositories {

class AuditRepository {
public:
    /**
     * @brief Constructor
     * @param queryExecutor Query executor (PostgreSQL or Oracle, non-owning pointer)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit AuditRepository(common::IQueryExecutor* queryExecutor);
    ~AuditRepository() = default;

    /**
     * @brief Insert audit log
     */
    bool insert(
        const std::string& operationType,
        const std::string& username,
        const std::string& ipAddress,
        bool success,
        const std::string& errorMessage,
        const std::string& metadata,  // JSON string
        int durationMs
    );

    /**
     * @brief Find audit logs with filter
     * @param limit Maximum number of records
     * @param offset Offset for pagination
     * @param operationType Filter by operation type (empty = all)
     * @param username Filter by username (empty = all)
     * @param successFilter Filter by success status ("true", "false", or empty = all)
     * @return JSON array of audit logs
     */
    Json::Value findAll(
        int limit,
        int offset,
        const std::string& operationType = "",
        const std::string& username = "",
        const std::string& successFilter = ""
    );

    /**
     * @brief Count audit logs with filter
     * @param operationType Filter by operation type (empty = all)
     * @param username Filter by username (empty = all)
     * @param successFilter Filter by success status ("true", "false", or empty = all)
     * @return Total count of matching records
     */
    int countAll(
        const std::string& operationType = "",
        const std::string& username = "",
        const std::string& successFilter = ""
    );

    /**
     * @brief Count by operation type
     */
    int countByOperationType(const std::string& operationType);

    /**
     * @brief Get operation statistics
     */
    Json::Value getStatistics(const std::string& startDate, const std::string& endDate);

private:
    common::IQueryExecutor* queryExecutor_;  // Query executor (non-owning)

    /**
     * @brief Helper function to convert snake_case to camelCase
     * @param snake_case Input string in snake_case format
     * @return String in camelCase format
     */
    std::string toCamelCase(const std::string& snake_case);
};

} // namespace repositories

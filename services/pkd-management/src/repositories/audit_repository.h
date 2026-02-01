#pragma once

#include <string>
#include <vector>
#include <libpq-fe.h>
#include <json/json.h>

/**
 * @file audit_repository.h
 * @brief Audit Repository - Database Access Layer for operation_audit_log table
 *
 * @note Part of main.cpp refactoring Phase 1.5
 * @date 2026-01-29
 */

namespace repositories {

class AuditRepository {
public:
    explicit AuditRepository(PGconn* dbConn);
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
    PGconn* dbConn_;

    PGresult* executeParamQuery(const std::string& query, const std::vector<std::string>& params);
    PGresult* executeQuery(const std::string& query);
    Json::Value pgResultToJson(PGresult* res);
};

} // namespace repositories

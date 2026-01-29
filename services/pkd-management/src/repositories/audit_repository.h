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
     */
    Json::Value findAll(
        int limit,
        int offset,
        const std::string& operationType = "",
        const std::string& username = ""
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

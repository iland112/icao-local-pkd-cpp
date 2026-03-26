/**
 * @file sync_status_repository.h
 * @brief Repository for sync_status table operations
 */
#pragma once

#include "../domain/models/sync_status.h"
#include "i_query_executor.h"
#include <memory>
#include <vector>
#include <optional>
#include <json/json.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for sync_status table operations (Database-agnostic)
 *
 * Handles all database operations for sync status tracking.
 * All queries use parameterized statements for SQL injection prevention.
 * Uses Query Executor Pattern for database independence (PostgreSQL/Oracle).
 */
class SyncStatusRepository {
public:
    /**
     * @brief Constructor with Query Executor injection
     * @param executor Query Executor (must remain valid during repository lifetime)
     * @throws std::invalid_argument if executor is nullptr
     */
    explicit SyncStatusRepository(common::IQueryExecutor* executor);

    /**
     * @brief Destructor
     */
    ~SyncStatusRepository() = default;

    /// @name Non-copyable and non-movable
    /// @{
    SyncStatusRepository(const SyncStatusRepository&) = delete;
    SyncStatusRepository& operator=(const SyncStatusRepository&) = delete;
    SyncStatusRepository(SyncStatusRepository&&) = delete;
    SyncStatusRepository& operator=(SyncStatusRepository&&) = delete;
    /// @}

    /**
     * @brief Save new sync check result
     * @param syncStatus The sync status to save (id will be set after insert)
     * @return true if successful, false otherwise
     */
    bool create(domain::SyncStatus& syncStatus);

    /**
     * @brief Get the most recent sync status
     * @return Optional containing latest sync status, or nullopt if none exists
     */
    std::optional<domain::SyncStatus> findLatest();

    /**
     * @brief Get sync history with pagination
     * @param limit Maximum number of results
     * @param offset Number of results to skip
     * @return Vector of sync status records
     */
    std::vector<domain::SyncStatus> findAll(int limit = 50, int offset = 0);

    /**
     * @brief Get total count of sync status records
     * @return Total number of records
     */
    int count();

private:
    /**
     * @brief Convert database result row (JSON) to SyncStatus domain object
     * @param row Database result row as JSON
     * @return SyncStatus domain object
     */
    domain::SyncStatus jsonToSyncStatus(const Json::Value& row);

    common::IQueryExecutor* queryExecutor_;  // Not owned - do not free
};


} // namespace icao::relay::repositories

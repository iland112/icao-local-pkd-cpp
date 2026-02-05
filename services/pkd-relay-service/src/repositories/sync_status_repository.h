#pragma once

#include "../domain/models/sync_status.h"
#include "db_connection_pool.h"
#include "db_connection_interface.h"  // Phase 4.4: Interface for PostgreSQL/Oracle support
#include <memory>
#include <vector>
#include <optional>
#include <libpq-fe.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for sync_status table operations
 *
 * Handles all database operations for sync status tracking.
 * All queries use parameterized statements for SQL injection prevention.
 *
 * Thread-safe: Uses DbConnectionPool for concurrent request handling.
 */
class SyncStatusRepository {
public:
    /**
     * @brief Constructor (Phase 4.4: Supports PostgreSQL and Oracle via interface)
     * @param dbPool Shared database connection pool (IDbConnectionPool interface)
     */
    explicit SyncStatusRepository(std::shared_ptr<common::IDbConnectionPool> dbPool);

    /**
     * @brief Destructor
     */
    ~SyncStatusRepository() = default;

    // Disable copy and move
    SyncStatusRepository(const SyncStatusRepository&) = delete;
    SyncStatusRepository& operator=(const SyncStatusRepository&) = delete;
    SyncStatusRepository(SyncStatusRepository&&) = delete;
    SyncStatusRepository& operator=(SyncStatusRepository&&) = delete;

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
     * @brief Convert PostgreSQL result row to SyncStatus domain object
     * @param res PGresult pointer
     * @param row Row number in result set
     * @return SyncStatus domain object
     */
    domain::SyncStatus resultToSyncStatus(PGresult* res, int row);

    std::shared_ptr<common::IDbConnectionPool> dbPool_;  // Phase 4.4: Interface for PostgreSQL/Oracle
};


} // namespace icao::relay::repositories

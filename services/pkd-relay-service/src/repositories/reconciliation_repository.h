#pragma once

#include "../domain/models/reconciliation_summary.h"
#include "../domain/models/reconciliation_log.h"
#include "i_query_executor.h"
#include <memory>
#include <vector>
#include <optional>
#include <json/json.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for reconciliation_summary and reconciliation_log tables (Database-agnostic)
 *
 * Handles all database operations for reconciliation tracking.
 * All queries use parameterized statements for SQL injection prevention.
 * Uses Query Executor Pattern for database independence (PostgreSQL/Oracle).
 *
 * @date 2026-02-05 (Phase 5.2: Query Executor Pattern)
 */
class ReconciliationRepository {
public:
    /**
     * @brief Constructor with Query Executor injection
     * @param executor Query Executor (must remain valid during repository lifetime)
     * @throws std::invalid_argument if executor is nullptr
     */
    explicit ReconciliationRepository(common::IQueryExecutor* executor);

    /**
     * @brief Destructor
     */
    ~ReconciliationRepository() = default;

    // Disable copy and move
    ReconciliationRepository(const ReconciliationRepository&) = delete;
    ReconciliationRepository& operator=(const ReconciliationRepository&) = delete;
    ReconciliationRepository(ReconciliationRepository&&) = delete;
    ReconciliationRepository& operator=(ReconciliationRepository&&) = delete;

    // ========================================================================
    // ReconciliationSummary Operations
    // ========================================================================

    /**
     * @brief Create new reconciliation summary record
     * @param summary The summary to create (id will be set after insert)
     * @return true if successful, false otherwise
     */
    bool createSummary(domain::ReconciliationSummary& summary);

    /**
     * @brief Update existing reconciliation summary
     * @param summary The summary to update
     * @return true if successful, false otherwise
     */
    bool updateSummary(const domain::ReconciliationSummary& summary);

    /**
     * @brief Find reconciliation summary by ID
     * @param id Reconciliation ID
     * @return Optional containing summary, or nullopt if not found
     */
    std::optional<domain::ReconciliationSummary> findSummaryById(const std::string& id);

    /**
     * @brief Get reconciliation history with pagination
     * @param limit Maximum number of results
     * @param offset Number of results to skip
     * @return Vector of reconciliation summaries
     */
    std::vector<domain::ReconciliationSummary> findAllSummaries(int limit = 50, int offset = 0);

    /**
     * @brief Get total count of reconciliation summaries
     * @return Total number of records
     */
    int countSummaries();

    // ========================================================================
    // ReconciliationLog Operations
    // ========================================================================

    /**
     * @brief Create reconciliation log entry
     * @param log The log entry to create (id will be set after insert)
     * @return true if successful, false otherwise
     */
    bool createLog(domain::ReconciliationLog& log);

    /**
     * @brief Find logs for a specific reconciliation
     * @param reconciliationId Reconciliation ID
     * @param limit Maximum number of results (default: 1000)
     * @param offset Number of results to skip (default: 0)
     * @return Vector of reconciliation logs
     */
    std::vector<domain::ReconciliationLog> findLogsByReconciliationId(
        const std::string& reconciliationId,
        int limit = 1000,
        int offset = 0
    );

    /**
     * @brief Count logs for a specific reconciliation
     * @param reconciliationId Reconciliation ID
     * @return Number of log entries
     */
    int countLogsByReconciliationId(const std::string& reconciliationId);

private:
    /**
     * @brief Convert database result row (JSON) to ReconciliationSummary domain object
     * @param row Database result row as JSON
     * @return ReconciliationSummary domain object
     */
    domain::ReconciliationSummary jsonToSummary(const Json::Value& row);

    /**
     * @brief Convert database result row (JSON) to ReconciliationLog domain object
     * @param row Database result row as JSON
     * @return ReconciliationLog domain object
     */
    domain::ReconciliationLog jsonToLog(const Json::Value& row);

    common::IQueryExecutor* queryExecutor_;  // Not owned - do not free
};

} // namespace icao::relay::repositories

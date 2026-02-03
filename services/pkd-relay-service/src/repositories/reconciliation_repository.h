#pragma once

#include "../domain/models/reconciliation_summary.h"
#include "../domain/models/reconciliation_log.h"
#include <memory>
#include <vector>
#include <optional>
#include <libpq-fe.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for reconciliation_summary and reconciliation_log tables
 *
 * Handles all database operations for reconciliation tracking.
 * All queries use parameterized statements for SQL injection prevention.
 */
class ReconciliationRepository {
public:
    /**
     * @brief Constructor
     * @param conninfo PostgreSQL connection string
     */
    explicit ReconciliationRepository(const std::string& conninfo);

    /**
     * @brief Destructor - closes database connection
     */
    ~ReconciliationRepository();

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
    std::optional<domain::ReconciliationSummary> findSummaryById(int id);

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
        int reconciliationId,
        int limit = 1000,
        int offset = 0
    );

    /**
     * @brief Count logs for a specific reconciliation
     * @param reconciliationId Reconciliation ID
     * @return Number of log entries
     */
    int countLogsByReconciliationId(int reconciliationId);

private:
    /**
     * @brief Convert PostgreSQL result row to ReconciliationSummary domain object
     * @param res PGresult pointer
     * @param row Row number in result set
     * @return ReconciliationSummary domain object
     */
    domain::ReconciliationSummary resultToSummary(PGresult* res, int row);

    /**
     * @brief Convert PostgreSQL result row to ReconciliationLog domain object
     * @param res PGresult pointer
     * @param row Row number in result set
     * @return ReconciliationLog domain object
     */
    domain::ReconciliationLog resultToLog(PGresult* res, int row);

    /**
     * @brief Get database connection (reconnects if needed)
     * @return PGconn pointer
     */
    PGconn* getConnection();

    std::string conninfo_;
    PGconn* conn_ = nullptr;
};

} // namespace icao::relay::repositories

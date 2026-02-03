#include "reconciliation_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>

namespace icao::relay::repositories {

ReconciliationRepository::ReconciliationRepository(const std::string& conninfo)
    : conninfo_(conninfo) {
    conn_ = PQconnectdb(conninfo_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string error = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("Database connection failed: " + error);
    }
}

ReconciliationRepository::~ReconciliationRepository() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

PGconn* ReconciliationRepository::getConnection() {
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        if (conn_) {
            PQfinish(conn_);
        }
        conn_ = PQconnectdb(conninfo_.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string error = PQerrorMessage(conn_);
            PQfinish(conn_);
            conn_ = nullptr;
            throw std::runtime_error("Database reconnection failed: " + error);
        }
    }
    return conn_;
}

// ========================================================================
// ReconciliationSummary Operations
// ========================================================================

bool ReconciliationRepository::createSummary(domain::ReconciliationSummary& summary) {
    try {
        PGconn* conn = getConnection();

        const char* query =
            "INSERT INTO reconciliation_summary ("
            "triggered_by, triggered_at, status, dry_run, "
            "success_count, failed_count, "
            "csca_added, dsc_added, dsc_nc_added, crl_added, total_added, "
            "csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted"
            ") VALUES ("
            "$1, NOW(), $2, $3, "
            "$4, $5, "
            "$6, $7, $8, $9, $10, "
            "$11, $12, $13, $14"
            ") RETURNING id, triggered_at";

        const char* dryRun = summary.isDryRun() ? "true" : "false";

        std::string successCount = std::to_string(summary.getSuccessCount());
        std::string failedCount = std::to_string(summary.getFailedCount());
        std::string cscaAdded = std::to_string(summary.getCscaAdded());
        std::string dscAdded = std::to_string(summary.getDscAdded());
        std::string dscNcAdded = std::to_string(summary.getDscNcAdded());
        std::string crlAdded = std::to_string(summary.getCrlAdded());
        std::string totalAdded = std::to_string(summary.getTotalAdded());
        std::string cscaDeleted = std::to_string(summary.getCscaDeleted());
        std::string dscDeleted = std::to_string(summary.getDscDeleted());
        std::string dscNcDeleted = std::to_string(summary.getDscNcDeleted());
        std::string crlDeleted = std::to_string(summary.getCrlDeleted());

        const char* paramValues[14] = {
            summary.getTriggeredBy().c_str(),
            summary.getStatus().c_str(),
            dryRun,
            successCount.c_str(),
            failedCount.c_str(),
            cscaAdded.c_str(),
            dscAdded.c_str(),
            dscNcAdded.c_str(),
            crlAdded.c_str(),
            totalAdded.c_str(),
            cscaDeleted.c_str(),
            dscDeleted.c_str(),
            dscNcDeleted.c_str(),
            crlDeleted.c_str()
        };

        PGresult* res = PQexecParams(
            conn, query, 14, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to create summary: {}", error);
            return false;
        }

        // Update domain object with generated id
        // Note: triggered_at is already set in the constructor and doesn't need updating
        if (PQntuples(res) > 0) {
            int id = std::atoi(PQgetvalue(res, 0, 0));
            summary.setId(id);
        }

        PQclear(res);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in createSummary(): {}", e.what());
        return false;
    }
}

bool ReconciliationRepository::updateSummary(const domain::ReconciliationSummary& summary) {
    try {
        PGconn* conn = getConnection();

        const char* query =
            "UPDATE reconciliation_summary SET "
            "status = $1, "
            "completed_at = $2, "
            "success_count = $3, "
            "failed_count = $4, "
            "csca_added = $5, "
            "dsc_added = $6, "
            "dsc_nc_added = $7, "
            "crl_added = $8, "
            "total_added = $9, "
            "csca_deleted = $10, "
            "dsc_deleted = $11, "
            "dsc_nc_deleted = $12, "
            "crl_deleted = $13 "
            "WHERE id = $14";

        // Format completed_at timestamp
        std::string completedAtStr = "NULL";
        if (summary.getCompletedAt().has_value()) {
            auto tp = summary.getCompletedAt().value();
            std::time_t t = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = *std::localtime(&t);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            completedAtStr = buf;
        }

        std::string successCount = std::to_string(summary.getSuccessCount());
        std::string failedCount = std::to_string(summary.getFailedCount());
        std::string cscaAdded = std::to_string(summary.getCscaAdded());
        std::string dscAdded = std::to_string(summary.getDscAdded());
        std::string dscNcAdded = std::to_string(summary.getDscNcAdded());
        std::string crlAdded = std::to_string(summary.getCrlAdded());
        std::string totalAdded = std::to_string(summary.getTotalAdded());
        std::string cscaDeleted = std::to_string(summary.getCscaDeleted());
        std::string dscDeleted = std::to_string(summary.getDscDeleted());
        std::string dscNcDeleted = std::to_string(summary.getDscNcDeleted());
        std::string crlDeleted = std::to_string(summary.getCrlDeleted());
        std::string id = std::to_string(summary.getId());

        const char* paramValues[14] = {
            summary.getStatus().c_str(),
            completedAtStr == "NULL" ? nullptr : completedAtStr.c_str(),
            successCount.c_str(),
            failedCount.c_str(),
            cscaAdded.c_str(),
            dscAdded.c_str(),
            dscNcAdded.c_str(),
            crlAdded.c_str(),
            totalAdded.c_str(),
            cscaDeleted.c_str(),
            dscDeleted.c_str(),
            dscNcDeleted.c_str(),
            crlDeleted.c_str(),
            id.c_str()
        };

        PGresult* res = PQexecParams(
            conn, query, 14, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to update summary: {}", error);
            return false;
        }

        PQclear(res);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in updateSummary(): {}", e.what());
        return false;
    }
}

std::optional<domain::ReconciliationSummary> ReconciliationRepository::findSummaryById(int id) {
    try {
        PGconn* conn = getConnection();

        const char* query =
            "SELECT id, triggered_by, triggered_at, completed_at, status, dry_run, "
            "success_count, failed_count, "
            "csca_added, dsc_added, dsc_nc_added, crl_added, total_added, "
            "csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted, "
            "duration_ms, error_message, sync_status_id "
            "FROM reconciliation_summary "
            "WHERE id = $1";

        std::string idStr = std::to_string(id);

        const char* paramValues[1] = {
            idStr.c_str()
        };

        PGresult* res = PQexecParams(
            conn, query, 1, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to find summary by id: {}", error);
            return std::nullopt;
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            return std::nullopt;
        }

        auto summary = resultToSummary(res, 0);
        PQclear(res);
        return summary;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in findSummaryById(): {}", e.what());
        return std::nullopt;
    }
}

std::vector<domain::ReconciliationSummary> ReconciliationRepository::findAllSummaries(int limit, int offset) {
    std::vector<domain::ReconciliationSummary> results;

    try {
        PGconn* conn = getConnection();

        const char* query =
            "SELECT id, triggered_by, triggered_at, completed_at, status, dry_run, "
            "success_count, failed_count, "
            "csca_added, dsc_added, dsc_nc_added, crl_added, total_added, "
            "csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted, "
            "duration_ms, error_message, sync_status_id "
            "FROM reconciliation_summary "
            "ORDER BY triggered_at DESC "
            "LIMIT $1 OFFSET $2";

        std::string limitStr = std::to_string(limit);
        std::string offsetStr = std::to_string(offset);

        const char* paramValues[2] = {
            limitStr.c_str(),
            offsetStr.c_str()
        };

        PGresult* res = PQexecParams(
            conn, query, 2, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to find all summaries: {}", error);
            return results;
        }

        int rowCount = PQntuples(res);
        for (int i = 0; i < rowCount; i++) {
            results.push_back(resultToSummary(res, i));
        }

        PQclear(res);
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in findAllSummaries(): {}", e.what());
        return results;
    }
}

int ReconciliationRepository::countSummaries() {
    try {
        PGconn* conn = getConnection();

        const char* query = "SELECT COUNT(*) FROM reconciliation_summary";

        PGresult* res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to count summaries: {}", error);
            return 0;
        }

        int count = 0;
        if (PQntuples(res) > 0) {
            count = std::atoi(PQgetvalue(res, 0, 0));
        }

        PQclear(res);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in countSummaries(): {}", e.what());
        return 0;
    }
}

// ========================================================================
// ReconciliationLog Operations
// ========================================================================

bool ReconciliationRepository::createLog(domain::ReconciliationLog& log) {
    try {
        PGconn* conn = getConnection();

        const char* query =
            "INSERT INTO reconciliation_log ("
            "reconciliation_id, created_at, cert_fingerprint, cert_type, country_code, "
            "action, result, error_message"
            ") VALUES ("
            "$1, NOW(), $2, $3, $4, "
            "$5, $6, $7"
            ") RETURNING id, created_at";

        std::string reconciliationId = std::to_string(log.getReconciliationId());

        const char* paramValues[7] = {
            reconciliationId.c_str(),
            log.getCertFingerprint().c_str(),
            log.getCertType().c_str(),
            log.getCountryCode().c_str(),
            log.getAction().c_str(),
            log.getResult().c_str(),
            log.getErrorMessage().has_value() ? log.getErrorMessage().value().c_str() : nullptr
        };

        PGresult* res = PQexecParams(
            conn, query, 7, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to create log: {}", error);
            return false;
        }

        // Update domain object with generated id and timestamp
        if (PQntuples(res) > 0) {
            int id = std::atoi(PQgetvalue(res, 0, 0));
            log.setId(id);

            const char* createdAtStr = PQgetvalue(res, 0, 1);
            std::tm tm = {};
            if (strptime(createdAtStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
                auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                // Note: ReconciliationLog doesn't have setCreatedAt in our model
                // If needed, add it to the model
            }
        }

        PQclear(res);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in createLog(): {}", e.what());
        return false;
    }
}

std::vector<domain::ReconciliationLog> ReconciliationRepository::findLogsByReconciliationId(
    int reconciliationId,
    int limit,
    int offset
) {
    std::vector<domain::ReconciliationLog> results;

    try {
        PGconn* conn = getConnection();

        const char* query =
            "SELECT id, reconciliation_id, created_at, cert_fingerprint, cert_type, "
            "country_code, action, result, error_message "
            "FROM reconciliation_log "
            "WHERE reconciliation_id = $1 "
            "ORDER BY created_at ASC "
            "LIMIT $2 OFFSET $3";

        std::string reconciliationIdStr = std::to_string(reconciliationId);
        std::string limitStr = std::to_string(limit);
        std::string offsetStr = std::to_string(offset);

        const char* paramValues[3] = {
            reconciliationIdStr.c_str(),
            limitStr.c_str(),
            offsetStr.c_str()
        };

        PGresult* res = PQexecParams(
            conn, query, 3, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to find logs: {}", error);
            return results;
        }

        int rowCount = PQntuples(res);
        for (int i = 0; i < rowCount; i++) {
            results.push_back(resultToLog(res, i));
        }

        PQclear(res);
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in findLogsByReconciliationId(): {}", e.what());
        return results;
    }
}

int ReconciliationRepository::countLogsByReconciliationId(int reconciliationId) {
    try {
        PGconn* conn = getConnection();

        const char* query = "SELECT COUNT(*) FROM reconciliation_log WHERE reconciliation_id = $1";

        std::string reconciliationIdStr = std::to_string(reconciliationId);

        const char* paramValues[1] = {
            reconciliationIdStr.c_str()
        };

        PGresult* res = PQexecParams(
            conn, query, 1, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[ReconciliationRepository] Failed to count logs: {}", error);
            return 0;
        }

        int count = 0;
        if (PQntuples(res) > 0) {
            count = std::atoi(PQgetvalue(res, 0, 0));
        }

        PQclear(res);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in countLogsByReconciliationId(): {}", e.what());
        return 0;
    }
}

// ========================================================================
// Helper Methods
// ========================================================================

domain::ReconciliationSummary ReconciliationRepository::resultToSummary(PGresult* res, int row) {
    // Parse all fields from result set
    int id = std::atoi(PQgetvalue(res, row, 0));
    std::string triggeredBy = PQgetvalue(res, row, 1);

    // Parse timestamps
    const char* triggeredAtStr = PQgetvalue(res, row, 2);
    std::tm tm = {};
    auto triggeredAt = std::chrono::system_clock::now();
    if (strptime(triggeredAtStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        triggeredAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    std::optional<std::chrono::system_clock::time_point> completedAt = std::nullopt;
    if (!PQgetisnull(res, row, 3)) {
        const char* completedAtStr = PQgetvalue(res, row, 3);
        tm = {};
        if (strptime(completedAtStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
            completedAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
    }

    std::string status = PQgetvalue(res, row, 4);
    bool dryRun = (strcmp(PQgetvalue(res, row, 5), "t") == 0);

    int successCount = std::atoi(PQgetvalue(res, row, 6));
    int failedCount = std::atoi(PQgetvalue(res, row, 7));
    int cscaAdded = std::atoi(PQgetvalue(res, row, 8));
    int dscAdded = std::atoi(PQgetvalue(res, row, 9));
    int dscNcAdded = std::atoi(PQgetvalue(res, row, 10));
    int crlAdded = std::atoi(PQgetvalue(res, row, 11));
    int totalAdded = std::atoi(PQgetvalue(res, row, 12));
    int cscaDeleted = std::atoi(PQgetvalue(res, row, 13));
    int dscDeleted = std::atoi(PQgetvalue(res, row, 14));
    int dscNcDeleted = std::atoi(PQgetvalue(res, row, 15));
    int crlDeleted = std::atoi(PQgetvalue(res, row, 16));

    int durationMs = std::atoi(PQgetvalue(res, row, 17));

    // Parse optional error_message
    std::optional<std::string> errorMessage;
    if (!PQgetisnull(res, row, 18)) {
        const char* errorMessageStr = PQgetvalue(res, row, 18);
        if (errorMessageStr && strlen(errorMessageStr) > 0) {
            errorMessage = std::string(errorMessageStr);
        }
    }

    // Parse optional sync_status_id
    std::optional<int> syncStatusId;
    if (!PQgetisnull(res, row, 19)) {
        syncStatusId = std::atoi(PQgetvalue(res, row, 19));
    }

    // Construct and return domain object using constructor
    return domain::ReconciliationSummary(
        id, triggeredBy, triggeredAt, completedAt,
        status, dryRun,
        successCount, failedCount,
        cscaAdded, cscaDeleted,
        dscAdded, dscDeleted,
        dscNcAdded, dscNcDeleted,
        crlAdded, crlDeleted,
        totalAdded,
        durationMs, errorMessage, syncStatusId
    );
}

domain::ReconciliationLog ReconciliationRepository::resultToLog(PGresult* res, int row) {
    int id = std::atoi(PQgetvalue(res, row, 0));
    int reconciliationId = std::atoi(PQgetvalue(res, row, 1));

    // Parse timestamp
    const char* createdAtStr = PQgetvalue(res, row, 2);
    std::tm tm = {};
    auto createdAt = std::chrono::system_clock::now();
    if (strptime(createdAtStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        createdAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    std::string certFingerprint = PQgetvalue(res, row, 3);
    std::string certType = PQgetvalue(res, row, 4);
    std::string countryCode = PQgetvalue(res, row, 5);
    std::string action = PQgetvalue(res, row, 6);
    std::string result = PQgetvalue(res, row, 7);

    std::optional<std::string> errorMessage = std::nullopt;
    if (!PQgetisnull(res, row, 8)) {
        errorMessage = PQgetvalue(res, row, 8);
    }

    return domain::ReconciliationLog(
        id,
        reconciliationId,
        createdAt,
        certFingerprint,
        certType,
        countryCode,
        action,
        result,
        errorMessage
    );
}

} // namespace icao::relay::repositories

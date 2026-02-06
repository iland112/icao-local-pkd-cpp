#include "reconciliation_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace icao::relay::repositories {

ReconciliationRepository::ReconciliationRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("ReconciliationRepository: queryExecutor cannot be nullptr");
    }

    spdlog::debug("[ReconciliationRepository] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

// ========================================================================
// ReconciliationSummary Operations
// ========================================================================

bool ReconciliationRepository::createSummary(domain::ReconciliationSummary& summary) {
    try {
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

        std::vector<std::string> params = {
            summary.getTriggeredBy(),                           // $1
            summary.getStatus(),                                 // $2
            summary.isDryRun() ? "true" : "false",              // $3
            std::to_string(summary.getSuccessCount()),          // $4
            std::to_string(summary.getFailedCount()),           // $5
            std::to_string(summary.getCscaAdded()),             // $6
            std::to_string(summary.getDscAdded()),              // $7
            std::to_string(summary.getDscNcAdded()),            // $8
            std::to_string(summary.getCrlAdded()),              // $9
            std::to_string(summary.getTotalAdded()),            // $10
            std::to_string(summary.getCscaDeleted()),           // $11
            std::to_string(summary.getDscDeleted()),            // $12
            std::to_string(summary.getDscNcDeleted()),          // $13
            std::to_string(summary.getCrlDeleted())             // $14
        };

        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::error("[ReconciliationRepository] Insert returned no rows");
            return false;
        }

        // Update domain object with generated id
        std::string id = result[0]["id"].asString();
        summary.setId(id);

        spdlog::info("[ReconciliationRepository] Reconciliation summary created with ID: {}", id);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in createSummary(): {}", e.what());
        return false;
    }
}

bool ReconciliationRepository::updateSummary(const domain::ReconciliationSummary& summary) {
    try {
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
        std::string completedAtStr = "";
        if (summary.getCompletedAt().has_value()) {
            auto tp = summary.getCompletedAt().value();
            std::time_t t = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = *std::localtime(&t);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            completedAtStr = buf;
        }

        std::vector<std::string> params = {
            summary.getStatus(),                                // $1
            completedAtStr,                                     // $2 (empty string for NULL)
            std::to_string(summary.getSuccessCount()),         // $3
            std::to_string(summary.getFailedCount()),          // $4
            std::to_string(summary.getCscaAdded()),            // $5
            std::to_string(summary.getDscAdded()),             // $6
            std::to_string(summary.getDscNcAdded()),           // $7
            std::to_string(summary.getCrlAdded()),             // $8
            std::to_string(summary.getTotalAdded()),           // $9
            std::to_string(summary.getCscaDeleted()),          // $10
            std::to_string(summary.getDscDeleted()),           // $11
            std::to_string(summary.getDscNcDeleted()),         // $12
            std::to_string(summary.getCrlDeleted()),           // $13
            summary.getId()                                     // $14
        };

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::debug("[ReconciliationRepository] Updated reconciliation summary ID: {}", summary.getId());
            return true;
        }

        spdlog::warn("[ReconciliationRepository] Summary not found for update: {}", summary.getId());
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in updateSummary(): {}", e.what());
        return false;
    }
}

std::optional<domain::ReconciliationSummary> ReconciliationRepository::findSummaryById(const std::string& id) {
    try {
        const char* query =
            "SELECT id, triggered_by, triggered_at, completed_at, status, dry_run, "
            "success_count, failed_count, "
            "csca_added, dsc_added, dsc_nc_added, crl_added, total_added, "
            "csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted, "
            "duration_ms, error_message, sync_status_id "
            "FROM reconciliation_summary "
            "WHERE id = $1";

        std::vector<std::string> params = {id};

        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            return std::nullopt;
        }

        return jsonToSummary(result[0]);

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in findSummaryById(): {}", e.what());
        return std::nullopt;
    }
}

std::vector<domain::ReconciliationSummary> ReconciliationRepository::findAllSummaries(int limit, int offset) {
    std::vector<domain::ReconciliationSummary> results;

    try {
        const char* query =
            "SELECT id, triggered_by, triggered_at, completed_at, status, dry_run, "
            "success_count, failed_count, "
            "csca_added, dsc_added, dsc_nc_added, crl_added, total_added, "
            "csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted, "
            "duration_ms, error_message, sync_status_id "
            "FROM reconciliation_summary "
            "ORDER BY triggered_at DESC "
            "LIMIT $1 OFFSET $2";

        std::vector<std::string> params = {
            std::to_string(limit),
            std::to_string(offset)
        };

        Json::Value result = queryExecutor_->executeQuery(query, params);

        for (const auto& row : result) {
            results.push_back(jsonToSummary(row));
        }

        spdlog::debug("[ReconciliationRepository] Found {} summaries", results.size());
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in findAllSummaries(): {}", e.what());
        return results;
    }
}

int ReconciliationRepository::countSummaries() {
    try {
        const char* query = "SELECT COUNT(*) FROM reconciliation_summary";

        Json::Value result = queryExecutor_->executeScalar(query);
        return result.asInt();

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
        const char* query =
            "INSERT INTO reconciliation_log ("
            "reconciliation_id, created_at, cert_fingerprint, cert_type, country_code, "
            "action, result, error_message"
            ") VALUES ("
            "$1, NOW(), $2, $3, $4, "
            "$5, $6, $7"
            ") RETURNING id, created_at";

        std::vector<std::string> params = {
            log.getReconciliationId(),                      // $1
            log.getCertFingerprint(),                       // $2
            log.getCertType(),                              // $3
            log.getCountryCode(),                           // $4
            log.getAction(),                                // $5
            log.getResult(),                                // $6
            log.getErrorMessage().value_or("")              // $7 (empty string for NULL)
        };

        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::error("[ReconciliationRepository] Insert returned no rows");
            return false;
        }

        // Update domain object with generated id
        std::string id = result[0]["id"].asString();
        log.setId(id);

        spdlog::debug("[ReconciliationRepository] Reconciliation log created with ID: {}", id);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in createLog(): {}", e.what());
        return false;
    }
}

std::vector<domain::ReconciliationLog> ReconciliationRepository::findLogsByReconciliationId(
    const std::string& reconciliationId,
    int limit,
    int offset
) {
    std::vector<domain::ReconciliationLog> results;

    try {
        const char* query =
            "SELECT id, reconciliation_id, created_at, cert_fingerprint, cert_type, "
            "country_code, action, result, error_message "
            "FROM reconciliation_log "
            "WHERE reconciliation_id = $1 "
            "ORDER BY created_at ASC "
            "LIMIT $2 OFFSET $3";

        std::vector<std::string> params = {
            reconciliationId,
            std::to_string(limit),
            std::to_string(offset)
        };

        Json::Value result = queryExecutor_->executeQuery(query, params);

        for (const auto& row : result) {
            results.push_back(jsonToLog(row));
        }

        spdlog::debug("[ReconciliationRepository] Found {} logs for reconciliation ID: {}",
            results.size(), reconciliationId);
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in findLogsByReconciliationId(): {}", e.what());
        return results;
    }
}

int ReconciliationRepository::countLogsByReconciliationId(const std::string& reconciliationId) {
    try {
        const char* query = "SELECT COUNT(*) FROM reconciliation_log WHERE reconciliation_id = $1";

        std::vector<std::string> params = {reconciliationId};

        Json::Value result = queryExecutor_->executeScalar(query, params);
        return result.asInt();

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in countLogsByReconciliationId(): {}", e.what());
        return 0;
    }
}

// ========================================================================
// Helper Methods
// ========================================================================

domain::ReconciliationSummary ReconciliationRepository::jsonToSummary(const Json::Value& row) {
    // Parse fields
    std::string id = row["id"].asString();
    std::string triggeredBy = row["triggered_by"].asString();
    std::string status = row["status"].asString();

    // Parse boolean field
    bool dryRun = false;
    if (row["dry_run"].isBool()) {
        dryRun = row["dry_run"].asBool();
    } else if (row["dry_run"].isString()) {
        std::string val = row["dry_run"].asString();
        dryRun = (val == "t" || val == "true" || val == "1");
    }

    // Parse timestamps
    std::string triggeredAtStr = row["triggered_at"].asString();
    std::tm tm = {};
    auto triggeredAt = std::chrono::system_clock::now();
    if (strptime(triggeredAtStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        triggeredAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    std::optional<std::chrono::system_clock::time_point> completedAt = std::nullopt;
    if (!row["completed_at"].isNull() && row["completed_at"].isString()) {
        std::string completedAtStr = row["completed_at"].asString();
        tm = {};
        if (strptime(completedAtStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
            completedAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
    }

    // Parse count fields
    int successCount = row["success_count"].asInt();
    int failedCount = row["failed_count"].asInt();
    int cscaAdded = row["csca_added"].asInt();
    int dscAdded = row["dsc_added"].asInt();
    int dscNcAdded = row["dsc_nc_added"].asInt();
    int crlAdded = row["crl_added"].asInt();
    int totalAdded = row["total_added"].asInt();
    int cscaDeleted = row["csca_deleted"].asInt();
    int dscDeleted = row["dsc_deleted"].asInt();
    int dscNcDeleted = row["dsc_nc_deleted"].asInt();
    int crlDeleted = row["crl_deleted"].asInt();
    int durationMs = row["duration_ms"].asInt();

    // Parse optional error_message
    std::optional<std::string> errorMessage = std::nullopt;
    if (!row["error_message"].isNull() && row["error_message"].isString()) {
        std::string msg = row["error_message"].asString();
        if (!msg.empty()) {
            errorMessage = msg;
        }
    }

    // Parse optional sync_status_id
    std::optional<int> syncStatusId = std::nullopt;
    if (!row["sync_status_id"].isNull()) {
        syncStatusId = row["sync_status_id"].asInt();
    }

    // Construct and return domain object
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

domain::ReconciliationLog ReconciliationRepository::jsonToLog(const Json::Value& row) {
    // Parse fields
    std::string id = row["id"].asString();
    std::string reconciliationId = row["reconciliation_id"].asString();

    // Parse timestamp
    std::string createdAtStr = row["created_at"].asString();
    std::tm tm = {};
    auto createdAt = std::chrono::system_clock::now();
    if (strptime(createdAtStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        createdAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    // Parse string fields
    std::string certFingerprint = row["cert_fingerprint"].asString();
    std::string certType = row["cert_type"].asString();
    std::string countryCode = row["country_code"].asString();
    std::string action = row["action"].asString();
    std::string result = row["result"].asString();

    // Parse optional error_message
    std::optional<std::string> errorMessage = std::nullopt;
    if (!row["error_message"].isNull() && row["error_message"].isString()) {
        std::string msg = row["error_message"].asString();
        if (!msg.empty()) {
            errorMessage = msg;
        }
    }

    // Construct and return domain object
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

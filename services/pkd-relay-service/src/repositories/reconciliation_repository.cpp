/**
 * @file reconciliation_repository.cpp
 * @brief Reconciliation repository implementation
 */
#include "reconciliation_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace icao::relay::repositories {

/**
 * @brief Parse JSON value to integer with type-safe conversion
 *
 * Oracle returns all values as strings, so .asInt() fails.
 * This handles int, uint, string, double types gracefully.
 */
static int getInt(const Json::Value& json, const std::string& field, int defaultValue = 0) {
    if (!json.isMember(field) || json[field].isNull()) return defaultValue;
    const auto& v = json[field];
    if (v.isInt()) return v.asInt();
    if (v.isUInt()) return static_cast<int>(v.asUInt());
    if (v.isString()) {
        try { return std::stoi(v.asString()); }
        catch (...) { return defaultValue; }
    }
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return defaultValue;
}

ReconciliationRepository::ReconciliationRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("ReconciliationRepository: queryExecutor cannot be nullptr");
    }

    spdlog::debug("[ReconciliationRepository] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

// --- ReconciliationSummary Operations ---

bool ReconciliationRepository::createSummary(domain::ReconciliationSummary& summary) {
    try {
        // Step 1: Generate integer ID using database-specific sequence
        // PostgreSQL: nextval('reconciliation_summary_id_seq')
        // Oracle: seq_reconciliation_summary.NEXTVAL
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string idQuery;

        if (dbType == "postgres") {
            idQuery = "SELECT nextval('reconciliation_summary_id_seq') as id";
        } else {
            // Oracle: Use sequence
            idQuery = "SELECT SEQ_RECON_SUMMARY.NEXTVAL as id FROM DUAL";
        }

        Json::Value idResult = queryExecutor_->executeQuery(idQuery, {});
        if (idResult.empty()) {
            spdlog::error("[ReconciliationRepository] Failed to generate ID");
            return false;
        }
        // OracleQueryExecutor converts column names to lowercase
        std::string generatedId = std::to_string(getInt(idResult[0], "id", 0));

        // Step 2: Insert with generated ID and current timestamp (no RETURNING clause)
        const char* query =
            "INSERT INTO reconciliation_summary ("
            "id, triggered_by, started_at, status, dry_run, "
            "success_count, failed_count, "
            "csca_added, dsc_added, dsc_nc_added, crl_added, total_added, "
            "csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted"
            ") VALUES ("
            "$1, $2, NOW(), $3, $4, "
            "$5, $6, "
            "$7, $8, $9, $10, $11, "
            "$12, $13, $14, $15"
            ")";

        // Oracle NUMBER(1) needs "1"/"0", PostgreSQL BOOLEAN needs "true"/"false"
        // Reuse dbType from line 44 (already declared in this scope)
        auto boolStr = [&dbType](bool val) -> std::string {
            return (dbType == "oracle") ? (val ? "1" : "0") : (val ? "true" : "false");
        };

        std::vector<std::string> params = {
            generatedId,                                        // $1: id
            summary.getTriggeredBy(),                           // $2
            summary.getStatus(),                                 // $3
            boolStr(summary.isDryRun()),                        // $4
            std::to_string(summary.getSuccessCount()),          // $5
            std::to_string(summary.getFailedCount()),           // $6
            std::to_string(summary.getCscaAdded()),             // $7
            std::to_string(summary.getDscAdded()),              // $8
            std::to_string(summary.getDscNcAdded()),            // $9
            std::to_string(summary.getCrlAdded()),              // $10
            std::to_string(summary.getTotalAdded()),            // $11
            std::to_string(summary.getCscaDeleted()),           // $12
            std::to_string(summary.getDscDeleted()),            // $13
            std::to_string(summary.getDscNcDeleted()),          // $14
            std::to_string(summary.getCrlDeleted())             // $15
        };

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        // Oracle's OTL get_rpc() may return 0 even for successful INSERTs without RETURNING clause
        // If execution reaches here without exception, INSERT was successful
        std::string dbTypeCheck = queryExecutor_->getDatabaseType();
        if (rowsAffected == 0 && dbTypeCheck == "postgres") {
            // PostgreSQL should always return affected rows count
            spdlog::error("[ReconciliationRepository] Insert failed: no rows affected");
            return false;
        }

        // Update domain object with generated id
        summary.setId(generatedId);

        spdlog::info("[ReconciliationRepository] Reconciliation summary created with ID: {}", generatedId);
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
            "SELECT id, triggered_by, started_at, completed_at, status, dry_run, "
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
            "SELECT id, triggered_by, started_at, completed_at, status, dry_run, "
            "success_count, failed_count, "
            "csca_added, dsc_added, dsc_nc_added, crl_added, total_added, "
            "csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted, "
            "duration_ms, error_message, sync_status_id "
            "FROM reconciliation_summary "
            "ORDER BY started_at DESC "
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
        if (result.isInt()) return result.asInt();
        if (result.isString()) {
            try { return std::stoi(result.asString()); }
            catch (...) { return 0; }
        }
        return result.asInt();

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in countSummaries(): {}", e.what());
        return 0;
    }
}

// --- ReconciliationLog Operations ---

bool ReconciliationRepository::createLog(domain::ReconciliationLog& log) {
    try {
        // Step 1: Generate integer ID using database-specific sequence
        // PostgreSQL: nextval('reconciliation_log_id_seq')
        // Oracle: seq_reconciliation_log.NEXTVAL
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string idQuery;

        if (dbType == "postgres") {
            idQuery = "SELECT nextval('reconciliation_log_id_seq') as id";
        } else {
            // Oracle: Use sequence
            idQuery = "SELECT SEQ_RECON_LOG.NEXTVAL as id FROM DUAL";
        }

        Json::Value idResult = queryExecutor_->executeQuery(idQuery, {});
        if (idResult.empty()) {
            spdlog::error("[ReconciliationRepository] Failed to generate ID");
            return false;
        }
        // OracleQueryExecutor converts column names to lowercase
        std::string generatedId = std::to_string(getInt(idResult[0], "id", 0));

        // Step 2: Insert with generated ID and current timestamp (no RETURNING clause)
        const char* query =
            "INSERT INTO reconciliation_log ("
            "id, summary_id, started_at, fingerprint_sha256, certificate_type, country_code, "
            "operation, status, error_message"
            ") VALUES ("
            "$1, $2, NOW(), $3, $4, $5, "
            "$6, $7, $8"
            ")";

        std::vector<std::string> params = {
            generatedId,                                    // $1: id
            log.getReconciliationId(),                      // $2
            log.getCertFingerprint(),                       // $3
            log.getCertType(),                              // $4
            log.getCountryCode(),                           // $5
            log.getAction(),                                // $6
            log.getResult(),                                // $7
            log.getErrorMessage().value_or("")              // $8 (empty string for NULL)
        };

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        // Oracle's OTL get_rpc() may return 0 even for successful INSERTs without RETURNING clause
        // If execution reaches here without exception, INSERT was successful
        std::string dbTypeCheck = queryExecutor_->getDatabaseType();
        if (rowsAffected == 0 && dbTypeCheck == "postgres") {
            // PostgreSQL should always return affected rows count
            spdlog::error("[ReconciliationRepository] Insert failed: no rows affected");
            return false;
        }

        // Update domain object with generated id
        log.setId(generatedId);

        spdlog::debug("[ReconciliationRepository] Reconciliation log created with ID: {}", generatedId);
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
            "SELECT id, summary_id, started_at, fingerprint_sha256, certificate_type, "
            "country_code, operation, status, error_message "
            "FROM reconciliation_log "
            "WHERE summary_id = $1 "
            "ORDER BY started_at ASC "
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
        const char* query = "SELECT COUNT(*) FROM reconciliation_log WHERE summary_id = $1";

        std::vector<std::string> params = {reconciliationId};

        Json::Value result = queryExecutor_->executeScalar(query, params);
        if (result.isInt()) return result.asInt();
        if (result.isString()) {
            try { return std::stoi(result.asString()); }
            catch (...) { return 0; }
        }
        return result.asInt();

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationRepository] Exception in countLogsByReconciliationId(): {}", e.what());
        return 0;
    }
}

// --- Helper Methods ---

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
    std::string triggeredAtStr = row["started_at"].asString();
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

    // Parse count fields - use getInt() for Oracle string compatibility
    int successCount = getInt(row, "success_count");
    int failedCount = getInt(row, "failed_count");
    int cscaAdded = getInt(row, "csca_added");
    int dscAdded = getInt(row, "dsc_added");
    int dscNcAdded = getInt(row, "dsc_nc_added");
    int crlAdded = getInt(row, "crl_added");
    int totalAdded = getInt(row, "total_added");
    int cscaDeleted = getInt(row, "csca_deleted");
    int dscDeleted = getInt(row, "dsc_deleted");
    int dscNcDeleted = getInt(row, "dsc_nc_deleted");
    int crlDeleted = getInt(row, "crl_deleted");
    int durationMs = getInt(row, "duration_ms");

    // Parse optional error_message
    std::optional<std::string> errorMessage = std::nullopt;
    if (!row["error_message"].isNull() && row["error_message"].isString()) {
        std::string msg = row["error_message"].asString();
        if (!msg.empty()) {
            errorMessage = msg;
        }
    }

    // Parse optional sync_status_id - use getInt() for Oracle string compatibility
    std::optional<int> syncStatusId = std::nullopt;
    if (!row["sync_status_id"].isNull()) {
        syncStatusId = getInt(row, "sync_status_id");
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
    std::string reconciliationId = row["summary_id"].asString();

    // Parse timestamp
    std::string createdAtStr = row["started_at"].asString();
    std::tm tm = {};
    auto createdAt = std::chrono::system_clock::now();
    if (strptime(createdAtStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        createdAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    // Parse string fields
    std::string certFingerprint = row["fingerprint_sha256"].asString();
    std::string certType = row["certificate_type"].asString();
    std::string countryCode = row["country_code"].asString();
    std::string action = row["operation"].asString();
    std::string result = row["status"].asString();

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

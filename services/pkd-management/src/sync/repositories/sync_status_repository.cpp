/**
 * @file sync_status_repository.cpp
 * @brief Sync status repository implementation
 */
#include "sync_status_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <sstream>

using common::db::getInt;

namespace icao::relay::repositories {

SyncStatusRepository::SyncStatusRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("SyncStatusRepository: queryExecutor cannot be nullptr");
    }

    spdlog::debug("[SyncStatusRepository] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

bool SyncStatusRepository::create(domain::SyncStatus& syncStatus) {
    try {
        // Step 1: Generate ID using database-specific method
        // PostgreSQL: UUID (gen_random_uuid())
        // Oracle: NUMBER (seq_sync_status.NEXTVAL)
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string idQuery;
        std::string generatedId;

        if (dbType == "postgres") {
            // PostgreSQL uses INTEGER with sequence
            idQuery = "SELECT nextval('sync_status_id_seq')::text as id";
            Json::Value result = queryExecutor_->executeQuery(idQuery, {});
            if (result.empty()) {
                spdlog::error("[SyncStatusRepository] Failed to generate UUID");
                return false;
            }
            generatedId = result[0]["id"].asString();
        } else {
            // Oracle uses NUMBER with sequence
            // NOTE: Oracle returns column names in UPPERCASE
            idQuery = "SELECT seq_sync_status.NEXTVAL as id FROM DUAL";
            Json::Value result = queryExecutor_->executeQuery(idQuery, {});
            if (result.empty()) {
                spdlog::error("[SyncStatusRepository] Failed to generate ID");
                return false;
            }
            // OracleQueryExecutor converts column names to lowercase
            generatedId = std::to_string(getInt(result[0], "id", 0));
        }

        // Step 2: Insert with generated ID and current timestamp (no RETURNING clause)
        std::string tsFunc = common::db::currentTimestamp(dbType);
        std::string jsonCast = (dbType == "oracle") ? "" : "::jsonb";

        std::string query =
            "INSERT INTO sync_status ("
            "id, checked_at, "
            "db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms, "
            "db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy"
            ") VALUES ("
            "$1, " + tsFunc + ", "
            "$2, $3, $4, $5, $6, "
            "$7, $8, $9, $10, $11, "
            "$12, $13, $14, $15, $16, "
            "$17" + jsonCast + ", $18" + jsonCast + ", $19, $20, $21, "
            "$22, $23, $24"
            ")";

        // Serialize JSONB fields
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";

        auto dbCountryStats = syncStatus.getDbCountryStats();
        std::string dbCountryStatsJson = dbCountryStats.has_value()
            ? Json::writeString(builder, dbCountryStats.value())
            : "{}";

        auto ldapCountryStats = syncStatus.getLdapCountryStats();
        std::string ldapCountryStatsJson = ldapCountryStats.has_value()
            ? Json::writeString(builder, ldapCountryStats.value())
            : "{}";

        // Get status and other fields
        auto errorMessage = syncStatus.getErrorMessage();
        std::string errorMessageStr = errorMessage.has_value() ? errorMessage.value() : "";

        // Build parameter vector
        std::vector<std::string> params = {
            generatedId,                                         // $1: id
            std::to_string(syncStatus.getDbCscaCount()),        // $2
            std::to_string(syncStatus.getDbDscCount()),         // $3
            std::to_string(syncStatus.getDbDscNcCount()),       // $4
            std::to_string(syncStatus.getDbCrlCount()),         // $5
            std::to_string(syncStatus.getDbStoredInLdapCount()),// $6
            std::to_string(syncStatus.getLdapCscaCount()),      // $7
            std::to_string(syncStatus.getLdapDscCount()),       // $8
            std::to_string(syncStatus.getLdapDscNcCount()),     // $9
            std::to_string(syncStatus.getLdapCrlCount()),       // $10
            std::to_string(syncStatus.getLdapTotalEntries()),   // $11
            std::to_string(syncStatus.getCscaDiscrepancy()),    // $12
            std::to_string(syncStatus.getDscDiscrepancy()),     // $13
            std::to_string(syncStatus.getDscNcDiscrepancy()),   // $14
            std::to_string(syncStatus.getCrlDiscrepancy()),     // $15
            std::to_string(syncStatus.getTotalDiscrepancy()),   // $16
            dbCountryStatsJson,                                  // $17
            ldapCountryStatsJson,                                // $18
            syncStatus.getStatus(),                              // $19
            errorMessageStr,                                     // $20
            std::to_string(syncStatus.getCheckDurationMs()),    // $21
            std::to_string(syncStatus.getDbMlscCount()),        // $22
            std::to_string(syncStatus.getLdapMlscCount()),      // $23
            std::to_string(syncStatus.getMlscDiscrepancy())     // $24
        };

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        // Oracle's OTL get_rpc() may return 0 even for successful INSERTs without RETURNING clause
        // If execution reaches here without exception, INSERT was successful
        // Reuse dbType from line 25 (already declared in this scope)
        if (rowsAffected == 0 && dbType == "postgres") {
            // PostgreSQL should always return affected rows count
            spdlog::error("[SyncStatusRepository] Insert failed: no rows affected");
            return false;
        }

        // Update domain object with generated id and current timestamp
        syncStatus.setId(generatedId);
        syncStatus.setCheckedAt(std::chrono::system_clock::now());

        spdlog::info("[SyncStatusRepository] Sync status created with ID: {}", generatedId);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in create(): {}", e.what());
        return false;
    }
}

std::optional<domain::SyncStatus> SyncStatusRepository::findLatest() {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query =
            "SELECT id, checked_at, "
            "db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms, "
            "db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy "
            "FROM sync_status "
            "ORDER BY checked_at DESC " +
            common::db::limitClause(dbType, 1);

        Json::Value result = queryExecutor_->executeQuery(query);

        if (result.empty()) {
            spdlog::debug("[SyncStatusRepository] No sync status records found");
            return std::nullopt;
        }

        return jsonToSyncStatus(result[0]);

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in findLatest(): {}", e.what());
        return std::nullopt;
    }
}

std::vector<domain::SyncStatus> SyncStatusRepository::findAll(int limit, int offset) {
    std::vector<domain::SyncStatus> results;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query =
            "SELECT id, checked_at, "
            "db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms, "
            "db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy "
            "FROM sync_status "
            "ORDER BY checked_at DESC " +
            common::db::paginationClause(dbType, limit, offset);

        Json::Value result = queryExecutor_->executeQuery(query);

        for (const auto& row : result) {
            results.push_back(jsonToSyncStatus(row));
        }

        spdlog::debug("[SyncStatusRepository] Found {} sync status records", results.size());
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in findAll(): {}", e.what());
        return results;
    }
}

int SyncStatusRepository::count() {
    try {
        const char* query = "SELECT COUNT(*) FROM sync_status";

        Json::Value result = queryExecutor_->executeScalar(query);
        if (result.isInt()) return result.asInt();
        if (result.isString()) {
            try { return std::stoi(result.asString()); }
            catch (...) { return 0; }
        }
        return result.asInt();

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in count(): {}", e.what());
        return 0;
    }
}

domain::SyncStatus SyncStatusRepository::jsonToSyncStatus(const Json::Value& row) {
    // Parse id
    std::string id = row["id"].asString();

    // Parse timestamp
    std::string checkedAtStr = row["checked_at"].asString();
    std::tm tm = {};
    auto checkedAt = std::chrono::system_clock::now();
    if (strptime(checkedAtStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        checkedAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    // Parse integer counts - use getInt() for Oracle string compatibility
    int dbCscaCount = getInt(row, "db_csca_count");
    int dbDscCount = getInt(row, "db_dsc_count");
    int dbDscNcCount = getInt(row, "db_dsc_nc_count");
    int dbCrlCount = getInt(row, "db_crl_count");
    int dbStoredInLdapCount = getInt(row, "db_stored_in_ldap_count");

    int ldapCscaCount = getInt(row, "ldap_csca_count");
    int ldapDscCount = getInt(row, "ldap_dsc_count");
    int ldapDscNcCount = getInt(row, "ldap_dsc_nc_count");
    int ldapCrlCount = getInt(row, "ldap_crl_count");
    int ldapTotalEntries = getInt(row, "ldap_total_entries");

    // Parse discrepancies
    int cscaDiscrepancy = getInt(row, "csca_discrepancy");
    int dscDiscrepancy = getInt(row, "dsc_discrepancy");
    int dscNcDiscrepancy = getInt(row, "dsc_nc_discrepancy");
    int crlDiscrepancy = getInt(row, "crl_discrepancy");
    int totalDiscrepancy = getInt(row, "total_discrepancy");

    // Parse MLSC counts
    int dbMlscCount = getInt(row, "db_mlsc_count");
    int ldapMlscCount = getInt(row, "ldap_mlsc_count");
    int mlscDiscrepancy = getInt(row, "mlsc_discrepancy");

    // Parse JSONB fields (already parsed by Query Executor)
    std::optional<Json::Value> dbCountryStats;
    if (!row["db_country_stats"].isNull() && row["db_country_stats"].isObject()) {
        dbCountryStats = row["db_country_stats"];
    }

    std::optional<Json::Value> ldapCountryStats;
    if (!row["ldap_country_stats"].isNull() && row["ldap_country_stats"].isObject()) {
        ldapCountryStats = row["ldap_country_stats"];
    }

    // Parse status string
    std::string status = row["status"].asString();

    // Parse optional error_message
    std::optional<std::string> errorMessage;
    if (!row["error_message"].isNull() && !row["error_message"].asString().empty()) {
        errorMessage = row["error_message"].asString();
    }

    // Parse check_duration_ms
    int checkDurationMs = getInt(row, "check_duration_ms");

    // Construct and return domain object with correct parameter order
    return domain::SyncStatus(
        id, checkedAt,
        dbCscaCount, ldapCscaCount, cscaDiscrepancy,
        dbMlscCount, ldapMlscCount, mlscDiscrepancy,
        dbDscCount, ldapDscCount, dscDiscrepancy,
        dbDscNcCount, ldapDscNcCount, dscNcDiscrepancy,
        dbCrlCount, ldapCrlCount, crlDiscrepancy,
        totalDiscrepancy,
        dbStoredInLdapCount, ldapTotalEntries,
        dbCountryStats, ldapCountryStats,
        status, errorMessage, checkDurationMs
    );
}

} // namespace icao::relay::repositories

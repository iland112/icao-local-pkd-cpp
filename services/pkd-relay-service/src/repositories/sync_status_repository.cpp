#include "sync_status_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <sstream>

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
        const char* query =
            "INSERT INTO sync_status ("
            "checked_at, "
            "db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms, "
            "db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy"
            ") VALUES ("
            "NOW(), "
            "$1, $2, $3, $4, $5, "
            "$6, $7, $8, $9, $10, "
            "$11, $12, $13, $14, $15, "
            "$16::jsonb, $17::jsonb, $18, $19, $20, "
            "$21, $22, $23"
            ") RETURNING id, checked_at";

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
            std::to_string(syncStatus.getDbCscaCount()),        // $1
            std::to_string(syncStatus.getDbDscCount()),         // $2
            std::to_string(syncStatus.getDbDscNcCount()),       // $3
            std::to_string(syncStatus.getDbCrlCount()),         // $4
            std::to_string(syncStatus.getDbStoredInLdapCount()),// $5
            std::to_string(syncStatus.getLdapCscaCount()),      // $6
            std::to_string(syncStatus.getLdapDscCount()),       // $7
            std::to_string(syncStatus.getLdapDscNcCount()),     // $8
            std::to_string(syncStatus.getLdapCrlCount()),       // $9
            std::to_string(syncStatus.getLdapTotalEntries()),   // $10
            std::to_string(syncStatus.getCscaDiscrepancy()),    // $11
            std::to_string(syncStatus.getDscDiscrepancy()),     // $12
            std::to_string(syncStatus.getDscNcDiscrepancy()),   // $13
            std::to_string(syncStatus.getCrlDiscrepancy()),     // $14
            std::to_string(syncStatus.getTotalDiscrepancy()),   // $15
            dbCountryStatsJson,                                  // $16
            ldapCountryStatsJson,                                // $17
            syncStatus.getStatus(),                              // $18
            errorMessageStr,                                     // $19
            std::to_string(syncStatus.getCheckDurationMs()),    // $20
            std::to_string(syncStatus.getDbMlscCount()),        // $21
            std::to_string(syncStatus.getLdapMlscCount()),      // $22
            std::to_string(syncStatus.getMlscDiscrepancy())     // $23
        };

        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::error("[SyncStatusRepository] Insert returned no rows");
            return false;
        }

        // Update domain object with generated id and timestamp
        std::string id = result[0]["id"].asString();
        syncStatus.setId(id);

        // Parse checked_at timestamp
        std::string checkedAtStr = result[0]["checked_at"].asString();
        std::tm tm = {};
        if (strptime(checkedAtStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
            auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            syncStatus.setCheckedAt(tp);
        }

        spdlog::info("[SyncStatusRepository] Sync status created with ID: {}", id);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in create(): {}", e.what());
        return false;
    }
}

std::optional<domain::SyncStatus> SyncStatusRepository::findLatest() {
    try {
        const char* query =
            "SELECT id, checked_at, "
            "db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms, "
            "db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy "
            "FROM sync_status "
            "ORDER BY checked_at DESC "
            "LIMIT 1";

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
        const char* query =
            "SELECT id, checked_at, "
            "db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms, "
            "db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy "
            "FROM sync_status "
            "ORDER BY checked_at DESC "
            "LIMIT $1 OFFSET $2";

        std::vector<std::string> params = {
            std::to_string(limit),   // $1
            std::to_string(offset)   // $2
        };

        Json::Value result = queryExecutor_->executeQuery(query, params);

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

    // Parse integer counts (DB columns first, then LDAP)
    int dbCscaCount = row["db_csca_count"].asInt();
    int dbDscCount = row["db_dsc_count"].asInt();
    int dbDscNcCount = row["db_dsc_nc_count"].asInt();
    int dbCrlCount = row["db_crl_count"].asInt();
    int dbStoredInLdapCount = row["db_stored_in_ldap_count"].asInt();

    int ldapCscaCount = row["ldap_csca_count"].asInt();
    int ldapDscCount = row["ldap_dsc_count"].asInt();
    int ldapDscNcCount = row["ldap_dsc_nc_count"].asInt();
    int ldapCrlCount = row["ldap_crl_count"].asInt();
    int ldapTotalEntries = row["ldap_total_entries"].asInt();

    // Parse discrepancies
    int cscaDiscrepancy = row["csca_discrepancy"].asInt();
    int dscDiscrepancy = row["dsc_discrepancy"].asInt();
    int dscNcDiscrepancy = row["dsc_nc_discrepancy"].asInt();
    int crlDiscrepancy = row["crl_discrepancy"].asInt();
    int totalDiscrepancy = row["total_discrepancy"].asInt();

    // Parse MLSC counts
    int dbMlscCount = row["db_mlsc_count"].asInt();
    int ldapMlscCount = row["ldap_mlsc_count"].asInt();
    int mlscDiscrepancy = row["mlsc_discrepancy"].asInt();

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
    int checkDurationMs = row["check_duration_ms"].asInt();

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

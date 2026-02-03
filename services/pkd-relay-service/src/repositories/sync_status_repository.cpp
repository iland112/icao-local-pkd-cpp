#include "sync_status_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>

namespace icao::relay::repositories {

SyncStatusRepository::SyncStatusRepository(const std::string& conninfo)
    : conninfo_(conninfo) {
    conn_ = PQconnectdb(conninfo_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string error = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("Database connection failed: " + error);
    }
}

SyncStatusRepository::~SyncStatusRepository() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

PGconn* SyncStatusRepository::getConnection() {
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

bool SyncStatusRepository::create(domain::SyncStatus& syncStatus) {
    try {
        PGconn* conn = getConnection();

        const char* query =
            "INSERT INTO sync_status ("
            "checked_at, "
            "db_csca_count, db_mlsc_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_mlsc_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, "
            "csca_discrepancy, mlsc_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "sync_required, country_stats"
            ") VALUES ("
            "NOW(), "
            "$1, $2, $3, $4, $5, $6, "
            "$7, $8, $9, $10, $11, "
            "$12, $13, $14, $15, $16, $17, "
            "$18, $19::jsonb"
            ") RETURNING id, checked_at";

        // Calculate sync_required based on total discrepancy
        const char* syncRequired = (syncStatus.getTotalDiscrepancy() > 0) ? "true" : "false";

        // Convert integers to strings
        std::string dbCscaCount = std::to_string(syncStatus.getDbCscaCount());
        std::string dbMlscCount = std::to_string(syncStatus.getDbMlscCount());
        std::string dbDscCount = std::to_string(syncStatus.getDbDscCount());
        std::string dbDscNcCount = std::to_string(syncStatus.getDbDscNcCount());
        std::string dbCrlCount = std::to_string(syncStatus.getDbCrlCount());
        std::string dbStoredInLdapCount = std::to_string(syncStatus.getDbStoredInLdapCount());

        std::string ldapCscaCount = std::to_string(syncStatus.getLdapCscaCount());
        std::string ldapMlscCount = std::to_string(syncStatus.getLdapMlscCount());
        std::string ldapDscCount = std::to_string(syncStatus.getLdapDscCount());
        std::string ldapDscNcCount = std::to_string(syncStatus.getLdapDscNcCount());
        std::string ldapCrlCount = std::to_string(syncStatus.getLdapCrlCount());

        std::string cscaDiscrepancy = std::to_string(syncStatus.getCscaDiscrepancy());
        std::string mlscDiscrepancy = std::to_string(syncStatus.getMlscDiscrepancy());
        std::string dscDiscrepancy = std::to_string(syncStatus.getDscDiscrepancy());
        std::string dscNcDiscrepancy = std::to_string(syncStatus.getDscNcDiscrepancy());
        std::string crlDiscrepancy = std::to_string(syncStatus.getCrlDiscrepancy());
        std::string totalDiscrepancy = std::to_string(syncStatus.getTotalDiscrepancy());

        // Serialize country_stats to JSON string
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        auto dbCountryStats = syncStatus.getDbCountryStats();
        std::string countryStatsJson = dbCountryStats.has_value()
            ? Json::writeString(builder, dbCountryStats.value())
            : "{}";

        const char* paramValues[19] = {
            dbCscaCount.c_str(), dbMlscCount.c_str(), dbDscCount.c_str(),
            dbDscNcCount.c_str(), dbCrlCount.c_str(), dbStoredInLdapCount.c_str(),
            ldapCscaCount.c_str(), ldapMlscCount.c_str(), ldapDscCount.c_str(),
            ldapDscNcCount.c_str(), ldapCrlCount.c_str(),
            cscaDiscrepancy.c_str(), mlscDiscrepancy.c_str(), dscDiscrepancy.c_str(),
            dscNcDiscrepancy.c_str(), crlDiscrepancy.c_str(), totalDiscrepancy.c_str(),
            syncRequired,
            countryStatsJson.c_str()
        };

        PGresult* res = PQexecParams(
            conn, query, 19, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[SyncStatusRepository] Failed to create sync_status: {}", error);
            return false;
        }

        // Update domain object with generated id and timestamp
        if (PQntuples(res) > 0) {
            int id = std::atoi(PQgetvalue(res, 0, 0));
            syncStatus.setId(id);

            // Parse checked_at timestamp
            const char* checkedAtStr = PQgetvalue(res, 0, 1);
            // Simple timestamp parsing (PostgreSQL format: YYYY-MM-DD HH:MM:SS)
            std::tm tm = {};
            if (strptime(checkedAtStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
                auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                syncStatus.setCheckedAt(tp);
            }
        }

        PQclear(res);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in create(): {}", e.what());
        return false;
    }
}

std::optional<domain::SyncStatus> SyncStatusRepository::findLatest() {
    try {
        PGconn* conn = getConnection();

        const char* query =
            "SELECT id, checked_at, "
            "db_csca_count, db_mlsc_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_mlsc_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, mlsc_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms "
            "FROM sync_status "
            "ORDER BY checked_at DESC "
            "LIMIT 1";

        PGresult* res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[SyncStatusRepository] Failed to find latest sync_status: {}", error);
            return std::nullopt;
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            return std::nullopt;
        }

        auto syncStatus = resultToSyncStatus(res, 0);
        PQclear(res);
        return syncStatus;

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in findLatest(): {}", e.what());
        return std::nullopt;
    }
}

std::vector<domain::SyncStatus> SyncStatusRepository::findAll(int limit, int offset) {
    std::vector<domain::SyncStatus> results;

    try {
        PGconn* conn = getConnection();

        const char* query =
            "SELECT id, checked_at, "
            "db_csca_count, db_mlsc_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
            "ldap_csca_count, ldap_mlsc_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
            "csca_discrepancy, mlsc_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
            "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms "
            "FROM sync_status "
            "ORDER BY checked_at DESC "
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
            spdlog::error("[SyncStatusRepository] Failed to find all sync_status: {}", error);
            return results;
        }

        int rowCount = PQntuples(res);
        for (int i = 0; i < rowCount; i++) {
            results.push_back(resultToSyncStatus(res, i));
        }

        PQclear(res);
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in findAll(): {}", e.what());
        return results;
    }
}

int SyncStatusRepository::count() {
    try {
        PGconn* conn = getConnection();

        const char* query = "SELECT COUNT(*) FROM sync_status";

        PGresult* res = PQexec(conn, query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            spdlog::error("[SyncStatusRepository] Failed to count sync_status: {}", error);
            return 0;
        }

        int count = 0;
        if (PQntuples(res) > 0) {
            count = std::atoi(PQgetvalue(res, 0, 0));
        }

        PQclear(res);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[SyncStatusRepository] Exception in count(): {}", e.what());
        return 0;
    }
}

domain::SyncStatus SyncStatusRepository::resultToSyncStatus(PGresult* res, int row) {
    // Parse all fields from result set
    int id = std::atoi(PQgetvalue(res, row, 0));

    // Parse timestamp
    const char* checkedAtStr = PQgetvalue(res, row, 1);
    std::tm tm = {};
    auto checkedAt = std::chrono::system_clock::now();
    if (strptime(checkedAtStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        checkedAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    // Parse integer counts (interleaved: db/ldap pairs)
    int dbCscaCount = std::atoi(PQgetvalue(res, row, 2));
    int dbMlscCount = std::atoi(PQgetvalue(res, row, 3));
    int dbDscCount = std::atoi(PQgetvalue(res, row, 4));
    int dbDscNcCount = std::atoi(PQgetvalue(res, row, 5));
    int dbCrlCount = std::atoi(PQgetvalue(res, row, 6));
    int dbStoredInLdapCount = std::atoi(PQgetvalue(res, row, 7));

    int ldapCscaCount = std::atoi(PQgetvalue(res, row, 8));
    int ldapMlscCount = std::atoi(PQgetvalue(res, row, 9));
    int ldapDscCount = std::atoi(PQgetvalue(res, row, 10));
    int ldapDscNcCount = std::atoi(PQgetvalue(res, row, 11));
    int ldapCrlCount = std::atoi(PQgetvalue(res, row, 12));
    int ldapTotalEntries = std::atoi(PQgetvalue(res, row, 13));

    // Parse discrepancies
    int cscaDiscrepancy = std::atoi(PQgetvalue(res, row, 14));
    int mlscDiscrepancy = std::atoi(PQgetvalue(res, row, 15));
    int dscDiscrepancy = std::atoi(PQgetvalue(res, row, 16));
    int dscNcDiscrepancy = std::atoi(PQgetvalue(res, row, 17));
    int crlDiscrepancy = std::atoi(PQgetvalue(res, row, 18));
    int totalDiscrepancy = std::atoi(PQgetvalue(res, row, 19));

    // Parse JSONB db_country_stats
    std::optional<Json::Value> dbCountryStats;
    const char* dbCountryStatsStr = PQgetvalue(res, row, 20);
    if (dbCountryStatsStr && strlen(dbCountryStatsStr) > 0) {
        Json::Value dbStats;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream iss(dbCountryStatsStr);
        if (Json::parseFromStream(builder, iss, &dbStats, &errors)) {
            dbCountryStats = dbStats;
        } else {
            spdlog::warn("[SyncStatusRepository] Failed to parse db_country_stats JSON: {}", errors);
        }
    }

    // Parse JSONB ldap_country_stats
    std::optional<Json::Value> ldapCountryStats;
    const char* ldapCountryStatsStr = PQgetvalue(res, row, 21);
    if (ldapCountryStatsStr && strlen(ldapCountryStatsStr) > 0) {
        Json::Value ldapStats;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream iss(ldapCountryStatsStr);
        if (Json::parseFromStream(builder, iss, &ldapStats, &errors)) {
            ldapCountryStats = ldapStats;
        } else {
            spdlog::warn("[SyncStatusRepository] Failed to parse ldap_country_stats JSON: {}", errors);
        }
    }

    // Parse status string
    std::string status = PQgetvalue(res, row, 22);

    // Parse optional error_message
    std::optional<std::string> errorMessage;
    const char* errorMessageStr = PQgetvalue(res, row, 23);
    if (errorMessageStr && strlen(errorMessageStr) > 0 && !PQgetisnull(res, row, 23)) {
        errorMessage = std::string(errorMessageStr);
    }

    // Parse check_duration_ms
    int checkDurationMs = std::atoi(PQgetvalue(res, row, 24));

    // Construct and return domain object with correct parameter order:
    // (id, checked_at,
    //  db_csca_count, ldap_csca_count, csca_discrepancy,
    //  db_mlsc_count, ldap_mlsc_count, mlsc_discrepancy,
    //  db_dsc_count, ldap_dsc_count, dsc_discrepancy,
    //  db_dsc_nc_count, ldap_dsc_nc_count, dsc_nc_discrepancy,
    //  db_crl_count, ldap_crl_count, crl_discrepancy,
    //  total_discrepancy,
    //  db_stored_in_ldap_count, ldap_total_entries,
    //  db_country_stats, ldap_country_stats,
    //  status, error_message, check_duration_ms)
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

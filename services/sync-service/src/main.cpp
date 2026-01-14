// =============================================================================
// ICAO Local PKD - Sync Service
// =============================================================================
// Version: 1.3.0
// Description: DB-LDAP synchronization checker, certificate re-validation
// =============================================================================
// Changelog:
//   v1.3.0 (2026-01-13): User-configurable settings UI, dynamic config reload
//   v1.2.0 (2026-01-07): Remove interval sync, keep only daily scheduler
//   v1.1.0 (2026-01-06): Daily scheduler at midnight, certificate re-validation
//   v1.0.0 (2026-01-03): Initial release
// =============================================================================

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <json/json.h>
#include <libpq-fe.h>
#include <ldap.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <algorithm>

using namespace drogon;

// =============================================================================
// Global Configuration
// =============================================================================
struct Config {
    // Server
    int serverPort = 8083;

    // Database
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "pkd";
    std::string dbUser = "pkd";
    std::string dbPassword = "pkd123";

    // LDAP (read)
    std::string ldapHost = "haproxy";
    int ldapPort = 389;

    // LDAP (write - for reconciliation)
    std::string ldapWriteHost = "openldap1";
    int ldapWritePort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword = "admin";
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    // Sync settings
    bool autoReconcile = true;
    int maxReconcileBatchSize = 100;

    // Daily scheduler settings
    bool dailySyncEnabled = true;
    int dailySyncHour = 0;      // 00:00 (midnight)
    int dailySyncMinute = 0;
    bool revalidateCertsOnSync = true;

    void loadFromEnv() {
        if (auto e = std::getenv("SERVER_PORT")) serverPort = std::stoi(e);
        if (auto e = std::getenv("DB_HOST")) dbHost = e;
        if (auto e = std::getenv("DB_PORT")) dbPort = std::stoi(e);
        if (auto e = std::getenv("DB_NAME")) dbName = e;
        if (auto e = std::getenv("DB_USER")) dbUser = e;
        if (auto e = std::getenv("DB_PASSWORD")) dbPassword = e;
        if (auto e = std::getenv("LDAP_HOST")) ldapHost = e;
        if (auto e = std::getenv("LDAP_PORT")) ldapPort = std::stoi(e);
        if (auto e = std::getenv("LDAP_WRITE_HOST")) ldapWriteHost = e;
        if (auto e = std::getenv("LDAP_WRITE_PORT")) ldapWritePort = std::stoi(e);
        if (auto e = std::getenv("LDAP_BIND_DN")) ldapBindDn = e;
        if (auto e = std::getenv("LDAP_BIND_PASSWORD")) ldapBindPassword = e;
        if (auto e = std::getenv("LDAP_BASE_DN")) ldapBaseDn = e;
        if (auto e = std::getenv("AUTO_RECONCILE")) autoReconcile = (std::string(e) == "true");
        if (auto e = std::getenv("MAX_RECONCILE_BATCH_SIZE")) maxReconcileBatchSize = std::stoi(e);
        if (auto e = std::getenv("DAILY_SYNC_ENABLED")) dailySyncEnabled = (std::string(e) == "true");
        if (auto e = std::getenv("DAILY_SYNC_HOUR")) dailySyncHour = std::stoi(e);
        if (auto e = std::getenv("DAILY_SYNC_MINUTE")) dailySyncMinute = std::stoi(e);
        if (auto e = std::getenv("REVALIDATE_CERTS_ON_SYNC")) revalidateCertsOnSync = (std::string(e) == "true");
    }

    // Load user-configurable settings from database (defined after PgConnection)
    bool loadFromDatabase();
};

Config g_config;

// =============================================================================
// Database Statistics
// =============================================================================
struct DbStats {
    int cscaCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int storedInLdapCount = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};

// =============================================================================
// LDAP Statistics
// =============================================================================
struct LdapStats {
    int cscaCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int totalEntries = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};

// =============================================================================
// Sync Result
// =============================================================================
struct SyncResult {
    std::string status;  // SYNCED, DISCREPANCY, ERROR
    DbStats dbStats;
    LdapStats ldapStats;
    int cscaDiscrepancy = 0;
    int dscDiscrepancy = 0;
    int dscNcDiscrepancy = 0;
    int crlDiscrepancy = 0;
    int totalDiscrepancy = 0;
    int checkDurationMs = 0;
    std::string errorMessage;
    int syncStatusId = 0;
};

// =============================================================================
// PostgreSQL Connection
// =============================================================================
class PgConnection {
public:
    PgConnection() : conn_(nullptr) {}
    ~PgConnection() { disconnect(); }

    bool connect() {
        std::string connStr = "host=" + g_config.dbHost +
                              " port=" + std::to_string(g_config.dbPort) +
                              " dbname=" + g_config.dbName +
                              " user=" + g_config.dbUser +
                              " password=" + g_config.dbPassword;

        conn_ = PQconnectdb(connStr.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            spdlog::error("Database connection failed: {}", PQerrorMessage(conn_));
            return false;
        }
        return true;
    }

    void disconnect() {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    PGconn* get() { return conn_; }
    bool isConnected() const { return conn_ && PQstatus(conn_) == CONNECTION_OK; }

private:
    PGconn* conn_;
};

// =============================================================================
// Config Database Operations
// =============================================================================
bool Config::loadFromDatabase() {
    PgConnection conn;
    if (!conn.connect()) {
        spdlog::warn("Failed to connect to database for loading config");
        return false;
    }

    const char* query = "SELECT daily_sync_enabled, daily_sync_hour, daily_sync_minute, "
                       "auto_reconcile, revalidate_certs_on_sync, max_reconcile_batch_size "
                       "FROM sync_config WHERE id = 1";

    PGresult* res = PQexec(conn.get(), query);
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        dailySyncEnabled = std::string(PQgetvalue(res, 0, 0)) == "t";
        dailySyncHour = std::stoi(PQgetvalue(res, 0, 1));
        dailySyncMinute = std::stoi(PQgetvalue(res, 0, 2));
        autoReconcile = std::string(PQgetvalue(res, 0, 3)) == "t";
        revalidateCertsOnSync = std::string(PQgetvalue(res, 0, 4)) == "t";
        maxReconcileBatchSize = std::stoi(PQgetvalue(res, 0, 5));

        PQclear(res);
        spdlog::info("Loaded configuration from database");
        return true;
    }

    PQclear(res);
    spdlog::warn("No configuration found in database, using defaults");
    return false;
}

// =============================================================================
// Database Operations
// =============================================================================
DbStats getDbStats() {
    DbStats stats;
    PgConnection conn;

    if (!conn.connect()) {
        spdlog::error("Failed to connect to database for stats");
        return stats;
    }

    // Get certificate counts by type
    const char* certQuery = R"(
        SELECT certificate_type, COUNT(*) as cnt
        FROM certificate
        GROUP BY certificate_type
    )";

    PGresult* res = PQexec(conn.get(), certQuery);
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            std::string type = PQgetvalue(res, i, 0);
            int count = std::stoi(PQgetvalue(res, i, 1));
            if (type == "CSCA") stats.cscaCount = count;
            else if (type == "DSC") stats.dscCount = count;
            else if (type == "DSC_NC") stats.dscNcCount = count;
        }
    }
    PQclear(res);

    // Get CRL count
    res = PQexec(conn.get(), "SELECT COUNT(*) FROM crl");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        stats.crlCount = std::stoi(PQgetvalue(res, 0, 0));
    }
    PQclear(res);

    // Get stored_in_ldap count
    res = PQexec(conn.get(), "SELECT COUNT(*) FROM certificate WHERE stored_in_ldap = TRUE");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        stats.storedInLdapCount = std::stoi(PQgetvalue(res, 0, 0));
    }
    PQclear(res);

    // Get country breakdown
    const char* countryQuery = R"(
        SELECT country_code, certificate_type, COUNT(*) as cnt
        FROM certificate
        GROUP BY country_code, certificate_type
        ORDER BY country_code
    )";

    res = PQexec(conn.get(), countryQuery);
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            std::string country = PQgetvalue(res, i, 0);
            std::string type = PQgetvalue(res, i, 1);
            int count = std::stoi(PQgetvalue(res, i, 2));

            if (type == "CSCA") stats.countryStats[country]["csca"] = count;
            else if (type == "DSC") stats.countryStats[country]["dsc"] = count;
            else if (type == "DSC_NC") stats.countryStats[country]["dsc_nc"] = count;
        }
    }
    PQclear(res);

    return stats;
}

int saveSyncStatus(const SyncResult& result) {
    PgConnection conn;
    if (!conn.connect()) {
        spdlog::error("Failed to connect to database for saving sync status");
        return -1;
    }

    // Convert country stats to JSON string
    Json::Value dbCountryJson(Json::objectValue);
    for (const auto& [country, stats] : result.dbStats.countryStats) {
        Json::Value countryObj(Json::objectValue);
        for (const auto& [key, val] : stats) {
            countryObj[key] = val;
        }
        dbCountryJson[country] = countryObj;
    }

    Json::Value ldapCountryJson(Json::objectValue);
    for (const auto& [country, stats] : result.ldapStats.countryStats) {
        Json::Value countryObj(Json::objectValue);
        for (const auto& [key, val] : stats) {
            countryObj[key] = val;
        }
        ldapCountryJson[country] = countryObj;
    }

    Json::StreamWriterBuilder writer;
    std::string dbCountryStr = Json::writeString(writer, dbCountryJson);
    std::string ldapCountryStr = Json::writeString(writer, ldapCountryJson);

    std::string query = "INSERT INTO sync_status ("
        "db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
        "ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
        "csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
        "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms"
        ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20) RETURNING id";

    std::string dbCsca = std::to_string(result.dbStats.cscaCount);
    std::string dbDsc = std::to_string(result.dbStats.dscCount);
    std::string dbDscNc = std::to_string(result.dbStats.dscNcCount);
    std::string dbCrl = std::to_string(result.dbStats.crlCount);
    std::string dbStoredInLdap = std::to_string(result.dbStats.storedInLdapCount);
    std::string ldapCsca = std::to_string(result.ldapStats.cscaCount);
    std::string ldapDsc = std::to_string(result.ldapStats.dscCount);
    std::string ldapDscNc = std::to_string(result.ldapStats.dscNcCount);
    std::string ldapCrl = std::to_string(result.ldapStats.crlCount);
    std::string ldapTotal = std::to_string(result.ldapStats.totalEntries);
    std::string cscaDisc = std::to_string(result.cscaDiscrepancy);
    std::string dscDisc = std::to_string(result.dscDiscrepancy);
    std::string dscNcDisc = std::to_string(result.dscNcDiscrepancy);
    std::string crlDisc = std::to_string(result.crlDiscrepancy);
    std::string totalDisc = std::to_string(result.totalDiscrepancy);
    std::string durationMs = std::to_string(result.checkDurationMs);

    const char* paramValues[20] = {
        dbCsca.c_str(), dbDsc.c_str(), dbDscNc.c_str(), dbCrl.c_str(), dbStoredInLdap.c_str(),
        ldapCsca.c_str(), ldapDsc.c_str(), ldapDscNc.c_str(), ldapCrl.c_str(), ldapTotal.c_str(),
        cscaDisc.c_str(), dscDisc.c_str(), dscNcDisc.c_str(), crlDisc.c_str(), totalDisc.c_str(),
        dbCountryStr.c_str(), ldapCountryStr.c_str(),
        result.status.c_str(),
        result.errorMessage.empty() ? nullptr : result.errorMessage.c_str(),
        durationMs.c_str()
    };

    PGresult* res = PQexecParams(conn.get(), query.c_str(), 20, nullptr, paramValues, nullptr, nullptr, 0);

    int syncId = -1;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        syncId = std::stoi(PQgetvalue(res, 0, 0));
        spdlog::info("Saved sync status with id: {}", syncId);
    } else {
        spdlog::error("Failed to save sync status: {}", PQerrorMessage(conn.get()));
    }
    PQclear(res);

    return syncId;
}

Json::Value getLatestSyncStatus() {
    PgConnection conn;
    Json::Value result(Json::objectValue);

    if (!conn.connect()) {
        result["error"] = "Database connection failed";
        return result;
    }

    const char* query = R"(
        SELECT id, checked_at,
               db_csca_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count,
               ldap_csca_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries,
               csca_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy,
               status, error_message, check_duration_ms
        FROM sync_status
        ORDER BY checked_at DESC
        LIMIT 1
    )";

    PGresult* res = PQexec(conn.get(), query);

    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        result["id"] = std::stoi(PQgetvalue(res, 0, 0));
        result["checkedAt"] = PQgetvalue(res, 0, 1);

        Json::Value dbStats(Json::objectValue);
        dbStats["csca"] = std::stoi(PQgetvalue(res, 0, 2));
        dbStats["dsc"] = std::stoi(PQgetvalue(res, 0, 3));
        dbStats["dscNc"] = std::stoi(PQgetvalue(res, 0, 4));
        dbStats["crl"] = std::stoi(PQgetvalue(res, 0, 5));
        dbStats["storedInLdap"] = std::stoi(PQgetvalue(res, 0, 6));
        result["dbStats"] = dbStats;

        Json::Value ldapStats(Json::objectValue);
        ldapStats["csca"] = std::stoi(PQgetvalue(res, 0, 7));
        ldapStats["dsc"] = std::stoi(PQgetvalue(res, 0, 8));
        ldapStats["dscNc"] = std::stoi(PQgetvalue(res, 0, 9));
        ldapStats["crl"] = std::stoi(PQgetvalue(res, 0, 10));
        ldapStats["total"] = std::stoi(PQgetvalue(res, 0, 11));
        result["ldapStats"] = ldapStats;

        Json::Value discrepancy(Json::objectValue);
        discrepancy["csca"] = std::stoi(PQgetvalue(res, 0, 12));
        discrepancy["dsc"] = std::stoi(PQgetvalue(res, 0, 13));
        discrepancy["dscNc"] = std::stoi(PQgetvalue(res, 0, 14));
        discrepancy["crl"] = std::stoi(PQgetvalue(res, 0, 15));
        discrepancy["total"] = std::stoi(PQgetvalue(res, 0, 16));
        result["discrepancy"] = discrepancy;

        result["status"] = PQgetvalue(res, 0, 17);
        if (!PQgetisnull(res, 0, 18)) {
            result["errorMessage"] = PQgetvalue(res, 0, 18);
        }
        result["checkDurationMs"] = std::stoi(PQgetvalue(res, 0, 19));
    } else {
        result["status"] = "NO_DATA";
        result["message"] = "No sync status found";
    }
    PQclear(res);

    return result;
}

Json::Value getSyncHistory(int limit = 20) {
    PgConnection conn;
    Json::Value result(Json::arrayValue);

    if (!conn.connect()) {
        return result;
    }

    std::string query = "SELECT id, checked_at, "
        "db_csca_count + db_dsc_count + db_dsc_nc_count as db_total, "
        "ldap_csca_count + ldap_dsc_count + ldap_dsc_nc_count as ldap_total, "
        "total_discrepancy, status, check_duration_ms "
        "FROM sync_status ORDER BY checked_at DESC LIMIT " + std::to_string(limit);

    PGresult* res = PQexec(conn.get(), query.c_str());

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            Json::Value item(Json::objectValue);
            item["id"] = std::stoi(PQgetvalue(res, i, 0));
            item["checkedAt"] = PQgetvalue(res, i, 1);
            item["dbTotal"] = std::stoi(PQgetvalue(res, i, 2));
            item["ldapTotal"] = std::stoi(PQgetvalue(res, i, 3));
            item["totalDiscrepancy"] = std::stoi(PQgetvalue(res, i, 4));
            item["status"] = PQgetvalue(res, i, 5);
            item["checkDurationMs"] = std::stoi(PQgetvalue(res, i, 6));
            result.append(item);
        }
    }
    PQclear(res);

    return result;
}

// =============================================================================
// LDAP Operations
// =============================================================================
LdapStats getLdapStats() {
    LdapStats stats;

    std::string ldapUri = "ldap://" + g_config.ldapHost + ":" + std::to_string(g_config.ldapPort);

    LDAP* ld = nullptr;
    int rc = ldap_initialize(&ld, ldapUri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP initialize failed: {}", ldap_err2string(rc));
        return stats;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    // Authenticated bind for read access
    struct berval cred;
    cred.bv_val = const_cast<char*>(g_config.ldapBindPassword.c_str());
    cred.bv_len = g_config.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, g_config.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return stats;
    }

    // Search under dc=data for certificates
    LDAPMessage* result = nullptr;
    const char* attrs[] = {"dn", nullptr};
    struct timeval timeout = {60, 0};

    std::string dataBase = "dc=data,dc=download," + g_config.ldapBaseDn;
    rc = ldap_search_ext_s(ld, dataBase.c_str(), LDAP_SCOPE_SUBTREE,
                           "(objectClass=pkdDownload)", const_cast<char**>(attrs), 0,
                           nullptr, nullptr, &timeout, 0, &result);

    if (rc == LDAP_SUCCESS) {
        LDAPMessage* entry = ldap_first_entry(ld, result);
        while (entry) {
            char* dn = ldap_get_dn(ld, entry);
            if (dn) {
                std::string dnStr(dn);
                // Count by OU type in DN
                if (dnStr.find("o=csca,") != std::string::npos) {
                    stats.cscaCount++;
                } else if (dnStr.find("o=dsc,") != std::string::npos) {
                    stats.dscCount++;
                } else if (dnStr.find("o=crl,") != std::string::npos) {
                    stats.crlCount++;
                }

                // Extract country from DN
                size_t cPos = dnStr.find("c=");
                if (cPos != std::string::npos) {
                    size_t endPos = dnStr.find(",", cPos);
                    std::string country = dnStr.substr(cPos + 2, endPos - cPos - 2);
                    if (!country.empty()) {
                        if (dnStr.find("o=csca,") != std::string::npos) {
                            stats.countryStats[country]["csca"]++;
                        } else if (dnStr.find("o=dsc,") != std::string::npos) {
                            stats.countryStats[country]["dsc"]++;
                        }
                    }
                }

                ldap_memfree(dn);
            }
            entry = ldap_next_entry(ld, entry);
        }
    }
    if (result) ldap_msgfree(result);

    // Search under dc=nc-data for DSC_NC
    std::string ncDataBase = "dc=nc-data,dc=download," + g_config.ldapBaseDn;
    rc = ldap_search_ext_s(ld, ncDataBase.c_str(), LDAP_SCOPE_SUBTREE,
                           "(objectClass=pkdDownload)", const_cast<char**>(attrs), 0,
                           nullptr, nullptr, &timeout, 0, &result);

    if (rc == LDAP_SUCCESS) {
        stats.dscNcCount = ldap_count_entries(ld, result);
    }
    if (result) ldap_msgfree(result);

    stats.totalEntries = stats.cscaCount + stats.dscCount + stats.dscNcCount + stats.crlCount;

    ldap_unbind_ext_s(ld, nullptr, nullptr);
    return stats;
}

// =============================================================================
// Sync Checker
// =============================================================================
SyncResult performSyncCheck() {
    SyncResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    spdlog::info("Starting sync check...");

    // Get DB stats
    result.dbStats = getDbStats();
    spdlog::info("DB stats - CSCA: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                 result.dbStats.cscaCount, result.dbStats.dscCount,
                 result.dbStats.dscNcCount, result.dbStats.crlCount);

    // Get LDAP stats
    result.ldapStats = getLdapStats();
    spdlog::info("LDAP stats - CSCA: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                 result.ldapStats.cscaCount, result.ldapStats.dscCount,
                 result.ldapStats.dscNcCount, result.ldapStats.crlCount);

    // Calculate discrepancies
    result.cscaDiscrepancy = result.dbStats.cscaCount - result.ldapStats.cscaCount;
    result.dscDiscrepancy = result.dbStats.dscCount - result.ldapStats.dscCount;
    result.dscNcDiscrepancy = result.dbStats.dscNcCount - result.ldapStats.dscNcCount;
    result.crlDiscrepancy = result.dbStats.crlCount - result.ldapStats.crlCount;
    result.totalDiscrepancy = std::abs(result.cscaDiscrepancy) +
                               std::abs(result.dscDiscrepancy) +
                               std::abs(result.dscNcDiscrepancy) +
                               std::abs(result.crlDiscrepancy);

    // Determine status
    if (result.totalDiscrepancy == 0) {
        result.status = "SYNCED";
        spdlog::info("Sync check completed: SYNCED");
    } else {
        result.status = "DISCREPANCY";
        spdlog::warn("Sync check completed: DISCREPANCY (total: {})", result.totalDiscrepancy);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    // Save to database
    result.syncStatusId = saveSyncStatus(result);

    return result;
}

// =============================================================================
// Certificate Re-validation
// =============================================================================

struct RevalidationResult {
    int totalProcessed = 0;
    int newlyExpired = 0;
    int newlyValid = 0;
    int unchanged = 0;
    int errors = 0;
    int durationMs = 0;
};

// Check if certificate is expired based on not_after timestamp
bool isCertificateExpired(const std::string& notAfterStr) {
    if (notAfterStr.empty()) return false;

    // Parse PostgreSQL timestamp format: "2025-12-31 23:59:59+00"
    std::tm tm = {};
    std::istringstream ss(notAfterStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        // Try ISO format: "2025-12-31T23:59:59"
        ss.clear();
        ss.str(notAfterStr);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (ss.fail()) {
            spdlog::warn("Failed to parse timestamp: {}", notAfterStr);
            return false;
        }
    }

    time_t certTime = std::mktime(&tm);
    time_t now = std::time(nullptr);

    return now > certTime;
}

// Re-validate all certificates and update expiration status
RevalidationResult performCertificateRevalidation() {
    RevalidationResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    spdlog::info("Starting certificate re-validation...");

    PgConnection conn;
    if (!conn.connect()) {
        spdlog::error("Failed to connect to database for certificate re-validation");
        result.errors = 1;
        return result;
    }

    // Get all validation results with expiration info
    const char* selectQuery = R"(
        SELECT vr.id, vr.certificate_id, vr.certificate_type, vr.country_code,
               vr.is_expired, vr.validation_status, vr.not_after
        FROM validation_result vr
        WHERE vr.not_after IS NOT NULL
    )";

    PGresult* res = PQexec(conn.get(), selectQuery);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Failed to query validation results: {}", PQerrorMessage(conn.get()));
        PQclear(res);
        result.errors = 1;
        return result;
    }

    int rows = PQntuples(res);
    spdlog::info("Processing {} certificates for expiration check", rows);

    // Process each certificate
    for (int i = 0; i < rows; i++) {
        std::string vrId = PQgetvalue(res, i, 0);
        std::string certId = PQgetvalue(res, i, 1);
        std::string certType = PQgetvalue(res, i, 2);
        std::string countryCode = PQgetvalue(res, i, 3);
        bool wasExpired = std::string(PQgetvalue(res, i, 4)) == "t";
        std::string oldStatus = PQgetvalue(res, i, 5);
        std::string notAfter = PQgetvalue(res, i, 6);

        bool isNowExpired = isCertificateExpired(notAfter);

        result.totalProcessed++;

        // Check if status changed
        if (isNowExpired != wasExpired) {
            // Update validation_result
            std::string newStatus = isNowExpired ? "INVALID" : "VALID";

            // If certificate just expired, update status
            // If certificate is no longer expired (unlikely but handle it), update status
            std::string updateQuery = "UPDATE validation_result SET is_expired = $1, "
                "validation_status = CASE WHEN $1 = TRUE THEN 'INVALID' ELSE validation_status END, "
                "validated_at = NOW() WHERE id = $2";

            const char* paramValues[2];
            std::string expiredStr = isNowExpired ? "TRUE" : "FALSE";
            paramValues[0] = expiredStr.c_str();
            paramValues[1] = vrId.c_str();

            PGresult* updateRes = PQexecParams(conn.get(), updateQuery.c_str(), 2, nullptr, paramValues, nullptr, nullptr, 0);

            if (PQresultStatus(updateRes) == PGRES_COMMAND_OK) {
                if (isNowExpired) {
                    result.newlyExpired++;
                    spdlog::debug("Certificate {} ({} {}) marked as expired", certId, countryCode, certType);
                } else {
                    result.newlyValid++;
                    spdlog::debug("Certificate {} ({} {}) no longer expired", certId, countryCode, certType);
                }
            } else {
                result.errors++;
                spdlog::error("Failed to update certificate {}: {}", certId, PQerrorMessage(conn.get()));
            }
            PQclear(updateRes);
        } else {
            result.unchanged++;
        }
    }
    PQclear(res);

    // Update upload file statistics if any changes were made
    if (result.newlyExpired > 0 || result.newlyValid > 0) {
        const char* updateStatsQuery = R"(
            UPDATE uploaded_file uf SET
                expired_count = COALESCE((
                    SELECT COUNT(*) FROM validation_result vr
                    WHERE vr.upload_id = uf.id AND vr.is_expired = TRUE
                ), 0)
            WHERE EXISTS (SELECT 1 FROM validation_result vr WHERE vr.upload_id = uf.id)
        )";

        PGresult* statsRes = PQexec(conn.get(), updateStatsQuery);
        if (PQresultStatus(statsRes) != PGRES_COMMAND_OK) {
            spdlog::warn("Failed to update upload file statistics: {}", PQerrorMessage(conn.get()));
        }
        PQclear(statsRes);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    spdlog::info("Certificate re-validation completed: {} processed, {} newly expired, {} unchanged, {} errors ({}ms)",
                 result.totalProcessed, result.newlyExpired, result.unchanged, result.errors, result.durationMs);

    return result;
}

// Save re-validation result to database
void saveRevalidationResult(const RevalidationResult& result) {
    PgConnection conn;
    if (!conn.connect()) {
        spdlog::error("Failed to connect to database for saving revalidation result");
        return;
    }

    // Create table if not exists
    const char* createTableQuery = R"(
        CREATE TABLE IF NOT EXISTS revalidation_history (
            id SERIAL PRIMARY KEY,
            executed_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
            total_processed INTEGER NOT NULL DEFAULT 0,
            newly_expired INTEGER NOT NULL DEFAULT 0,
            newly_valid INTEGER NOT NULL DEFAULT 0,
            unchanged INTEGER NOT NULL DEFAULT 0,
            errors INTEGER NOT NULL DEFAULT 0,
            duration_ms INTEGER NOT NULL DEFAULT 0
        )
    )";

    PGresult* createRes = PQexec(conn.get(), createTableQuery);
    if (PQresultStatus(createRes) != PGRES_COMMAND_OK) {
        spdlog::warn("Failed to create revalidation_history table: {}", PQerrorMessage(conn.get()));
    }
    PQclear(createRes);

    // Insert result
    std::string insertQuery = "INSERT INTO revalidation_history "
        "(total_processed, newly_expired, newly_valid, unchanged, errors, duration_ms) "
        "VALUES ($1, $2, $3, $4, $5, $6)";

    std::string totalStr = std::to_string(result.totalProcessed);
    std::string expiredStr = std::to_string(result.newlyExpired);
    std::string validStr = std::to_string(result.newlyValid);
    std::string unchangedStr = std::to_string(result.unchanged);
    std::string errorsStr = std::to_string(result.errors);
    std::string durationStr = std::to_string(result.durationMs);

    const char* paramValues[6] = {
        totalStr.c_str(), expiredStr.c_str(), validStr.c_str(),
        unchangedStr.c_str(), errorsStr.c_str(), durationStr.c_str()
    };

    PGresult* res = PQexecParams(conn.get(), insertQuery.c_str(), 6, nullptr, paramValues, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to save revalidation result: {}", PQerrorMessage(conn.get()));
    } else {
        spdlog::info("Revalidation result saved to database");
    }
    PQclear(res);
}

// Get revalidation history
Json::Value getRevalidationHistory(int limit = 10) {
    PgConnection conn;
    Json::Value result(Json::arrayValue);

    if (!conn.connect()) {
        return result;
    }

    std::string query = "SELECT id, executed_at, total_processed, newly_expired, newly_valid, "
        "unchanged, errors, duration_ms FROM revalidation_history "
        "ORDER BY executed_at DESC LIMIT " + std::to_string(limit);

    PGresult* res = PQexec(conn.get(), query.c_str());

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            Json::Value item(Json::objectValue);
            item["id"] = std::stoi(PQgetvalue(res, i, 0));
            item["executedAt"] = PQgetvalue(res, i, 1);
            item["totalProcessed"] = std::stoi(PQgetvalue(res, i, 2));
            item["newlyExpired"] = std::stoi(PQgetvalue(res, i, 3));
            item["newlyValid"] = std::stoi(PQgetvalue(res, i, 4));
            item["unchanged"] = std::stoi(PQgetvalue(res, i, 5));
            item["errors"] = std::stoi(PQgetvalue(res, i, 6));
            item["durationMs"] = std::stoi(PQgetvalue(res, i, 7));
            result.append(item);
        }
    }
    PQclear(res);

    return result;
}

// =============================================================================
// Daily Scheduler
// =============================================================================

// Calculate seconds until next scheduled time
int secondsUntilScheduledTime(int targetHour, int targetMinute) {
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm* localTm = std::localtime(&nowTime);

    // Create target time for today
    std::tm targetTm = *localTm;
    targetTm.tm_hour = targetHour;
    targetTm.tm_min = targetMinute;
    targetTm.tm_sec = 0;

    std::time_t targetTime = std::mktime(&targetTm);

    // If target time has passed today, schedule for tomorrow
    if (targetTime <= nowTime) {
        targetTime += 24 * 60 * 60;  // Add 24 hours
    }

    return static_cast<int>(targetTime - nowTime);
}

std::string formatScheduledTime(int targetHour, int targetMinute) {
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << targetHour << ":"
       << std::setfill('0') << std::setw(2) << targetMinute;
    return ss.str();
}

// =============================================================================
// Scheduler (Daily sync only)
// =============================================================================
class SyncScheduler {
public:
    SyncScheduler() : running_(false), lastDailySyncDate_("") {}

    void start() {
        running_ = true;

        // Perform initial sync check after startup delay
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (running_) {
                spdlog::info("Performing initial sync check after startup...");
                try {
                    performSyncCheck();
                } catch (const std::exception& e) {
                    spdlog::error("Initial sync check failed: {}", e.what());
                }
            }
        }).detach();

        // Start daily sync thread
        if (g_config.dailySyncEnabled) {
            dailyThread_ = std::thread([this]() {
                std::string scheduledTime = formatScheduledTime(g_config.dailySyncHour, g_config.dailySyncMinute);
                spdlog::info("Daily sync scheduler started (scheduled at {} daily)", scheduledTime);

                while (running_) {
                    // Calculate time until next scheduled run
                    int waitSeconds = secondsUntilScheduledTime(g_config.dailySyncHour, g_config.dailySyncMinute);
                    spdlog::info("Next daily sync in {} seconds ({} hours {} minutes)",
                                 waitSeconds, waitSeconds / 3600, (waitSeconds % 3600) / 60);

                    // Wait until scheduled time
                    std::unique_lock<std::mutex> lock(dailyMutex_);
                    bool signaled = dailyCv_.wait_for(lock, std::chrono::seconds(waitSeconds),
                                [this]() { return !running_ || forceDailySync_; });

                    if (!running_) break;

                    // Check if we should run (either scheduled time reached or forced)
                    std::string today = getCurrentDateString();
                    if (forceDailySync_ || lastDailySyncDate_ != today) {
                        forceDailySync_ = false;
                        lastDailySyncDate_ = today;

                        spdlog::info("=== Starting Daily Sync Tasks ===");

                        try {
                            // 1. Perform sync check
                            spdlog::info("[Daily] Step 1: Performing sync check...");
                            performSyncCheck();

                            // 2. Re-validate certificates if enabled
                            if (g_config.revalidateCertsOnSync) {
                                spdlog::info("[Daily] Step 2: Performing certificate re-validation...");
                                RevalidationResult revalResult = performCertificateRevalidation();
                                saveRevalidationResult(revalResult);
                            }

                            spdlog::info("=== Daily Sync Tasks Completed ===");
                        } catch (const std::exception& e) {
                            spdlog::error("Daily sync failed: {}", e.what());
                        }
                    }
                }

                spdlog::info("Daily sync scheduler stopped");
            });
        }
    }

    void stop() {
        running_ = false;
        dailyCv_.notify_all();

        if (dailyThread_.joinable()) {
            dailyThread_.join();
        }
    }

    void triggerDailySync() {
        {
            std::lock_guard<std::mutex> lock(dailyMutex_);
            forceDailySync_ = true;
        }
        dailyCv_.notify_all();
    }

private:
    std::string getCurrentDateString() {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm* localTm = std::localtime(&nowTime);

        std::ostringstream ss;
        ss << std::put_time(localTm, "%Y-%m-%d");
        return ss.str();
    }

    std::atomic<bool> running_;
    std::string lastDailySyncDate_;
    bool forceDailySync_ = false;

    // Daily sync
    std::thread dailyThread_;
    std::mutex dailyMutex_;
    std::condition_variable dailyCv_;
};

SyncScheduler g_scheduler;

// =============================================================================
// HTTP Handlers
// =============================================================================

// Health check
void handleHealth(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value response(Json::objectValue);
    response["status"] = "UP";
    response["service"] = "sync-service";
    response["timestamp"] = trantor::Date::now().toFormattedString(false);

    // Check DB connection
    PgConnection conn;
    if (conn.connect()) {
        response["database"] = "UP";
    } else {
        response["database"] = "DOWN";
        response["status"] = "DEGRADED";
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

// Get latest sync status
void handleSyncStatus(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value result = getLatestSyncStatus();
    auto resp = HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

// Get sync history
void handleSyncHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int limit = 20;
    if (auto l = req->getParameter("limit"); !l.empty()) {
        limit = std::stoi(l);
    }

    Json::Value result = getSyncHistory(limit);
    auto resp = HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

// Trigger manual sync check
void handleSyncCheck(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        SyncResult result = performSyncCheck();

        Json::Value response(Json::objectValue);
        response["success"] = true;
        response["syncStatusId"] = result.syncStatusId;
        response["status"] = result.status;

        Json::Value dbStats(Json::objectValue);
        dbStats["csca"] = result.dbStats.cscaCount;
        dbStats["dsc"] = result.dbStats.dscCount;
        dbStats["dscNc"] = result.dbStats.dscNcCount;
        dbStats["crl"] = result.dbStats.crlCount;
        response["dbStats"] = dbStats;

        Json::Value ldapStats(Json::objectValue);
        ldapStats["csca"] = result.ldapStats.cscaCount;
        ldapStats["dsc"] = result.ldapStats.dscCount;
        ldapStats["dscNc"] = result.ldapStats.dscNcCount;
        ldapStats["crl"] = result.ldapStats.crlCount;
        response["ldapStats"] = ldapStats;

        Json::Value discrepancy(Json::objectValue);
        discrepancy["csca"] = result.cscaDiscrepancy;
        discrepancy["dsc"] = result.dscDiscrepancy;
        discrepancy["dscNc"] = result.dscNcDiscrepancy;
        discrepancy["crl"] = result.crlDiscrepancy;
        discrepancy["total"] = result.totalDiscrepancy;
        response["discrepancy"] = discrepancy;

        response["checkDurationMs"] = result.checkDurationMs;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);
    } catch (const std::exception& e) {
        Json::Value error(Json::objectValue);
        error["success"] = false;
        error["error"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

// Get unresolved discrepancies
void handleDiscrepancies(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    PgConnection conn;
    if (!conn.connect()) {
        Json::Value error(Json::objectValue);
        error["error"] = "Database connection failed";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    const char* query = R"(
        SELECT id, detected_at, item_type, certificate_type, country_code, fingerprint,
               issue_type, db_exists, ldap_exists
        FROM sync_discrepancy
        WHERE resolved = FALSE
        ORDER BY detected_at DESC
        LIMIT 100
    )";

    PGresult* res = PQexec(conn.get(), query);

    Json::Value result(Json::arrayValue);
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            Json::Value item(Json::objectValue);
            item["id"] = PQgetvalue(res, i, 0);
            item["detectedAt"] = PQgetvalue(res, i, 1);
            item["itemType"] = PQgetvalue(res, i, 2);
            if (!PQgetisnull(res, i, 3)) item["certificateType"] = PQgetvalue(res, i, 3);
            if (!PQgetisnull(res, i, 4)) item["countryCode"] = PQgetvalue(res, i, 4);
            if (!PQgetisnull(res, i, 5)) item["fingerprint"] = PQgetvalue(res, i, 5);
            item["issueType"] = PQgetvalue(res, i, 6);
            item["dbExists"] = std::string(PQgetvalue(res, i, 7)) == "t";
            item["ldapExists"] = std::string(PQgetvalue(res, i, 8)) == "t";
            result.append(item);
        }
    }
    PQclear(res);

    auto resp = HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

// Trigger reconciliation (placeholder for future implementation)
void handleReconcile(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    // TODO: Implement reconciliation logic
    // For now, just return a placeholder response
    Json::Value response(Json::objectValue);
    response["success"] = true;
    response["message"] = "Reconciliation triggered (not yet implemented)";
    response["autoReconcileEnabled"] = g_config.autoReconcile;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

// Get sync configuration
void handleSyncConfig(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value config(Json::objectValue);
    config["autoReconcile"] = g_config.autoReconcile;
    config["maxReconcileBatchSize"] = g_config.maxReconcileBatchSize;
    config["dailySyncEnabled"] = g_config.dailySyncEnabled;
    config["dailySyncHour"] = g_config.dailySyncHour;
    config["dailySyncMinute"] = g_config.dailySyncMinute;
    config["dailySyncTime"] = formatScheduledTime(g_config.dailySyncHour, g_config.dailySyncMinute);
    config["revalidateCertsOnSync"] = g_config.revalidateCertsOnSync;

    auto resp = HttpResponse::newHttpJsonResponse(config);
    callback(resp);
}

// Update sync configuration
void handleUpdateSyncConfig(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        auto jsonPtr = req->getJsonObject();
        if (!jsonPtr) {
            Json::Value error(Json::objectValue);
            error["success"] = false;
            error["error"] = "Invalid JSON request";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        const Json::Value& json = *jsonPtr;

        // Validate input
        if (json.isMember("dailySyncHour")) {
            int hour = json["dailySyncHour"].asInt();
            if (hour < 0 || hour > 23) {
                Json::Value error(Json::objectValue);
                error["success"] = false;
                error["error"] = "dailySyncHour must be between 0 and 23";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k400BadRequest);
                callback(resp);
                return;
            }
        }

        if (json.isMember("dailySyncMinute")) {
            int minute = json["dailySyncMinute"].asInt();
            if (minute < 0 || minute > 59) {
                Json::Value error(Json::objectValue);
                error["success"] = false;
                error["error"] = "dailySyncMinute must be between 0 and 59";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k400BadRequest);
                callback(resp);
                return;
            }
        }

        // Connect to database
        PgConnection conn;
        if (!conn.connect()) {
            Json::Value error(Json::objectValue);
            error["success"] = false;
            error["error"] = "Database connection failed";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
            return;
        }

        // Build UPDATE query dynamically
        std::vector<std::string> setClauses;
        std::vector<std::string> paramValues;
        int paramIndex = 1;

        if (json.isMember("dailySyncEnabled")) {
            setClauses.push_back("daily_sync_enabled = $" + std::to_string(paramIndex++));
            paramValues.push_back(json["dailySyncEnabled"].asBool() ? "TRUE" : "FALSE");
        }
        if (json.isMember("dailySyncHour")) {
            setClauses.push_back("daily_sync_hour = $" + std::to_string(paramIndex++));
            paramValues.push_back(std::to_string(json["dailySyncHour"].asInt()));
        }
        if (json.isMember("dailySyncMinute")) {
            setClauses.push_back("daily_sync_minute = $" + std::to_string(paramIndex++));
            paramValues.push_back(std::to_string(json["dailySyncMinute"].asInt()));
        }
        if (json.isMember("autoReconcile")) {
            setClauses.push_back("auto_reconcile = $" + std::to_string(paramIndex++));
            paramValues.push_back(json["autoReconcile"].asBool() ? "TRUE" : "FALSE");
        }
        if (json.isMember("revalidateCertsOnSync")) {
            setClauses.push_back("revalidate_certs_on_sync = $" + std::to_string(paramIndex++));
            paramValues.push_back(json["revalidateCertsOnSync"].asBool() ? "TRUE" : "FALSE");
        }
        if (json.isMember("maxReconcileBatchSize")) {
            setClauses.push_back("max_reconcile_batch_size = $" + std::to_string(paramIndex++));
            paramValues.push_back(std::to_string(json["maxReconcileBatchSize"].asInt()));
        }

        if (setClauses.empty()) {
            Json::Value error(Json::objectValue);
            error["success"] = false;
            error["error"] = "No fields to update";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        // Add updated_at
        setClauses.push_back("updated_at = NOW()");

        std::string query = "UPDATE sync_config SET " +
                           std::accumulate(setClauses.begin(), setClauses.end(), std::string(),
                                         [](const std::string& a, const std::string& b) {
                                             return a.empty() ? b : a + ", " + b;
                                         }) +
                           " WHERE id = 1";

        // Convert to const char* array
        std::vector<const char*> paramPtrs;
        for (const auto& val : paramValues) {
            paramPtrs.push_back(val.c_str());
        }

        PGresult* res = PQexecParams(conn.get(), query.c_str(), paramValues.size(),
                                    nullptr, paramPtrs.data(), nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            Json::Value error(Json::objectValue);
            error["success"] = false;
            error["error"] = "Failed to update configuration";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
            return;
        }
        PQclear(res);

        // Reload configuration from database
        g_config.loadFromDatabase();

        // Restart scheduler with new settings
        spdlog::info("Configuration updated, restarting scheduler...");
        g_scheduler.stop();
        g_scheduler.start();

        Json::Value response(Json::objectValue);
        response["success"] = true;
        response["message"] = "Configuration updated successfully";
        response["config"]["autoReconcile"] = g_config.autoReconcile;
        response["config"]["maxReconcileBatchSize"] = g_config.maxReconcileBatchSize;
        response["config"]["dailySyncEnabled"] = g_config.dailySyncEnabled;
        response["config"]["dailySyncHour"] = g_config.dailySyncHour;
        response["config"]["dailySyncMinute"] = g_config.dailySyncMinute;
        response["config"]["dailySyncTime"] = formatScheduledTime(g_config.dailySyncHour, g_config.dailySyncMinute);
        response["config"]["revalidateCertsOnSync"] = g_config.revalidateCertsOnSync;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        Json::Value error(Json::objectValue);
        error["success"] = false;
        error["error"] = std::string("Exception: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

// Trigger manual certificate re-validation
void handleRevalidate(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        spdlog::info("Manual certificate re-validation triggered via API");
        RevalidationResult result = performCertificateRevalidation();
        saveRevalidationResult(result);

        Json::Value response(Json::objectValue);
        response["success"] = true;
        response["totalProcessed"] = result.totalProcessed;
        response["newlyExpired"] = result.newlyExpired;
        response["newlyValid"] = result.newlyValid;
        response["unchanged"] = result.unchanged;
        response["errors"] = result.errors;
        response["durationMs"] = result.durationMs;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);
    } catch (const std::exception& e) {
        Json::Value error(Json::objectValue);
        error["success"] = false;
        error["error"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

// Get re-validation history
void handleRevalidationHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int limit = 10;
    if (auto l = req->getParameter("limit"); !l.empty()) {
        limit = std::stoi(l);
    }

    Json::Value result = getRevalidationHistory(limit);
    auto resp = HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

// Trigger daily sync manually
void handleTriggerDailySync(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    spdlog::info("Manual daily sync triggered via API");
    g_scheduler.triggerDailySync();

    Json::Value response(Json::objectValue);
    response["success"] = true;
    response["message"] = "Daily sync triggered";

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

// =============================================================================
// Logging Setup
// =============================================================================
void setupLogging() {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);

        std::shared_ptr<spdlog::logger> logger;

        try {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "/app/logs/sync-service.log", 1024 * 1024 * 10, 5);
            file_sink->set_level(spdlog::level::debug);

            logger = std::make_shared<spdlog::logger>("sync",
                spdlog::sinks_init_list{console_sink, file_sink});
        } catch (...) {
            // Fallback to console-only logging
            logger = std::make_shared<spdlog::logger>("sync", console_sink);
            std::cerr << "Warning: Could not create log file, using console only" << std::endl;
        }

        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    } catch (const std::exception& e) {
        std::cerr << "Logging setup failed: " << e.what() << std::endl;
    }
}

// =============================================================================
// Main
// =============================================================================
int main() {
    // Load configuration from environment
    g_config.loadFromEnv();

    // Setup logging
    setupLogging();

    spdlog::info("===========================================");
    spdlog::info("  ICAO Local PKD - Sync Service v1.3.0");
    spdlog::info("===========================================");
    spdlog::info("Server port: {}", g_config.serverPort);
    spdlog::info("Database: {}:{}/{}", g_config.dbHost, g_config.dbPort, g_config.dbName);
    spdlog::info("LDAP (read): {}:{}", g_config.ldapHost, g_config.ldapPort);
    spdlog::info("LDAP (write): {}:{}", g_config.ldapWriteHost, g_config.ldapWritePort);

    // Load user-configurable settings from database
    spdlog::info("Loading configuration from database...");
    g_config.loadFromDatabase();

    spdlog::info("Daily sync: {} at {}", g_config.dailySyncEnabled ? "enabled" : "disabled",
                 formatScheduledTime(g_config.dailySyncHour, g_config.dailySyncMinute));
    spdlog::info("Certificate re-validation on sync: {}", g_config.revalidateCertsOnSync ? "enabled" : "disabled");
    spdlog::info("Auto reconcile: {}", g_config.autoReconcile ? "enabled" : "disabled");

    // Register HTTP handlers
    app().registerHandler("/api/sync/health",
        &handleHealth, {Get});
    app().registerHandler("/api/sync/status",
        &handleSyncStatus, {Get});
    app().registerHandler("/api/sync/history",
        &handleSyncHistory, {Get});
    app().registerHandler("/api/sync/check",
        &handleSyncCheck, {Post});
    app().registerHandler("/api/sync/discrepancies",
        &handleDiscrepancies, {Get});
    app().registerHandler("/api/sync/reconcile",
        &handleReconcile, {Post});
    app().registerHandler("/api/sync/config",
        &handleSyncConfig, {Get});
    app().registerHandler("/api/sync/config",
        &handleUpdateSyncConfig, {Put});

    // Re-validation endpoints
    app().registerHandler("/api/sync/revalidate",
        &handleRevalidate, {Post});
    app().registerHandler("/api/sync/revalidation-history",
        &handleRevalidationHistory, {Get});
    app().registerHandler("/api/sync/trigger-daily",
        &handleTriggerDailySync, {Post});

    // OpenAPI specification endpoint
    app().registerHandler("/api/openapi.yaml",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/openapi.yaml");

            std::string spec = R"(openapi: 3.0.3
info:
  title: Sync Service API
  description: |
    DB-LDAP Synchronization and Certificate Re-validation Service.

    ## Changelog
    - v1.1.0 (2026-01-06): Daily scheduler, certificate re-validation
    - v1.0.0 (2026-01-03): Initial release
  version: 1.1.0
servers:
  - url: /
tags:
  - name: Health
    description: Health check
  - name: Sync
    description: Synchronization operations
  - name: Revalidation
    description: Certificate re-validation operations
  - name: Config
    description: Configuration
paths:
  /api/sync/health:
    get:
      tags: [Health]
      summary: Service health check
      responses:
        '200':
          description: Health status
  /api/sync/status:
    get:
      tags: [Sync]
      summary: Get sync status
      description: Returns DB and LDAP statistics
      responses:
        '200':
          description: Sync status
  /api/sync/check:
    post:
      tags: [Sync]
      summary: Trigger sync check
      responses:
        '200':
          description: Check result
  /api/sync/discrepancies:
    get:
      tags: [Sync]
      summary: Get discrepancies
      parameters:
        - name: type
          in: query
          schema:
            type: string
        - name: limit
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: Discrepancy list
  /api/sync/reconcile:
    post:
      tags: [Sync]
      summary: Reconcile discrepancies
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                mode:
                  type: string
                dryRun:
                  type: boolean
      responses:
        '200':
          description: Reconciliation result
  /api/sync/history:
    get:
      tags: [Sync]
      summary: Get sync history
      parameters:
        - name: limit
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: Sync history
  /api/sync/config:
    get:
      tags: [Config]
      summary: Get configuration
      responses:
        '200':
          description: Current configuration
  /api/sync/revalidate:
    post:
      tags: [Revalidation]
      summary: Trigger certificate re-validation
      description: Re-check all certificates for expiration and update validation status
      responses:
        '200':
          description: Re-validation result
  /api/sync/revalidation-history:
    get:
      tags: [Revalidation]
      summary: Get re-validation history
      parameters:
        - name: limit
          in: query
          schema:
            type: integer
            default: 10
      responses:
        '200':
          description: Re-validation history
  /api/sync/trigger-daily:
    post:
      tags: [Sync]
      summary: Trigger daily sync manually
      description: Manually trigger the daily sync process including certificate re-validation
      responses:
        '200':
          description: Daily sync triggered
)";

            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(spec);
            resp->setContentTypeCode(CT_TEXT_PLAIN);
            resp->addHeader("Content-Type", "application/x-yaml");
            callback(resp);
        }, {Get});

    // Swagger UI redirect
    app().registerHandler("/api/docs",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto resp = HttpResponse::newRedirectionResponse("/swagger-ui/index.html");
            callback(resp);
        }, {Get});

    // Enable CORS
    app().registerPostHandlingAdvice([](const HttpRequestPtr&, const HttpResponsePtr& resp) {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    });

    // Start scheduler
    g_scheduler.start();

    // Start server
    spdlog::info("Starting HTTP server on port {}...", g_config.serverPort);
    app().addListener("0.0.0.0", g_config.serverPort)
        .setThreadNum(4)  // Increased from 2 to handle concurrent requests
        .run();

    // Cleanup
    g_scheduler.stop();

    return 0;
}

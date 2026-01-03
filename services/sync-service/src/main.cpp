// =============================================================================
// ICAO Local PKD - Sync Service
// =============================================================================
// Version: 1.0.0
// Description: DB-LDAP synchronization checker and reconciler
// =============================================================================

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <json/json.h>
#include <libpq-fe.h>
#include <ldap.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

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
    int syncIntervalMinutes = 5;
    bool autoReconcile = true;
    int maxReconcileBatchSize = 100;

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
        if (auto e = std::getenv("SYNC_INTERVAL_MINUTES")) syncIntervalMinutes = std::stoi(e);
        if (auto e = std::getenv("AUTO_RECONCILE")) autoReconcile = (std::string(e) == "true");
        if (auto e = std::getenv("MAX_RECONCILE_BATCH_SIZE")) maxReconcileBatchSize = std::stoi(e);
    }
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
        "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms"
        ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15) RETURNING id";

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
    std::string durationMs = std::to_string(result.checkDurationMs);

    const char* paramValues[15] = {
        dbCsca.c_str(), dbDsc.c_str(), dbDscNc.c_str(), dbCrl.c_str(), dbStoredInLdap.c_str(),
        ldapCsca.c_str(), ldapDsc.c_str(), ldapDscNc.c_str(), ldapCrl.c_str(), ldapTotal.c_str(),
        dbCountryStr.c_str(), ldapCountryStr.c_str(),
        result.status.c_str(),
        result.errorMessage.empty() ? nullptr : result.errorMessage.c_str(),
        durationMs.c_str()
    };

    PGresult* res = PQexecParams(conn.get(), query.c_str(), 15, nullptr, paramValues, nullptr, nullptr, 0);

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
// Scheduler
// =============================================================================
class SyncScheduler {
public:
    SyncScheduler() : running_(false) {}

    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            spdlog::info("Sync scheduler started (interval: {} minutes)", g_config.syncIntervalMinutes);

            // Initial sync after startup delay
            std::this_thread::sleep_for(std::chrono::seconds(10));

            while (running_) {
                try {
                    performSyncCheck();
                } catch (const std::exception& e) {
                    spdlog::error("Sync check failed: {}", e.what());
                }

                // Wait for next interval
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::minutes(g_config.syncIntervalMinutes),
                            [this]() { return !running_; });
            }

            spdlog::info("Sync scheduler stopped");
        });
    }

    void stop() {
        running_ = false;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void triggerNow() {
        cv_.notify_all();
    }

private:
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
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
    config["syncIntervalMinutes"] = g_config.syncIntervalMinutes;
    config["autoReconcile"] = g_config.autoReconcile;
    config["maxReconcileBatchSize"] = g_config.maxReconcileBatchSize;

    auto resp = HttpResponse::newHttpJsonResponse(config);
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
    // Load configuration
    g_config.loadFromEnv();

    // Setup logging
    setupLogging();

    spdlog::info("===========================================");
    spdlog::info("  ICAO Local PKD - Sync Service v1.0.0");
    spdlog::info("===========================================");
    spdlog::info("Server port: {}", g_config.serverPort);
    spdlog::info("Database: {}:{}/{}", g_config.dbHost, g_config.dbPort, g_config.dbName);
    spdlog::info("LDAP (read): {}:{}", g_config.ldapHost, g_config.ldapPort);
    spdlog::info("LDAP (write): {}:{}", g_config.ldapWriteHost, g_config.ldapWritePort);
    spdlog::info("Sync interval: {} minutes", g_config.syncIntervalMinutes);
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
        .setThreadNum(2)
        .run();

    // Cleanup
    g_scheduler.stop();

    return 0;
}

// =============================================================================
// ICAO Local PKD - PKD Relay Service
// =============================================================================
// Version: 2.1.0
// Description: Data relay layer (ICAO portal monitoring, LDIF upload/parsing, DB-LDAP sync)
// =============================================================================
// Changelog:
//   v2.1.0 (2026-01-26): MLSC (Master List Signer Certificate) sync support
//   v2.0.5 (2026-01-25): CRL reconciliation support, reconciliation_log UUID fix
//   v1.4.0 (2026-01-14): Modularized code, Auto Reconcile implementation
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

// Modularized components
#include "relay/sync/common/types.h"
#include "relay/sync/common/config.h"
#include "relay/sync/reconciliation_engine.h"

// Repository Pattern
#include "db_connection_pool.h"
#include <ldap_connection_pool.h>  // v2.4.3: LDAP connection pool
#include "repositories/sync_status_repository.h"
#include "repositories/certificate_repository.h"
#include "repositories/crl_repository.h"
#include "repositories/reconciliation_repository.h"
#include "services/sync_service.h"
#include "services/reconciliation_service.h"

using namespace drogon;
using namespace icao::relay;

// =============================================================================
// Global Configuration Instance
// =============================================================================
Config g_config;

// =============================================================================
// Repository Pattern - Global Service Instances
// =============================================================================
std::shared_ptr<common::DbConnectionPool> g_dbPool;
std::shared_ptr<common::LdapConnectionPool> g_ldapPool;  // v2.4.3: LDAP connection pool

std::shared_ptr<repositories::SyncStatusRepository> g_syncStatusRepo;
std::shared_ptr<repositories::CertificateRepository> g_certificateRepo;
std::shared_ptr<repositories::CrlRepository> g_crlRepo;
std::shared_ptr<repositories::ReconciliationRepository> g_reconciliationRepo;

std::shared_ptr<services::SyncService> g_syncService;
std::shared_ptr<services::ReconciliationService> g_reconciliationService;

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

    // Sprint 3: Get CSCA count (excluding MLSC)
    PGresult* res = PQexec(conn.get(),
        "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA' AND (ldap_dn_v2 NOT LIKE '%o=mlsc%' OR ldap_dn_v2 IS NULL)");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        stats.cscaCount = std::stoi(PQgetvalue(res, 0, 0));
    }
    PQclear(res);

    // Sprint 3: Get MLSC count (stored as CSCA but distinguished by ldap_dn_v2)
    res = PQexec(conn.get(), "SELECT COUNT(*) FROM certificate WHERE ldap_dn_v2 LIKE '%o=mlsc%'");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        stats.mlscCount = std::stoi(PQgetvalue(res, 0, 0));
    }
    PQclear(res);

    // Get DSC and DSC_NC counts
    const char* certQuery = R"(
        SELECT certificate_type, COUNT(*) as cnt
        FROM certificate
        WHERE certificate_type IN ('DSC', 'DSC_NC')
        GROUP BY certificate_type
    )";

    res = PQexec(conn.get(), certQuery);
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            std::string type = PQgetvalue(res, i, 0);
            int count = std::stoi(PQgetvalue(res, i, 1));
            if (type == "DSC") stats.dscCount = count;
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
        "db_csca_count, db_mlsc_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count, "
        "ldap_csca_count, ldap_mlsc_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries, "
        "csca_discrepancy, mlsc_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy, "
        "db_country_stats, ldap_country_stats, status, error_message, check_duration_ms"
        ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23) RETURNING id";

    std::string dbCsca = std::to_string(result.dbStats.cscaCount);
    std::string dbMlsc = std::to_string(result.dbStats.mlscCount);
    std::string dbDsc = std::to_string(result.dbStats.dscCount);
    std::string dbDscNc = std::to_string(result.dbStats.dscNcCount);
    std::string dbCrl = std::to_string(result.dbStats.crlCount);
    std::string dbStoredInLdap = std::to_string(result.dbStats.storedInLdapCount);
    std::string ldapCsca = std::to_string(result.ldapStats.cscaCount);
    std::string ldapMlsc = std::to_string(result.ldapStats.mlscCount);
    std::string ldapDsc = std::to_string(result.ldapStats.dscCount);
    std::string ldapDscNc = std::to_string(result.ldapStats.dscNcCount);
    std::string ldapCrl = std::to_string(result.ldapStats.crlCount);
    std::string ldapTotal = std::to_string(result.ldapStats.totalEntries);
    std::string cscaDisc = std::to_string(result.cscaDiscrepancy);
    std::string mlscDisc = std::to_string(result.mlscDiscrepancy);
    std::string dscDisc = std::to_string(result.dscDiscrepancy);
    std::string dscNcDisc = std::to_string(result.dscNcDiscrepancy);
    std::string crlDisc = std::to_string(result.crlDiscrepancy);
    std::string totalDisc = std::to_string(result.totalDiscrepancy);
    std::string durationMs = std::to_string(result.checkDurationMs);

    const char* paramValues[23] = {
        dbCsca.c_str(), dbMlsc.c_str(), dbDsc.c_str(), dbDscNc.c_str(), dbCrl.c_str(), dbStoredInLdap.c_str(),
        ldapCsca.c_str(), ldapMlsc.c_str(), ldapDsc.c_str(), ldapDscNc.c_str(), ldapCrl.c_str(), ldapTotal.c_str(),
        cscaDisc.c_str(), mlscDisc.c_str(), dscDisc.c_str(), dscNcDisc.c_str(), crlDisc.c_str(), totalDisc.c_str(),
        dbCountryStr.c_str(), ldapCountryStr.c_str(),
        result.status.c_str(),
        result.errorMessage.empty() ? nullptr : result.errorMessage.c_str(),
        durationMs.c_str()
    };

    PGresult* res = PQexecParams(conn.get(), query.c_str(), 23, nullptr, paramValues, nullptr, nullptr, 0);

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
               db_csca_count, db_mlsc_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count,
               ldap_csca_count, ldap_mlsc_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries,
               csca_discrepancy, mlsc_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy,
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
        dbStats["mlsc"] = std::stoi(PQgetvalue(res, 0, 3));
        dbStats["dsc"] = std::stoi(PQgetvalue(res, 0, 4));
        dbStats["dscNc"] = std::stoi(PQgetvalue(res, 0, 5));
        dbStats["crl"] = std::stoi(PQgetvalue(res, 0, 6));
        dbStats["storedInLdap"] = std::stoi(PQgetvalue(res, 0, 7));
        result["dbStats"] = dbStats;

        Json::Value ldapStats(Json::objectValue);
        ldapStats["csca"] = std::stoi(PQgetvalue(res, 0, 8));
        ldapStats["mlsc"] = std::stoi(PQgetvalue(res, 0, 9));
        ldapStats["dsc"] = std::stoi(PQgetvalue(res, 0, 10));
        ldapStats["dscNc"] = std::stoi(PQgetvalue(res, 0, 11));
        ldapStats["crl"] = std::stoi(PQgetvalue(res, 0, 12));
        ldapStats["total"] = std::stoi(PQgetvalue(res, 0, 13));
        result["ldapStats"] = ldapStats;

        Json::Value discrepancy(Json::objectValue);
        discrepancy["csca"] = std::stoi(PQgetvalue(res, 0, 14));
        discrepancy["mlsc"] = std::stoi(PQgetvalue(res, 0, 15));
        discrepancy["dsc"] = std::stoi(PQgetvalue(res, 0, 16));
        discrepancy["dscNc"] = std::stoi(PQgetvalue(res, 0, 17));
        discrepancy["crl"] = std::stoi(PQgetvalue(res, 0, 18));
        discrepancy["total"] = std::stoi(PQgetvalue(res, 0, 19));
        result["discrepancy"] = discrepancy;

        result["status"] = PQgetvalue(res, 0, 20);
        if (!PQgetisnull(res, 0, 21)) {
            result["errorMessage"] = PQgetvalue(res, 0, 21);
        }
        result["checkDurationMs"] = std::stoi(PQgetvalue(res, 0, 22));
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

    // Parse LDAP_READ_HOSTS (comma-separated list: "openldap1:389,openldap2:389")
    std::vector<std::string> ldapHosts;
    std::string hostsStr = g_config.ldapReadHosts;
    std::istringstream ss(hostsStr);
    std::string host;
    while (std::getline(ss, host, ',')) {
        // Trim whitespace
        host.erase(0, host.find_first_not_of(" \t"));
        host.erase(host.find_last_not_of(" \t") + 1);
        if (!host.empty()) {
            ldapHosts.push_back(host);
        }
    }

    if (ldapHosts.empty()) {
        spdlog::error("No LDAP hosts configured in LDAP_READ_HOSTS");
        return stats;
    }

    // Try connecting to LDAP hosts in round-robin fashion
    LDAP* ld = nullptr;
    int rc = LDAP_SERVER_DOWN;
    std::string connectedHost;

    for (const auto& hostPort : ldapHosts) {
        std::string ldapUri = "ldap://" + hostPort;
        rc = ldap_initialize(&ld, ldapUri.c_str());
        if (rc == LDAP_SUCCESS) {
            connectedHost = hostPort;
            spdlog::debug("LDAP initialized successfully: {}", ldapUri);
            break;
        } else {
            spdlog::warn("Failed to initialize LDAP {}: {}", ldapUri, ldap_err2string(rc));
        }
    }

    if (rc != LDAP_SUCCESS || !ld) {
        spdlog::error("Failed to connect to any LDAP host");
        return stats;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    // CRITICAL: Disable referrals to prevent MMR referral bypass (fixes LDAP count instability)
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    // Authenticated bind for read access
    struct berval cred;
    cred.bv_val = const_cast<char*>(g_config.ldapBindPassword.c_str());
    cred.bv_len = g_config.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, g_config.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP bind failed on {}: {}", connectedHost, ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return stats;
    }

    // Search under data container for certificates
    LDAPMessage* result = nullptr;
    const char* attrs[] = {"dn", nullptr};
    struct timeval timeout = {60, 0};

    // v2.0.0: Use configurable LDAP DIT structure (runtime environment variable)
    std::string dataBase = g_config.ldapDataContainer + "," + g_config.ldapBaseDn;
    spdlog::info("LDAP search base DN: {}", dataBase);
    rc = ldap_search_ext_s(ld, dataBase.c_str(), LDAP_SCOPE_SUBTREE,
                           "(objectClass=pkdDownload)", const_cast<char**>(attrs), 0,
                           nullptr, nullptr, &timeout, 0, &result);

    if (rc == LDAP_SUCCESS) {
        int entryCount = result ? ldap_count_entries(ld, result) : 0;
        spdlog::info("LDAP search successful, found {} entries", entryCount);
        LDAPMessage* entry = ldap_first_entry(ld, result);
        while (entry) {
            char* dn = ldap_get_dn(ld, entry);
            if (dn) {
                std::string dnStr(dn);
                // Count by OU type in DN
                if (dnStr.find("o=csca,") != std::string::npos) {
                    stats.cscaCount++;
                } else if (dnStr.find("o=mlsc,") != std::string::npos) {
                    // Sprint 3: Count Master List Signer Certificates
                    stats.mlscCount++;
                } else if (dnStr.find("o=lc,") != std::string::npos) {
                    // Sprint 3: Count Link Certificates as CSCA
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
                        } else if (dnStr.find("o=mlsc,") != std::string::npos) {
                            stats.countryStats[country]["mlsc"]++;
                        } else if (dnStr.find("o=dsc,") != std::string::npos) {
                            stats.countryStats[country]["dsc"]++;
                        }
                    }
                }

                ldap_memfree(dn);
            }
            entry = ldap_next_entry(ld, entry);
        }
    } else {
        spdlog::error("LDAP search failed for dataBase: {}, error: {}", dataBase, ldap_err2string(rc));
    }
    if (result) ldap_msgfree(result);

    // Search under nc-data container for DSC_NC
    // v2.0.0: Use configurable LDAP DIT structure (runtime environment variable)
    std::string ncDataBase = g_config.ldapNcDataContainer + "," + g_config.ldapBaseDn;
    spdlog::info("LDAP search nc-data base DN: {}", ncDataBase);
    rc = ldap_search_ext_s(ld, ncDataBase.c_str(), LDAP_SCOPE_SUBTREE,
                           "(objectClass=pkdDownload)", const_cast<char**>(attrs), 0,
                           nullptr, nullptr, &timeout, 0, &result);

    if (rc == LDAP_SUCCESS) {
        stats.dscNcCount = ldap_count_entries(ld, result);
        spdlog::info("LDAP nc-data search successful, found {} entries", stats.dscNcCount);
    } else {
        spdlog::error("LDAP search failed for ncDataBase: {}, error: {}", ncDataBase, ldap_err2string(rc));
    }
    if (result) ldap_msgfree(result);

    stats.totalEntries = stats.cscaCount + stats.mlscCount + stats.dscCount + stats.dscNcCount + stats.crlCount;

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
    spdlog::info("DB stats - CSCA: {}, MLSC: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                 result.dbStats.cscaCount, result.dbStats.mlscCount, result.dbStats.dscCount,
                 result.dbStats.dscNcCount, result.dbStats.crlCount);

    // Get LDAP stats
    result.ldapStats = getLdapStats();
    spdlog::info("LDAP stats - CSCA: {}, MLSC: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                 result.ldapStats.cscaCount, result.ldapStats.mlscCount, result.ldapStats.dscCount,
                 result.ldapStats.dscNcCount, result.ldapStats.crlCount);

    // Calculate discrepancies
    result.cscaDiscrepancy = result.dbStats.cscaCount - result.ldapStats.cscaCount;
    result.mlscDiscrepancy = result.dbStats.mlscCount - result.ldapStats.mlscCount;
    result.dscDiscrepancy = result.dbStats.dscCount - result.ldapStats.dscCount;
    result.dscNcDiscrepancy = result.dbStats.dscNcCount - result.ldapStats.dscNcCount;
    result.crlDiscrepancy = result.dbStats.crlCount - result.ldapStats.crlCount;
    result.totalDiscrepancy = std::abs(result.cscaDiscrepancy) +
                               std::abs(result.mlscDiscrepancy) +
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
                    dailyCv_.wait_for(lock, std::chrono::seconds(waitSeconds),
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
                            SyncResult syncResult = performSyncCheck();
                            int syncStatusId = syncResult.syncStatusId;

                            // 2. Re-validate certificates if enabled
                            if (g_config.revalidateCertsOnSync) {
                                spdlog::info("[Daily] Step 2: Performing certificate re-validation...");
                                RevalidationResult revalResult = performCertificateRevalidation();
                                saveRevalidationResult(revalResult);
                            }

                            // 3. Auto reconcile if enabled and discrepancies detected
                            if (g_config.autoReconcile && syncResult.totalDiscrepancy > 0) {
                                spdlog::info("[Daily] Step 3: Auto reconcile triggered (discrepancy: {})",
                                           syncResult.totalDiscrepancy);

                                PgConnection pgConn;
                                if (pgConn.connect()) {
                                    // v2.4.3: Pass LDAP connection pool to ReconciliationEngine
                                    ReconciliationEngine engine(g_config, g_ldapPool.get());
                                    ReconciliationResult reconResult = engine.performReconciliation(
                                        pgConn.get(), false, "DAILY_SYNC", syncStatusId);

                                    if (reconResult.success) {
                                        spdlog::info("[Daily] Auto reconcile completed: {} processed, {} succeeded, {} failed",
                                                   reconResult.totalProcessed, reconResult.successCount,
                                                   reconResult.failedCount);
                                    } else {
                                        spdlog::error("[Daily] Auto reconcile failed: {}", reconResult.errorMessage);
                                    }
                                } else {
                                    spdlog::error("[Daily] Failed to connect to database for auto reconcile");
                                }
                            } else if (!g_config.autoReconcile) {
                                spdlog::debug("[Daily] Auto reconcile disabled in configuration");
                            } else {
                                spdlog::info("[Daily] No discrepancies detected, skipping auto reconcile");
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
    // Phase 4: Migrated to use SyncService
    Json::Value result = g_syncService->getCurrentStatus();
    auto resp = HttpResponse::newHttpJsonResponse(result);

    if (!result.get("success", true).asBool()) {
        resp->setStatusCode(k500InternalServerError);
    }

    callback(resp);
}

// Get sync history
void handleSyncHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    // Phase 4: Migrated to use SyncService
    int limit = 50;  // Default from service
    int offset = 0;

    if (auto l = req->getParameter("limit"); !l.empty()) {
        limit = std::stoi(l);
    }
    if (auto o = req->getParameter("offset"); !o.empty()) {
        offset = std::stoi(o);
    }

    Json::Value result = g_syncService->getSyncHistory(limit, offset);
    auto resp = HttpResponse::newHttpJsonResponse(result);

    if (!result.get("success", true).asBool()) {
        resp->setStatusCode(k500InternalServerError);
    }

    callback(resp);
}

// Trigger manual sync check
void handleSyncCheck(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        // Phase 4: Migrated to use SyncService
        spdlog::info("Starting sync check...");

        // Get DB stats (still using existing function)
        DbStats dbStats = getDbStats();
        spdlog::info("DB stats - CSCA: {}, MLSC: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                     dbStats.cscaCount, dbStats.mlscCount, dbStats.dscCount,
                     dbStats.dscNcCount, dbStats.crlCount);

        // Get LDAP stats (still using existing function)
        LdapStats ldapStats = getLdapStats();
        spdlog::info("LDAP stats - CSCA: {}, MLSC: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                     ldapStats.cscaCount, ldapStats.mlscCount, ldapStats.dscCount,
                     ldapStats.dscNcCount, ldapStats.crlCount);

        // Convert to JSON for service
        Json::Value dbCounts;
        dbCounts["csca"] = dbStats.cscaCount;
        dbCounts["mlsc"] = dbStats.mlscCount;
        dbCounts["dsc"] = dbStats.dscCount;
        dbCounts["dsc_nc"] = dbStats.dscNcCount;
        dbCounts["crl"] = dbStats.crlCount;
        dbCounts["stored_in_ldap"] = dbStats.storedInLdapCount;

        Json::Value ldapCounts;
        ldapCounts["csca"] = ldapStats.cscaCount;
        ldapCounts["mlsc"] = ldapStats.mlscCount;
        ldapCounts["dsc"] = ldapStats.dscCount;
        ldapCounts["dsc_nc"] = ldapStats.dscNcCount;
        ldapCounts["crl"] = ldapStats.crlCount;

        // Call service to perform check and save
        Json::Value result = g_syncService->performSyncCheck(dbCounts, ldapCounts);

        auto resp = HttpResponse::newHttpJsonResponse(result);
        if (!result.get("success", true).asBool()) {
            resp->setStatusCode(k500InternalServerError);
        }

        callback(resp);
    } catch (const std::exception& e) {
        Json::Value error;
        error["success"] = false;
        error["message"] = "Sync check failed";
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

// Trigger reconciliation
void handleReconcile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    // Check if auto reconcile is enabled
    if (!g_config.autoReconcile) {
        Json::Value error(Json::objectValue);
        error["success"] = false;
        error["error"] = "Auto reconcile is disabled";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    // Check for dry-run parameter
    bool dryRun = false;
    if (auto param = req->getParameter("dryRun"); !param.empty()) {
        dryRun = (param == "true" || param == "1");
    }

    try {
        // Connect to PostgreSQL
        PgConnection pgConn;
        if (!pgConn.connect()) {
            Json::Value error(Json::objectValue);
            error["success"] = false;
            error["error"] = "Database connection failed";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
            return;
        }

        // v2.4.3: Create reconciliation engine with LDAP pool and perform reconciliation
        ReconciliationEngine engine(g_config, g_ldapPool.get());
        ReconciliationResult result = engine.performReconciliation(pgConn.get(), dryRun);

        // Build JSON response
        Json::Value response(Json::objectValue);
        response["success"] = result.success;
        response["message"] = dryRun ? "Dry-run reconciliation completed" : "Reconciliation completed";
        response["dryRun"] = dryRun;

        Json::Value summary(Json::objectValue);
        summary["totalProcessed"] = result.totalProcessed;
        summary["cscaAdded"] = result.cscaAdded;
        summary["cscaDeleted"] = result.cscaDeleted;
        summary["dscAdded"] = result.dscAdded;
        summary["dscDeleted"] = result.dscDeleted;
        summary["dscNcAdded"] = result.dscNcAdded;
        summary["dscNcDeleted"] = result.dscNcDeleted;
        summary["crlAdded"] = result.crlAdded;
        summary["crlDeleted"] = result.crlDeleted;
        summary["successCount"] = result.successCount;
        summary["failedCount"] = result.failedCount;
        summary["durationMs"] = result.durationMs;
        summary["status"] = result.status;
        response["summary"] = summary;

        if (!result.failures.empty()) {
            Json::Value failures(Json::arrayValue);
            for (const auto& failure : result.failures) {
                Json::Value f(Json::objectValue);
                f["certType"] = failure.certType;
                f["operation"] = failure.operation;
                f["countryCode"] = failure.countryCode;
                f["subject"] = failure.subject;
                f["error"] = failure.error;
                failures.append(f);
            }
            response["failures"] = failures;
        }

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        Json::Value error(Json::objectValue);
        error["success"] = false;
        error["error"] = std::string("Reconciliation failed: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

// Get reconciliation history
void handleReconciliationHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        // Phase 4: Migrated to use ReconciliationService
        // Parse query parameters
        int limit = 50;
        int offset = 0;

        if (auto param = req->getParameter("limit"); !param.empty()) {
            limit = std::stoi(param);
            if (limit < 1 || limit > 100) limit = 50;
        }
        if (auto param = req->getParameter("offset"); !param.empty()) {
            offset = std::stoi(param);
        }

        // Note: status and triggeredBy filters not yet implemented in Service
        // TODO: Add filter support to ReconciliationService if needed

        // Call service to get reconciliation history
        Json::Value result = g_reconciliationService->getReconciliationHistory(limit, offset);

        auto resp = HttpResponse::newHttpJsonResponse(result);
        if (!result.get("success", true).asBool()) {
            resp->setStatusCode(k500InternalServerError);
        }

        callback(resp);

    } catch (const std::exception& e) {
        Json::Value error(Json::objectValue);
        error["success"] = false;
        error["error"] = std::string("Failed to get reconciliation history: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

// Get reconciliation details by ID
void handleReconciliationDetails(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        // Phase 4: Migrated to use ReconciliationService
        auto reconciliationIdStr = req->getParameter("id");
        if (reconciliationIdStr.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Missing reconciliation ID";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        int reconciliationId = std::stoi(reconciliationIdStr);

        // Call service to get details
        Json::Value result = g_reconciliationService->getReconciliationDetails(reconciliationId);

        auto resp = HttpResponse::newHttpJsonResponse(result);
        if (!result.get("success", true).asBool()) {
            resp->setStatusCode(k500InternalServerError);
        }

        callback(resp);

    } catch (const std::exception& e) {
        Json::Value error;
        error["success"] = false;
        error["message"] = "Failed to get reconciliation details";
        error["error"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
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
// Repository Pattern - Service Initialization
// =============================================================================
void initializeServices() {
    spdlog::info("Initializing Repository Pattern services...");

    try {
        // Build database connection string
        std::string conninfo = "host=" + g_config.dbHost +
                              " port=" + std::to_string(g_config.dbPort) +
                              " dbname=" + g_config.dbName +
                              " user=" + g_config.dbUser +
                              " password=" + g_config.dbPassword;

        // Initialize Database Connection Pool
        spdlog::info("Creating database connection pool (min=5, max=20)...");
        g_dbPool = std::make_shared<common::DbConnectionPool>(conninfo, 5, 20);
        spdlog::info("✅ Database connection pool initialized");

        // v2.4.3: Initialize LDAP Connection Pool
        spdlog::info("Creating LDAP connection pool (min=2, max=10)...");
        std::string ldapUri = "ldap://" + g_config.ldapWriteHost + ":" +
                             std::to_string(g_config.ldapWritePort);
        g_ldapPool = std::make_shared<common::LdapConnectionPool>(
            ldapUri,
            g_config.ldapBindDn,
            g_config.ldapBindPassword,
            2,   // min connections
            10,  // max connections
            5    // timeout seconds
        );
        spdlog::info("✅ LDAP connection pool initialized ({})", ldapUri);

        // Initialize Repositories with shared connection pool
        spdlog::info("Creating repository instances with connection pool...");
        g_syncStatusRepo = std::make_shared<repositories::SyncStatusRepository>(g_dbPool);
        g_certificateRepo = std::make_shared<repositories::CertificateRepository>(g_dbPool);
        g_crlRepo = std::make_shared<repositories::CrlRepository>(g_dbPool);
        g_reconciliationRepo = std::make_shared<repositories::ReconciliationRepository>(g_dbPool);

        // Initialize Services with dependency injection
        spdlog::info("Creating service instances with repository dependencies...");
        g_syncService = std::make_shared<services::SyncService>(
            g_syncStatusRepo,
            g_certificateRepo,
            g_crlRepo
        );

        g_reconciliationService = std::make_shared<services::ReconciliationService>(
            g_reconciliationRepo,
            g_certificateRepo,
            g_crlRepo
        );

        spdlog::info("✅ Repository Pattern services initialized successfully");

    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize services: {}", e.what());
        throw;  // Re-throw to stop application startup
    }
}

void shutdownServices() {
    spdlog::info("Shutting down Repository Pattern services...");

    // Services will be automatically cleaned up by shared_ptr destructors
    g_reconciliationService.reset();
    g_syncService.reset();

    // Repositories will be automatically cleaned up by shared_ptr destructors
    g_reconciliationRepo.reset();
    g_crlRepo.reset();
    g_certificateRepo.reset();
    g_syncStatusRepo.reset();

    // Database connection pool will be automatically cleaned up
    g_dbPool.reset();

    // v2.4.3: LDAP connection pool will be automatically cleaned up
    g_ldapPool.reset();

    spdlog::info("✅ Repository Pattern services shut down successfully");
}

// =============================================================================
// Main
// =============================================================================
int main() {
    // Load configuration from environment
    g_config.loadFromEnv();

    // Setup logging
    setupLogging();

    // Validate required credentials
    try {
        g_config.validateRequiredCredentials();
    } catch (const std::exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    }

    spdlog::info("=================================================");
    spdlog::info("  ICAO Local PKD - PKD Relay Service v2.1.0");
    spdlog::info("=================================================");
    spdlog::info("Server port: {}", g_config.serverPort);
    spdlog::info("Database: {}:{}/{}", g_config.dbHost, g_config.dbPort, g_config.dbName);
    spdlog::info("LDAP (read): {} (Software Load Balancing)", g_config.ldapReadHosts);
    spdlog::info("LDAP (write): {}:{}", g_config.ldapWriteHost, g_config.ldapWritePort);

    // Load user-configurable settings from database
    spdlog::info("Loading configuration from database...");
    g_config.loadFromDatabase();

    spdlog::info("Daily sync: {} at {}", g_config.dailySyncEnabled ? "enabled" : "disabled",
                 formatScheduledTime(g_config.dailySyncHour, g_config.dailySyncMinute));
    spdlog::info("Certificate re-validation on sync: {}", g_config.revalidateCertsOnSync ? "enabled" : "disabled");
    spdlog::info("Auto reconcile: {}", g_config.autoReconcile ? "enabled" : "disabled");

    // Initialize Repository Pattern services
    try {
        initializeServices();
    } catch (const std::exception& e) {
        spdlog::critical("Service initialization failed: {}", e.what());
        return 1;
    }

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
    app().registerHandler("/api/sync/reconcile/history",
        &handleReconciliationHistory, {Get});
    app().registerHandler("/api/sync/reconcile/{id}",
        &handleReconciliationDetails, {Get});
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
  title: PKD Relay Service API
  description: |
    Data Relay Layer for ICAO Local PKD System.
    Handles ICAO portal monitoring, LDIF upload/parsing, and DB-LDAP synchronization.

    ## Changelog
    - v2.1.0 (2026-01-26): MLSC (Master List Signer Certificate) sync support
    - v2.0.5 (2026-01-25): CRL reconciliation support
    - v2.0.0 (2026-01-20): Service reorganization - data relay layer separation
    - v1.4.0 (2026-01-14): Modularized code, Auto Reconcile implementation
    - v1.3.0 (2026-01-13): User-configurable settings UI
    - v1.2.0 (2026-01-07): Daily scheduler only
    - v1.1.0 (2026-01-06): Daily scheduler, certificate re-validation
    - v1.0.0 (2026-01-03): Initial release
  version: 2.1.0
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
    shutdownServices();

    return 0;
}

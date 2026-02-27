/**
 * @file relay_operations.cpp
 * @brief Core relay operations extracted from main.cpp
 *
 * Contains:
 * - getDbStats(): Database certificate/CRL statistics
 * - getLdapStats(): LDAP directory statistics
 * - saveSyncStatus(): Persist sync check result
 * - performSyncCheck(): Full sync check orchestration
 * - getRevalidationHistory(): Revalidation history query
 * - Config::loadFromDatabase(): Load config from sync_config table
 * - formatScheduledTime(): Time formatting helper
 */

#include "relay_operations.h"
#include "relay/sync/common/config.h"
#include "query_helpers.h"

#include <spdlog/spdlog.h>
#include <ldap.h>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "../repositories/sync_status_repository.h"
#include "../domain/models/sync_status.h"

// Global QueryExecutor pointer for Config::loadFromDatabase() compatibility
// Set by main.cpp after ServiceContainer initialization
extern common::IQueryExecutor* g_queryExecutor;

namespace infrastructure {

// --- Database Statistics ---
icao::relay::DbStats getDbStats(common::IQueryExecutor* executor) {
    icao::relay::DbStats stats;

    if (!executor) {
        spdlog::error("Query executor not initialized for DB stats");
        return stats;
    }

    try {
        std::string dbType = executor->getDatabaseType();

        // Helper lambda for scalar count queries
        auto scalarCount = [&](const std::string& query,
                               const std::vector<std::string>& params = {}) -> int {
            Json::Value result = executor->executeScalar(query, params);
            if (result.isInt()) return result.asInt();
            if (result.isString()) {
                try { return std::stoi(result.asString()); } catch (...) { return 0; }
            }
            return 0;
        };

        // Get CSCA count
        stats.cscaCount = scalarCount(
            "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA'");

        // Get MLSC count
        stats.mlscCount = scalarCount(
            "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'MLSC'");

        // Get DSC and DSC_NC counts
        std::string certQuery =
            "SELECT certificate_type, COUNT(*) as cnt "
            "FROM certificate "
            "WHERE certificate_type IN ('DSC', 'DSC_NC') "
            "GROUP BY certificate_type";

        Json::Value certResult = executor->executeQuery(certQuery, {});
        for (const auto& row : certResult) {
            std::string type = row["certificate_type"].asString();
            int count = 0;
            const auto& v = row["cnt"];
            if (v.isInt()) count = v.asInt();
            else if (v.isString()) { try { count = std::stoi(v.asString()); } catch (...) {} }

            if (type == "DSC") stats.dscCount = count;
            else if (type == "DSC_NC") stats.dscNcCount = count;
        }

        // Get CRL count
        stats.crlCount = scalarCount("SELECT COUNT(*) FROM crl");

        // Get stored_in_ldap count - Oracle uses NUMBER(1) with 1/0
        std::string storedQuery = (dbType == "oracle")
            ? "SELECT COUNT(*) FROM certificate WHERE stored_in_ldap = 1"
            : "SELECT COUNT(*) FROM certificate WHERE stored_in_ldap = TRUE";
        stats.storedInLdapCount = scalarCount(storedQuery);

        // Get country breakdown
        std::string countryQuery =
            "SELECT country_code, certificate_type, COUNT(*) as cnt "
            "FROM certificate "
            "GROUP BY country_code, certificate_type "
            "ORDER BY country_code";

        Json::Value countryResult = executor->executeQuery(countryQuery, {});
        for (const auto& row : countryResult) {
            std::string country = row["country_code"].asString();
            std::string type = row["certificate_type"].asString();
            int count = 0;
            const auto& v = row["cnt"];
            if (v.isInt()) count = v.asInt();
            else if (v.isString()) { try { count = std::stoi(v.asString()); } catch (...) {} }

            if (type == "CSCA") stats.countryStats[country]["csca"] = count;
            else if (type == "DSC") stats.countryStats[country]["dsc"] = count;
            else if (type == "DSC_NC") stats.countryStats[country]["dsc_nc"] = count;
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get DB stats: {}", e.what());
    }

    return stats;
}

// --- LDAP Statistics ---
icao::relay::LdapStats getLdapStats(common::LdapConnectionPool* ldapPool,
                                     const icao::relay::Config& config) {
    icao::relay::LdapStats stats;

    auto conn = ldapPool->acquire();
    if (!conn.isValid()) {
        spdlog::error("Failed to acquire LDAP connection from pool");
        return stats;
    }

    LDAP* ld = conn.get();
    spdlog::debug("Acquired LDAP connection from pool for statistics gathering");

    int rc;
    LDAPMessage* result = nullptr;
    const char* attrs[] = {"dn", nullptr};
    struct timeval timeout = {60, 0};

    // Search under data container for certificates
    std::string dataBase = config.ldapDataContainer + "," + config.ldapBaseDn;
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
                    stats.mlscCount++;
                } else if (dnStr.find("o=lc,") != std::string::npos) {
                    // Count Link Certificates as CSCA
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
    std::string ncDataBase = config.ldapNcDataContainer + "," + config.ldapBaseDn;
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

    stats.totalEntries = stats.cscaCount + stats.mlscCount + stats.dscCount +
                         stats.dscNcCount + stats.crlCount;

    // Connection automatically released when 'conn' goes out of scope (RAII)
    return stats;
}

// --- Save Sync Status ---
int saveSyncStatus(const icao::relay::SyncResult& result,
                   icao::relay::repositories::SyncStatusRepository* syncStatusRepo) {
    // Convert country stats to JSON
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

    // Create domain::SyncStatus object
    icao::relay::domain::SyncStatus syncStatus(
        "",  // ID will be generated by repository
        std::chrono::system_clock::now(),
        result.dbStats.cscaCount,
        result.ldapStats.cscaCount,
        result.cscaDiscrepancy,
        result.dbStats.mlscCount,
        result.ldapStats.mlscCount,
        result.mlscDiscrepancy,
        result.dbStats.dscCount,
        result.ldapStats.dscCount,
        result.dscDiscrepancy,
        result.dbStats.dscNcCount,
        result.ldapStats.dscNcCount,
        result.dscNcDiscrepancy,
        result.dbStats.crlCount,
        result.ldapStats.crlCount,
        result.crlDiscrepancy,
        result.totalDiscrepancy,
        result.dbStats.storedInLdapCount,
        result.ldapStats.totalEntries,
        dbCountryJson,
        ldapCountryJson,
        result.status,
        result.errorMessage.empty() ? std::nullopt : std::optional<std::string>(result.errorMessage),
        result.checkDurationMs
    );

    if (!syncStatusRepo->create(syncStatus)) {
        spdlog::error("Failed to save sync status using Repository");
        return -1;
    }

    // Extract generated ID
    std::string idStr = syncStatus.getId();
    int syncId = -1;

    try {
        syncId = std::stoi(idStr);
    } catch (...) {
        // UUID case
        spdlog::info("Saved sync status with UUID: {}", idStr);
        return 0;
    }

    spdlog::info("Saved sync status with id: {}", syncId);
    return syncId;
}

// --- Perform Sync Check ---
icao::relay::SyncResult performSyncCheck(common::IQueryExecutor* executor,
                                          common::LdapConnectionPool* ldapPool,
                                          const icao::relay::Config& config,
                                          icao::relay::repositories::SyncStatusRepository* syncStatusRepo) {
    icao::relay::SyncResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    spdlog::info("Starting sync check...");

    // Get DB stats
    result.dbStats = getDbStats(executor);
    spdlog::info("DB stats - CSCA: {}, MLSC: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                 result.dbStats.cscaCount, result.dbStats.mlscCount, result.dbStats.dscCount,
                 result.dbStats.dscNcCount, result.dbStats.crlCount);

    // Get LDAP stats
    result.ldapStats = getLdapStats(ldapPool, config);
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
    result.syncStatusId = saveSyncStatus(result, syncStatusRepo);

    return result;
}

// --- Revalidation History ---
Json::Value getRevalidationHistory(common::IQueryExecutor* executor, int limit) {
    Json::Value result(Json::arrayValue);

    if (!executor) {
        spdlog::error("Query executor not available for revalidation history");
        return result;
    }

    try {
        std::string dbType = executor->getDatabaseType();
        std::string query = "SELECT id, executed_at, total_processed, newly_expired, newly_valid, "
            "unchanged, errors, duration_ms FROM revalidation_history "
            "ORDER BY executed_at DESC " +
            common::db::limitClause(dbType, limit);

        Json::Value rows = executor->executeQuery(query);

        // Helper: parse int from various DB formats
        auto getInt = [](const Json::Value& v, int def = 0) -> int {
            if (v.isInt()) return v.asInt();
            if (v.isString()) { try { return std::stoi(v.asString()); } catch (...) { return def; } }
            return def;
        };

        for (const auto& row : rows) {
            Json::Value item(Json::objectValue);
            item["id"] = getInt(row["id"]);
            item["executedAt"] = row["executed_at"].asString();
            item["totalProcessed"] = getInt(row["total_processed"]);
            item["newlyExpired"] = getInt(row["newly_expired"]);
            item["newlyValid"] = getInt(row["newly_valid"]);
            item["unchanged"] = getInt(row["unchanged"]);
            item["errors"] = getInt(row["errors"]);
            item["durationMs"] = getInt(row["duration_ms"]);
            result.append(item);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get revalidation history: {}", e.what());
    }

    return result;
}

// --- Format Scheduled Time ---
std::string formatScheduledTime(int targetHour, int targetMinute) {
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << targetHour << ":"
       << std::setfill('0') << std::setw(2) << targetMinute;
    return ss.str();
}

} // namespace infrastructure

// --- Config::loadFromDatabase() ---
// Uses the global g_queryExecutor pointer set by main.cpp
bool icao::relay::Config::loadFromDatabase() {
    if (!g_queryExecutor) {
        spdlog::warn("Query executor not available for loading config from database");
        return false;
    }

    try {
        const char* query = "SELECT daily_sync_enabled, daily_sync_hour, daily_sync_minute, "
                           "auto_reconcile, revalidate_certs_on_sync, max_reconcile_batch_size "
                           "FROM sync_config WHERE id = 1";

        Json::Value result = g_queryExecutor->executeQuery(query, {});
        if (result.empty()) {
            spdlog::warn("No configuration found in database, using defaults");
            return false;
        }

        const auto& row = result[0];

        // Helper: parse boolean from various DB formats
        auto parseBool = [&](const std::string& field) -> bool {
            const auto& v = row[field];
            if (v.isBool()) return v.asBool();
            if (v.isString()) {
                std::string s = v.asString();
                return (s == "t" || s == "true" || s == "TRUE" || s == "1");
            }
            if (v.isInt()) return v.asInt() != 0;
            return false;
        };

        // Helper: parse int from various DB formats
        auto parseInt = [&](const std::string& field, int def = 0) -> int {
            const auto& v = row[field];
            if (v.isInt()) return v.asInt();
            if (v.isString()) { try { return std::stoi(v.asString()); } catch (...) { return def; } }
            return def;
        };

        dailySyncEnabled = parseBool("daily_sync_enabled");
        dailySyncHour = parseInt("daily_sync_hour", 0);
        dailySyncMinute = parseInt("daily_sync_minute", 0);
        autoReconcile = parseBool("auto_reconcile");
        revalidateCertsOnSync = parseBool("revalidate_certs_on_sync");
        maxReconcileBatchSize = parseInt("max_reconcile_batch_size", 100);

        spdlog::info("Loaded configuration from database (via QueryExecutor)");
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load config via QueryExecutor: {}", e.what());
        return false;
    }
}

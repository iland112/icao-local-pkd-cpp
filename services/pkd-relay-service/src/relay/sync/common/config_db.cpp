/**
 * @file config_db.cpp
 * @brief Config::loadFromDatabase() implementation
 */
#include "config.h"
#include "i_query_executor.h"
#include <spdlog/spdlog.h>
#include <json/json.h>

extern common::IQueryExecutor* g_queryExecutor;

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

        spdlog::info("Loaded configuration from database");
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load config from database: {}", e.what());
        return false;
    }
}

/**
 * @file sync_handler.cpp
 * @brief Sync handler implementation - status, history, check, config, revalidation
 *
 * Extracted from main.cpp HTTP handler functions.
 */

#include "sync_handler.h"
#include "../infrastructure/relay_operations.h"
#include "../infrastructure/sync_scheduler.h"
#include "relay/sync/common/config.h"
#include "../services/sync_service.h"
#include "../services/validation_service.h"
#include "query_helpers.h"

#include <icao/audit/audit_log.h>
#include "handler_utils.h"
#include <spdlog/spdlog.h>
#include <numeric>
#include <algorithm>

using namespace drogon;

namespace handlers {

SyncHandler::SyncHandler(icao::relay::services::SyncService* syncService,
                         icao::relay::services::ValidationService* validationService,
                         common::IQueryExecutor* queryExecutor,
                         common::LdapConnectionPool* ldapPool,
                         icao::relay::Config& config,
                         infrastructure::SyncScheduler& scheduler)
    : syncService_(syncService)
    , validationService_(validationService)
    , queryExecutor_(queryExecutor)
    , ldapPool_(ldapPool)
    , config_(config)
    , scheduler_(scheduler) {}

void SyncHandler::handleSyncStatus(const HttpRequestPtr&,
                                    std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value result = syncService_->getCurrentStatus();
    auto resp = HttpResponse::newHttpJsonResponse(result);

    if (!result.get("success", true).asBool()) {
        resp->setStatusCode(k500InternalServerError);
    }

    callback(resp);
}

void SyncHandler::handleSyncHistory(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) {
    int limit = 50;
    int offset = 0;

    if (auto l = req->getParameter("limit"); !l.empty()) {
        limit = std::stoi(l);
    }
    if (auto o = req->getParameter("offset"); !o.empty()) {
        offset = std::stoi(o);
    }

    Json::Value result = syncService_->getSyncHistory(limit, offset);
    auto resp = HttpResponse::newHttpJsonResponse(result);

    if (!result.get("success", true).asBool()) {
        resp->setStatusCode(k500InternalServerError);
    }

    callback(resp);
}

void SyncHandler::handleSyncCheck(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        spdlog::info("Starting sync check...");

        // Get DB stats
        icao::relay::DbStats dbStats = infrastructure::getDbStats(queryExecutor_);
        spdlog::info("DB stats - CSCA: {}, MLSC: {}, DSC: {}, DSC_NC: {}, CRL: {}",
                     dbStats.cscaCount, dbStats.mlscCount, dbStats.dscCount,
                     dbStats.dscNcCount, dbStats.crlCount);

        // Get LDAP stats
        icao::relay::LdapStats ldapStats = infrastructure::getLdapStats(ldapPool_, config_);
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
        Json::Value result = syncService_->performSyncCheck(dbCounts, ldapCounts);

        auto resp = HttpResponse::newHttpJsonResponse(result);
        if (!result.get("success", true).asBool()) {
            resp->setStatusCode(k500InternalServerError);
        }

        callback(resp);

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::SYNC_CHECK);
        auditEntry.success = result.get("success", true).asBool();
        auditEntry.resourceType = "SYNC";
        Json::Value auditMeta;
        auditMeta["dbTotal"] = dbStats.cscaCount + dbStats.mlscCount + dbStats.dscCount + dbStats.dscNcCount + dbStats.crlCount;
        auditMeta["ldapTotal"] = ldapStats.cscaCount + ldapStats.mlscCount + ldapStats.dscCount + ldapStats.dscNcCount + ldapStats.crlCount;
        auditEntry.metadata = auditMeta;
        icao::audit::logOperation(queryExecutor_, auditEntry);

    } catch (const std::exception& e) {
        // Audit log (failure)
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::SYNC_CHECK);
        auditEntry.success = false;
        auditEntry.resourceType = "SYNC";
        auditEntry.errorMessage = e.what();
        icao::audit::logOperation(queryExecutor_, auditEntry);

        callback(common::handler::internalError("SyncHandler::syncCheck", e));
    }
}

void SyncHandler::handleDiscrepancies(const HttpRequestPtr&,
                                       std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string boolFalse = (dbType == "oracle") ? "0" : "FALSE";

        std::string query =
            "SELECT id, detected_at, item_type, certificate_type, country_code, fingerprint, "
            "issue_type, db_exists, ldap_exists "
            "FROM sync_discrepancy "
            "WHERE resolved = " + boolFalse + " "
            "ORDER BY detected_at DESC " +
            common::db::limitClause(dbType, 100);

        Json::Value rows = queryExecutor_->executeQuery(query, {});

        Json::Value result(Json::arrayValue);
        for (const auto& row : rows) {
            Json::Value item(Json::objectValue);
            item["id"] = row["id"].asString();
            item["detectedAt"] = row["detected_at"].asString();
            item["itemType"] = row["item_type"].asString();
            if (!row["certificate_type"].isNull()) item["certificateType"] = row["certificate_type"].asString();
            if (!row["country_code"].isNull()) item["countryCode"] = row["country_code"].asString();
            if (!row["fingerprint"].isNull()) item["fingerprint"] = row["fingerprint"].asString();
            item["issueType"] = row["issue_type"].asString();

            auto parseBool = [](const Json::Value& v) -> bool {
                if (v.isBool()) return v.asBool();
                if (v.isString()) { std::string s = v.asString(); return (s == "t" || s == "true" || s == "1"); }
                if (v.isInt()) return v.asInt() != 0;
                return false;
            };
            item["dbExists"] = parseBool(row["db_exists"]);
            item["ldapExists"] = parseBool(row["ldap_exists"]);
            result.append(item);
        }

        auto resp = HttpResponse::newHttpJsonResponse(result);
        callback(resp);
    } catch (const std::exception& e) {
        callback(common::handler::internalError("SyncHandler::discrepancies", e));
    }
}

void SyncHandler::handleSyncConfig(const HttpRequestPtr&,
                                    std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value config(Json::objectValue);
    config["autoReconcile"] = config_.autoReconcile;
    config["maxReconcileBatchSize"] = config_.maxReconcileBatchSize;
    config["dailySyncEnabled"] = config_.dailySyncEnabled;
    config["dailySyncHour"] = config_.dailySyncHour;
    config["dailySyncMinute"] = config_.dailySyncMinute;
    config["dailySyncTime"] = infrastructure::formatScheduledTime(config_.dailySyncHour, config_.dailySyncMinute);
    config["revalidateCertsOnSync"] = config_.revalidateCertsOnSync;

    auto resp = HttpResponse::newHttpJsonResponse(config);
    callback(resp);
}

void SyncHandler::handleUpdateSyncConfig(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback) {
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

        std::string dbType = queryExecutor_->getDatabaseType();

        // Helper: format boolean for database
        auto boolStr = [&](bool val) -> std::string {
            if (dbType == "oracle") return val ? "1" : "0";
            return val ? "TRUE" : "FALSE";
        };

        // Build UPDATE query dynamically
        std::vector<std::string> setClauses;
        std::vector<std::string> paramValues;
        int paramIndex = 1;

        if (json.isMember("dailySyncEnabled")) {
            setClauses.push_back("daily_sync_enabled = $" + std::to_string(paramIndex++));
            paramValues.push_back(boolStr(json["dailySyncEnabled"].asBool()));
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
            paramValues.push_back(boolStr(json["autoReconcile"].asBool()));
        }
        if (json.isMember("revalidateCertsOnSync")) {
            setClauses.push_back("revalidate_certs_on_sync = $" + std::to_string(paramIndex++));
            paramValues.push_back(boolStr(json["revalidateCertsOnSync"].asBool()));
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
        std::string tsFunc = common::db::currentTimestamp(dbType);
        setClauses.push_back("updated_at = " + tsFunc);

        std::string query = "UPDATE sync_config SET " +
                           std::accumulate(setClauses.begin(), setClauses.end(), std::string(),
                                         [](const std::string& a, const std::string& b) {
                                             return a.empty() ? b : a + ", " + b;
                                         }) +
                           " WHERE id = 1";

        int rowsAffected = queryExecutor_->executeCommand(query, paramValues);
        if (rowsAffected == 0 && dbType == "postgres") {
            Json::Value error(Json::objectValue);
            error["success"] = false;
            error["error"] = "Failed to update configuration";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
            return;
        }

        // Reload configuration from database
        config_.loadFromDatabase();

        // Restart scheduler with new settings
        spdlog::info("Configuration updated, restarting scheduler...");
        scheduler_.stop();
        scheduler_.configure(config_.dailySyncEnabled, config_.dailySyncHour,
                            config_.dailySyncMinute, config_.revalidateCertsOnSync,
                            config_.autoReconcile);
        scheduler_.start();

        Json::Value response(Json::objectValue);
        response["success"] = true;
        response["message"] = "Configuration updated successfully";
        response["config"]["autoReconcile"] = config_.autoReconcile;
        response["config"]["maxReconcileBatchSize"] = config_.maxReconcileBatchSize;
        response["config"]["dailySyncEnabled"] = config_.dailySyncEnabled;
        response["config"]["dailySyncHour"] = config_.dailySyncHour;
        response["config"]["dailySyncMinute"] = config_.dailySyncMinute;
        response["config"]["dailySyncTime"] = infrastructure::formatScheduledTime(
            config_.dailySyncHour, config_.dailySyncMinute);
        response["config"]["revalidateCertsOnSync"] = config_.revalidateCertsOnSync;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::CONFIG_UPDATE);
        auditEntry.success = true;
        auditEntry.resourceType = "SYNC_CONFIG";
        auditEntry.metadata = json;
        icao::audit::logOperation(queryExecutor_, auditEntry);

    } catch (const std::exception& e) {
        // Audit log (failure)
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::CONFIG_UPDATE);
        auditEntry.success = false;
        auditEntry.resourceType = "SYNC_CONFIG";
        auditEntry.errorMessage = e.what();
        icao::audit::logOperation(queryExecutor_, auditEntry);

        callback(common::handler::internalError("SyncHandler::updateSyncConfig", e));
    }
}

void SyncHandler::handleRevalidate(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        spdlog::info("Manual certificate re-validation triggered via API");

        Json::Value response = validationService_->revalidateAll();

        auto resp = HttpResponse::newHttpJsonResponse(response);
        if (!response.get("success", false).asBool()) {
            resp->setStatusCode(k500InternalServerError);
        }

        callback(resp);

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::REVALIDATE);
        auditEntry.success = response.get("success", false).asBool();
        auditEntry.resourceType = "CERTIFICATE";
        if (response.isMember("totalProcessed")) {
            Json::Value auditMeta;
            auditMeta["totalProcessed"] = response["totalProcessed"];
            auditEntry.metadata = auditMeta;
        }
        icao::audit::logOperation(queryExecutor_, auditEntry);

    } catch (const std::exception& e) {
        // Audit log (failure)
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::REVALIDATE);
        auditEntry.success = false;
        auditEntry.resourceType = "CERTIFICATE";
        auditEntry.errorMessage = e.what();
        icao::audit::logOperation(queryExecutor_, auditEntry);

        callback(common::handler::internalError("SyncHandler::revalidate", e));
    }
}

void SyncHandler::handleRevalidationHistory(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback) {
    int limit = 10;
    if (auto l = req->getParameter("limit"); !l.empty()) {
        limit = std::stoi(l);
    }

    Json::Value result = infrastructure::getRevalidationHistory(queryExecutor_, limit);
    auto resp = HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void SyncHandler::handleTriggerDailySync(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback) {
    spdlog::info("Manual daily sync triggered via API");
    scheduler_.triggerDailySync();

    Json::Value response(Json::objectValue);
    response["success"] = true;
    response["message"] = "Daily sync triggered";

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);

    // Audit log
    auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::TRIGGER_DAILY_SYNC);
    auditEntry.success = true;
    auditEntry.resourceType = "SYNC";
    icao::audit::logOperation(queryExecutor_, auditEntry);
}

void SyncHandler::handleSyncStats(const HttpRequestPtr&,
                                   std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value result = syncService_->getSyncStatistics();
    auto resp = HttpResponse::newHttpJsonResponse(result);

    if (!result.get("success", true).asBool()) {
        resp->setStatusCode(k500InternalServerError);
    }

    callback(resp);
}

} // namespace handlers

/**
 * @file icao_ldap_handler.cpp
 * @brief REST API handler implementation for ICAO PKD LDAP sync
 */
#include "icao_ldap_handler.h"
#include "../relay/icao-ldap/icao_ldap_sync_service.h"
#include <spdlog/spdlog.h>
#include <thread>

namespace icao {
namespace relay {

IcaoLdapHandler::IcaoLdapHandler(IcaoLdapSyncService* syncService)
    : syncService_(syncService) {}

void IcaoLdapHandler::registerRoutes(drogon::HttpAppFramework& app) {
    app.registerHandler(
        "/api/sync/icao-ldap/trigger",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleTriggerSync(req, std::move(cb)); },
        {drogon::Post});

    app.registerHandler(
        "/api/sync/icao-ldap/status",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleGetStatus(req, std::move(cb)); },
        {drogon::Get});

    app.registerHandler(
        "/api/sync/icao-ldap/history",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleGetHistory(req, std::move(cb)); },
        {drogon::Get});

    app.registerHandler(
        "/api/sync/icao-ldap/config",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleGetConfig(req, std::move(cb)); },
        {drogon::Get});

    app.registerHandler(
        "/api/sync/icao-ldap/config",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleUpdateConfig(req, std::move(cb)); },
        {drogon::Put});

    app.registerHandler(
        "/api/sync/icao-ldap/test",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleTestConnection(req, std::move(cb)); },
        {drogon::Post});
}

void IcaoLdapHandler::handleTriggerSync(const drogon::HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    if (!syncService_) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(
            Json::Value("ICAO LDAP sync not configured"));
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        callback(resp);
        return;
    }

    if (syncService_->isSyncRunning()) {
        Json::Value json;
        json["status"] = "BUSY";
        json["message"] = "Sync already in progress";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k409Conflict);
        callback(resp);
        return;
    }

    // Run sync in background thread
    auto svc = syncService_;
    std::thread([svc]() {
        svc->performFullSync("MANUAL");
    }).detach();

    Json::Value json;
    json["status"] = "STARTED";
    json["message"] = "ICAO PKD LDAP sync triggered";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

void IcaoLdapHandler::handleGetStatus(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value json;

    if (!syncService_) {
        json["enabled"] = false;
        json["message"] = "ICAO LDAP sync not configured";
    } else {
        auto config = syncService_->getConfig();
        auto lastResult = syncService_->getLastSyncResult();

        json["enabled"] = config.enabled;
        json["running"] = syncService_->isSyncRunning();
        json["host"] = config.host;
        json["port"] = config.port;
        json["syncIntervalMinutes"] = config.syncIntervalMinutes;

        if (!lastResult.status.empty()) {
            Json::Value last;
            last["status"] = lastResult.status;
            last["syncType"] = lastResult.syncType;
            last["triggeredBy"] = lastResult.triggeredBy;
            last["totalRemoteCount"] = lastResult.totalRemoteCount;
            last["newCertificates"] = lastResult.newCertificates;
            last["existingSkipped"] = lastResult.existingSkipped;
            last["failedCount"] = lastResult.failedCount;
            last["durationMs"] = lastResult.durationMs;
            if (!lastResult.errorMessage.empty()) {
                last["errorMessage"] = lastResult.errorMessage;
            }
            json["lastSync"] = last;
        }
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

void IcaoLdapHandler::handleGetHistory(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value json(Json::arrayValue);

    if (syncService_) {
        int limit = 10, offset = 0;
        std::string statusFilter;
        auto limitParam = req->getParameter("limit");
        auto offsetParam = req->getParameter("offset");
        auto statusParam = req->getParameter("status");
        if (!limitParam.empty()) { try { limit = std::stoi(limitParam); } catch (...) {} }
        if (!offsetParam.empty()) { try { offset = std::stoi(offsetParam); } catch (...) {} }
        if (!statusParam.empty()) statusFilter = statusParam;
        limit = std::max(1, std::min(100, limit));
        offset = std::max(0, offset);

        auto history = syncService_->getSyncHistory(limit, offset, statusFilter);
        for (const auto& r : history) {
            Json::Value item;
            item["syncType"] = r.syncType;
            item["status"] = r.status;
            item["triggeredBy"] = r.triggeredBy;
            item["totalRemoteCount"] = r.totalRemoteCount;
            item["newCertificates"] = r.newCertificates;
            item["existingSkipped"] = r.existingSkipped;
            item["failedCount"] = r.failedCount;
            item["durationMs"] = r.durationMs;
            if (!r.createdAt.empty()) item["createdAt"] = r.createdAt;
            if (!r.errorMessage.empty() && r.errorMessage != " ") item["errorMessage"] = r.errorMessage;
            if (!r.typeStats.empty()) {
                Json::Value stats(Json::arrayValue);
                for (const auto& ts : r.typeStats) {
                    Json::Value s;
                    s["type"] = ts.type;
                    s["total"] = ts.total;
                    s["new"] = ts.newCount;
                    s["skipped"] = ts.skipped;
                    s["failed"] = ts.failed;
                    stats.append(s);
                }
                item["typeStats"] = stats;
            }
            json.append(item);
        }

        int total = syncService_->getSyncHistoryCount(statusFilter);
        Json::Value response;
        response["data"] = json;
        response["total"] = total;
        response["limit"] = limit;
        response["offset"] = offset;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);
        return;
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

void IcaoLdapHandler::handleGetConfig(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value json;

    if (syncService_) {
        auto config = syncService_->getConfig();
        json["enabled"] = config.enabled;
        json["host"] = config.host;
        json["port"] = config.port;
        json["baseDn"] = config.baseDn;
        json["syncIntervalMinutes"] = config.syncIntervalMinutes;
    } else {
        json["enabled"] = false;
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

void IcaoLdapHandler::handleUpdateConfig(const drogon::HttpRequestPtr& req,
                                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    if (!syncService_) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(
            Json::Value("ICAO LDAP sync not configured"));
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        callback(resp);
        return;
    }

    auto body = req->getJsonObject();
    if (!body) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value("Invalid JSON"));
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    auto config = syncService_->getConfig();
    if (body->isMember("enabled")) config.enabled = (*body)["enabled"].asBool();
    if (body->isMember("syncIntervalMinutes"))
        config.syncIntervalMinutes = (*body)["syncIntervalMinutes"].asInt();

    syncService_->updateConfig(config);

    Json::Value json;
    json["status"] = "updated";
    json["enabled"] = config.enabled;
    json["syncIntervalMinutes"] = config.syncIntervalMinutes;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

void IcaoLdapHandler::handleTestConnection(const drogon::HttpRequestPtr& req,
                                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    if (!syncService_) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(
            Json::Value("ICAO LDAP sync not configured"));
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        callback(resp);
        return;
    }

    auto result = syncService_->testConnection();

    Json::Value json;
    json["success"] = result.success;
    json["latencyMs"] = result.latencyMs;
    json["entryCount"] = result.entryCount;
    json["serverInfo"] = result.serverInfo;
    json["tlsMode"] = result.tlsMode;
    if (!result.errorMessage.empty()) {
        json["errorMessage"] = result.errorMessage;
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

} // namespace relay
} // namespace icao

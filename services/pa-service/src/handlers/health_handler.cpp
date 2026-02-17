/** @file health_handler.cpp
 *  @brief HealthHandler implementation
 */

#include "health_handler.h"
#include <spdlog/spdlog.h>

namespace handlers {

HealthHandler::HealthHandler(
    std::function<Json::Value()> checkDatabase,
    std::function<Json::Value()> checkLdap,
    std::function<std::string()> getCurrentTimestamp)
    : checkDatabase_(std::move(checkDatabase)),
      checkLdap_(std::move(checkLdap)),
      getCurrentTimestamp_(std::move(getCurrentTimestamp)) {

    if (!checkDatabase_ || !checkLdap_ || !getCurrentTimestamp_) {
        throw std::invalid_argument("HealthHandler: health check functions cannot be nullptr");
    }

    spdlog::info("[HealthHandler] Initialized");
}

void HealthHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // GET /api/health
    app.registerHandler(
        "/api/health",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleHealth(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/health/database
    app.registerHandler(
        "/api/health/database",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleDatabaseHealth(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/health/ldap
    app.registerHandler(
        "/api/health/ldap",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLdapHealth(req, std::move(callback));
        },
        {drogon::Get}
    );

    spdlog::info("[HealthHandler] Routes registered");
}

void HealthHandler::handleHealth(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value result;
    result["service"] = "pa-service";
    result["status"] = "UP";
    result["version"] = "2.1.1";
    result["timestamp"] = getCurrentTimestamp_();

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void HealthHandler::handleDatabaseHealth(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/health/database");
    auto result = checkDatabase_();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (result["status"].asString() != "UP") {
        resp->setStatusCode(drogon::k503ServiceUnavailable);
    }
    callback(resp);
}

void HealthHandler::handleLdapHealth(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/health/ldap");
    auto result = checkLdap_();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (result["status"].asString() != "UP") {
        resp->setStatusCode(drogon::k503ServiceUnavailable);
    }
    callback(resp);
}

} // namespace handlers

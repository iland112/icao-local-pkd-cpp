/**
 * @file health_handler.cpp
 * @brief Health check handler implementation
 */

#include "health_handler.h"
#include <spdlog/spdlog.h>

namespace handlers {

HealthHandler::HealthHandler(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor) {}

void HealthHandler::handle(const drogon::HttpRequestPtr&,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value response(Json::objectValue);
    response["status"] = "UP";
    response["service"] = "sync-service";
    response["timestamp"] = trantor::Date::now().toFormattedString(false);

    // Check DB connection via Query Executor (database-agnostic)
    try {
        if (queryExecutor_) {
            // Oracle requires FROM DUAL for any SELECT
            std::string healthQuery = (queryExecutor_->getDatabaseType() == "oracle")
                ? "SELECT 1 FROM DUAL" : "SELECT 1";
            queryExecutor_->executeScalar(healthQuery, {});
            response["database"] = "UP";
            response["databaseType"] = queryExecutor_->getDatabaseType();
        } else {
            response["database"] = "DOWN";
            response["status"] = "DEGRADED";
        }
    } catch (...) {
        response["database"] = "DOWN";
        response["status"] = "DEGRADED";
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

} // namespace handlers

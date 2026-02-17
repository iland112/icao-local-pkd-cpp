/**
 * @file reconciliation_handler.cpp
 * @brief Reconciliation handler implementation
 *
 * Extracted from main.cpp reconciliation HTTP handler functions.
 */

#include "reconciliation_handler.h"
#include "relay/sync/common/config.h"
#include "relay/sync/common/types.h"
#include "relay/sync/reconciliation_engine.h"
#include "../services/reconciliation_service.h"

#include <spdlog/spdlog.h>

using namespace drogon;

namespace handlers {

ReconciliationHandler::ReconciliationHandler(
    icao::relay::services::ReconciliationService* reconciliationService,
    common::IQueryExecutor* queryExecutor,
    common::LdapConnectionPool* ldapPool,
    icao::relay::Config& config)
    : reconciliationService_(reconciliationService)
    , queryExecutor_(queryExecutor)
    , ldapPool_(ldapPool)
    , config_(config) {}

void ReconciliationHandler::handleReconcile(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback) {
    // Check if auto reconcile is enabled
    if (!config_.autoReconcile) {
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
        // Create reconciliation engine with Query Executor (database-agnostic)
        icao::relay::ReconciliationEngine engine(config_, ldapPool_, queryExecutor_);
        icao::relay::ReconciliationResult result = engine.performReconciliation(dryRun);

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

void ReconciliationHandler::handleReconciliationHistory(const HttpRequestPtr& req,
                                                         std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        int limit = 50;
        int offset = 0;

        if (auto param = req->getParameter("limit"); !param.empty()) {
            limit = std::stoi(param);
            if (limit < 1 || limit > 100) limit = 50;
        }
        if (auto param = req->getParameter("offset"); !param.empty()) {
            offset = std::stoi(param);
        }

        Json::Value result = reconciliationService_->getReconciliationHistory(limit, offset);

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

void ReconciliationHandler::handleReconciliationDetails(const HttpRequestPtr& req,
                                                         std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
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

        Json::Value result = reconciliationService_->getReconciliationDetails(reconciliationIdStr);

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

void ReconciliationHandler::handleReconciliationStats(const HttpRequestPtr&,
                                                       std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        Json::Value result = reconciliationService_->getReconciliationStatistics();

        auto resp = HttpResponse::newHttpJsonResponse(result);
        if (!result.get("success", true).asBool()) {
            resp->setStatusCode(k500InternalServerError);
        }

        callback(resp);

    } catch (const std::exception& e) {
        Json::Value error(Json::objectValue);
        error["success"] = false;
        error["error"] = std::string("Failed to get reconciliation statistics: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

} // namespace handlers

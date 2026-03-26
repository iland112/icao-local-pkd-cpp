/** @file misc_handler.cpp
 *  @brief MiscHandler implementation
 */

#include "misc_handler.h"
#include <spdlog/spdlog.h>
#include <trantor/utils/Date.h>
#include <ctime>
#include "handler_utils.h"

namespace handlers {

MiscHandler::MiscHandler(
    services::AuditService* auditService,
    services::ValidationService* validationService,
    std::function<Json::Value()> checkDatabase,
    std::function<Json::Value()> checkLdap)
    : auditService_(auditService),
      validationService_(validationService),
      checkDatabase_(std::move(checkDatabase)),
      checkLdap_(std::move(checkLdap)) {

    if (!auditService_ || !validationService_) {
        throw std::invalid_argument("MiscHandler: services cannot be nullptr");
    }

    if (!checkDatabase_ || !checkLdap_) {
        throw std::invalid_argument("MiscHandler: health check functions cannot be nullptr");
    }

    spdlog::info("[MiscHandler] Initialized");
}

void MiscHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // GET /api/audit/operations
    app.registerHandler(
        "/api/audit/operations",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetOperationLogs(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/audit/operations/stats
    app.registerHandler(
        "/api/audit/operations/stats",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetOperationStats(req, std::move(callback));
        },
        {drogon::Get}
    );

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
            handleHealthDatabase(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/health/ldap
    app.registerHandler(
        "/api/health/ldap",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleHealthLdap(req, std::move(callback));
        },
        {drogon::Get}
    );

    // POST /api/validation/revalidate (also accepts GET)
    app.registerHandler(
        "/api/validation/revalidate",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleRevalidate(req, std::move(callback));
        },
        {drogon::Post, drogon::Get}
    );

    // PA mock endpoints removed — PA Service (port 8082) handles /api/pa/*

    // GET /api/ldap/health
    app.registerHandler(
        "/api/ldap/health",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLdapHealth(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /
    app.registerHandler(
        "/",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleRoot(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api
    app.registerHandler(
        "/api",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleApiInfo(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/openapi.yaml
    app.registerHandler(
        "/api/openapi.yaml",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleOpenApiSpec(req, std::move(callback));
        },
        {drogon::Get}
    );

    spdlog::info("[MiscHandler] Registered 13 routes");
}

// --- Handler Implementations ---

void MiscHandler::handleGetOperationLogs(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/audit/operations - List audit logs");

    try {
        // Build filter from query parameters
        services::AuditService::AuditLogFilter filter;
        filter.limit = req->getOptionalParameter<int>("limit").value_or(50);
        filter.offset = req->getOptionalParameter<int>("offset").value_or(0);
        filter.operationType = req->getOptionalParameter<std::string>("operationType").value_or("");
        filter.username = req->getOptionalParameter<std::string>("username").value_or("");
        filter.success = req->getOptionalParameter<std::string>("success").value_or("");

        // Call AuditService (Repository Pattern)
        Json::Value result = auditService_->getOperationLogs(filter);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        if (!result.get("success", false).asBool()) {
            resp->setStatusCode(drogon::k500InternalServerError);
        }
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("MiscHandler::handleGetOperationLogs", e));
    }
}

void MiscHandler::handleGetOperationStats(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/audit/operations/stats - Audit log statistics");

    try {
        // Call AuditService (Repository Pattern)
        Json::Value result = auditService_->getOperationStatistics();

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        if (!result.get("success", false).asBool()) {
            resp->setStatusCode(drogon::k500InternalServerError);
        }
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("MiscHandler::handleGetOperationStats", e));
    }
}

void MiscHandler::handleHealth(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value result;
    result["status"] = "UP";
    result["service"] = "icao-local-pkd";
    result["version"] = "1.0.0";
    result["timestamp"] = trantor::Date::now().toFormattedString(false);

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void MiscHandler::handleHealthDatabase(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto result = checkDatabase_();

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (result["status"].asString() != "UP") {
        resp->setStatusCode(drogon::k503ServiceUnavailable);
    }
    callback(resp);
}

void MiscHandler::handleHealthLdap(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto result = checkLdap_();

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (result["status"].asString() != "UP") {
        resp->setStatusCode(drogon::k503ServiceUnavailable);
    }
    callback(resp);
}

void MiscHandler::handleRevalidate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        spdlog::info("POST /api/validation/revalidate - Re-validate DSC certificates");

        // Call ValidationService (Repository Pattern)
        auto result = validationService_->revalidateDscCertificates();

        // Build response
        Json::Value response;
        response["success"] = result.success;
        response["message"] = result.message;
        response["totalProcessed"] = result.totalProcessed;
        response["validCount"] = result.validCount;
        response["expiredValidCount"] = result.expiredValidCount;
        response["invalidCount"] = result.invalidCount;
        response["pendingCount"] = result.pendingCount;
        response["errorCount"] = result.errorCount;
        response["durationSeconds"] = result.durationSeconds;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("MiscHandler::handleRevalidate", e));
    }
}


void MiscHandler::handleLdapHealth(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/ldap/health");
    auto result = checkLdap_();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    if (result["status"].asString() != "UP") {
        resp->setStatusCode(drogon::k503ServiceUnavailable);
    }
    callback(resp);
}


void MiscHandler::handleRoot(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value result;
    result["name"] = "ICAO Local PKD";
    result["description"] = "ICAO Local PKD Management and Passive Authentication System";
    result["version"] = "1.0.0";
    result["endpoints"]["health"] = "/api/health";
    result["endpoints"]["upload"] = "/api/upload";
    result["endpoints"]["pa"] = "/api/pa";
    result["endpoints"]["ldap"] = "/api/ldap";

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void MiscHandler::handleApiInfo(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value result;
    result["api"] = "ICAO Local PKD REST API";
    result["version"] = "v1";

    Json::Value endpoints(Json::arrayValue);

    Json::Value health;
    health["method"] = "GET";
    health["path"] = "/api/health";
    health["description"] = "Health check endpoint";
    endpoints.append(health);

    Json::Value healthDb;
    healthDb["method"] = "GET";
    healthDb["path"] = "/api/health/database";
    healthDb["description"] = "Database health check";
    endpoints.append(healthDb);

    Json::Value healthLdap;
    healthLdap["method"] = "GET";
    healthLdap["path"] = "/api/health/ldap";
    healthLdap["description"] = "LDAP health check";
    endpoints.append(healthLdap);

    Json::Value uploadLdif;
    uploadLdif["method"] = "POST";
    uploadLdif["path"] = "/api/upload/ldif";
    uploadLdif["description"] = "Upload LDIF file";
    endpoints.append(uploadLdif);

    Json::Value uploadMl;
    uploadMl["method"] = "POST";
    uploadMl["path"] = "/api/upload/masterlist";
    uploadMl["description"] = "Upload Master List file";
    endpoints.append(uploadMl);

    Json::Value uploadHistory;
    uploadHistory["method"] = "GET";
    uploadHistory["path"] = "/api/upload/history";
    uploadHistory["description"] = "Get upload history";
    endpoints.append(uploadHistory);

    Json::Value uploadStats;
    uploadStats["method"] = "GET";
    uploadStats["path"] = "/api/upload/statistics";
    uploadStats["description"] = "Get upload statistics";
    endpoints.append(uploadStats);


    result["endpoints"] = endpoints;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void MiscHandler::handleOpenApiSpec(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/openapi.yaml");

    // OpenAPI 3.0 specification
    std::string openApiSpec = R"(openapi: 3.0.3
info:
  title: PKD Management Service API
  description: ICAO Local PKD Management Service - Certificate upload, validation, and PA verification
  version: 1.0.0
servers:
  - url: /
tags:
  - name: Health
    description: Health check endpoints
  - name: Upload
    description: Certificate upload operations
  - name: Validation
    description: Certificate validation
  - name: PA
    description: Passive Authentication
  - name: Progress
    description: Upload progress tracking
paths:
  /api/health:
    get:
      tags: [Health]
      summary: Application health check
      responses:
        '200':
          description: Service is healthy
  /api/health/database:
    get:
      tags: [Health]
      summary: Database health check
      responses:
        '200':
          description: Database status
  /api/health/ldap:
    get:
      tags: [Health]
      summary: LDAP health check
      responses:
        '200':
          description: LDAP status
  /api/upload/ldif:
    post:
      tags: [Upload]
      summary: Upload LDIF file
      requestBody:
        content:
          multipart/form-data:
            schema:
              type: object
              properties:
                file:
                  type: string
                  format: binary
      responses:
        '200':
          description: Upload successful
  /api/upload/masterlist:
    post:
      tags: [Upload]
      summary: Upload Master List file
      requestBody:
        content:
          multipart/form-data:
            schema:
              type: object
              properties:
                file:
                  type: string
                  format: binary
      responses:
        '200':
          description: Upload successful
  /api/upload/statistics:
    get:
      tags: [Upload]
      summary: Get upload statistics
      responses:
        '200':
          description: Statistics data
  /api/upload/history:
    get:
      tags: [Upload]
      summary: Get upload history
      parameters:
        - name: limit
          in: query
          schema:
            type: integer
        - name: offset
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: Upload history
  /api/upload/countries:
    get:
      tags: [Upload]
      summary: Get country statistics
      responses:
        '200':
          description: Country stats
  /api/validation/revalidate:
    post:
      tags: [Validation]
      summary: Re-validate DSC trust chains
      responses:
        '200':
          description: Revalidation result
          description: PA history
  /api/progress/stream/{uploadId}:
    get:
      tags: [Progress]
      summary: SSE progress stream
      parameters:
        - name: uploadId
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: SSE stream
  /api/progress/status/{uploadId}:
    get:
      tags: [Progress]
      summary: Get progress status
      parameters:
        - name: uploadId
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: Progress status
)";

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(openApiSpec);
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    resp->addHeader("Content-Type", "application/x-yaml");
    callback(resp);
}

} // namespace handlers

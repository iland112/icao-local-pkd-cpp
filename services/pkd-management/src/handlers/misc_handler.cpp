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

    // GET /api/pa/statistics
    app.registerHandler(
        "/api/pa/statistics",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePaStatistics(req, std::move(callback));
        },
        {drogon::Get}
    );

    // POST /api/pa/verify
    app.registerHandler(
        "/api/pa/verify",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePaVerify(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/ldap/health
    app.registerHandler(
        "/api/ldap/health",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLdapHealth(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/pa/history
    app.registerHandler(
        "/api/pa/history",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePaHistory(req, std::move(callback));
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

void MiscHandler::handlePaStatistics(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/pa/statistics");

    // Return PAStatisticsOverview format matching frontend expectations
    Json::Value result;
    result["totalVerifications"] = 0;
    result["validCount"] = 0;
    result["invalidCount"] = 0;
    result["errorCount"] = 0;
    result["averageProcessingTimeMs"] = 0;
    result["countriesVerified"] = 0;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void MiscHandler::handlePaVerify(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("POST /api/pa/verify - Passive Authentication verification");

    // Mock response for PA verification
    Json::Value result;
    result["success"] = true;

    Json::Value data;
    data["id"] = "pa-" + std::to_string(std::time(nullptr));
    data["status"] = "VALID";
    data["overallValid"] = true;
    data["verifiedAt"] = trantor::Date::now().toFormattedString(false);
    data["processingTimeMs"] = 150;

    // Step results
    Json::Value sodParsing;
    sodParsing["step"] = "SOD_PARSING";
    sodParsing["status"] = "SUCCESS";
    sodParsing["message"] = "SOD 파싱 완료";
    data["sodParsing"] = sodParsing;

    Json::Value dscExtraction;
    dscExtraction["step"] = "DSC_EXTRACTION";
    dscExtraction["status"] = "SUCCESS";
    dscExtraction["message"] = "DSC 인증서 추출 완료";
    data["dscExtraction"] = dscExtraction;

    Json::Value cscaLookup;
    cscaLookup["step"] = "CSCA_LOOKUP";
    cscaLookup["status"] = "SUCCESS";
    cscaLookup["message"] = "CSCA 인증서 조회 완료";
    data["cscaLookup"] = cscaLookup;

    Json::Value trustChainValidation;
    trustChainValidation["step"] = "TRUST_CHAIN_VALIDATION";
    trustChainValidation["status"] = "SUCCESS";
    trustChainValidation["message"] = "Trust Chain 검증 완료";
    data["trustChainValidation"] = trustChainValidation;

    Json::Value sodSignatureValidation;
    sodSignatureValidation["step"] = "SOD_SIGNATURE_VALIDATION";
    sodSignatureValidation["status"] = "SUCCESS";
    sodSignatureValidation["message"] = "SOD 서명 검증 완료";
    data["sodSignatureValidation"] = sodSignatureValidation;

    Json::Value dataGroupHashValidation;
    dataGroupHashValidation["step"] = "DATA_GROUP_HASH_VALIDATION";
    dataGroupHashValidation["status"] = "SUCCESS";
    dataGroupHashValidation["message"] = "Data Group 해시 검증 완료";
    data["dataGroupHashValidation"] = dataGroupHashValidation;

    Json::Value crlCheck;
    crlCheck["step"] = "CRL_CHECK";
    crlCheck["status"] = "SUCCESS";
    crlCheck["message"] = "CRL 확인 완료 - 인증서 유효";
    data["crlCheck"] = crlCheck;

    result["data"] = data;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    resp->setStatusCode(drogon::k200OK);
    callback(resp);
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

void MiscHandler::handlePaHistory(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/pa/history");

    // Get query parameters
    int page = 0;
    int size = 20;
    if (auto p = req->getParameter("page"); !p.empty()) {
        page = std::stoi(p);
    }
    if (auto s = req->getParameter("size"); !s.empty()) {
        size = std::stoi(s);
    }

    // Return PageResponse format matching frontend expectations
    Json::Value result;
    result["content"] = Json::Value(Json::arrayValue);
    result["page"] = page;
    result["size"] = size;
    result["totalElements"] = 0;
    result["totalPages"] = 0;
    result["first"] = true;
    result["last"] = true;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
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

    Json::Value paVerify;
    paVerify["method"] = "POST";
    paVerify["path"] = "/api/pa/verify";
    paVerify["description"] = "Perform Passive Authentication";
    endpoints.append(paVerify);

    Json::Value paHistory;
    paHistory["method"] = "GET";
    paHistory["path"] = "/api/pa/history";
    paHistory["description"] = "Get PA verification history";
    endpoints.append(paHistory);

    Json::Value paStats;
    paStats["method"] = "GET";
    paStats["path"] = "/api/pa/statistics";
    paStats["description"] = "Get PA verification statistics";
    endpoints.append(paStats);

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
  /api/pa/verify:
    post:
      tags: [PA]
      summary: Verify Passive Authentication
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                sod:
                  type: string
                dataGroups:
                  type: object
      responses:
        '200':
          description: Verification result
  /api/pa/statistics:
    get:
      tags: [PA]
      summary: Get PA statistics
      responses:
        '200':
          description: PA stats
  /api/pa/history:
    get:
      tags: [PA]
      summary: Get PA history
      responses:
        '200':
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

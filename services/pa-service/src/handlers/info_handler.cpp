/** @file info_handler.cpp
 *  @brief InfoHandler implementation
 */

#include "info_handler.h"
#include <spdlog/spdlog.h>

namespace handlers {

InfoHandler::InfoHandler() {
    spdlog::info("[InfoHandler] Initialized");
}

void InfoHandler::registerRoutes(drogon::HttpAppFramework& app) {
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

    // GET /api/docs
    app.registerHandler(
        "/api/docs",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleDocs(req, std::move(callback));
        },
        {drogon::Get}
    );

    spdlog::info("[InfoHandler] Routes registered");
}

void InfoHandler::handleRoot(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value result;
    result["name"] = "PA Service";
    result["description"] = "ICAO Passive Authentication Service - ePassport PA Verification";
    result["version"] = "2.1.1";
    result["endpoints"]["health"] = "/api/health";
    result["endpoints"]["pa"] = "/api/pa";

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void InfoHandler::handleApiInfo(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value result;
    result["api"] = "PA Service REST API";
    result["version"] = "v2";

    Json::Value endpoints(Json::arrayValue);

    Json::Value verify;
    verify["method"] = "POST";
    verify["path"] = "/api/pa/verify";
    verify["description"] = "Perform Passive Authentication verification";
    endpoints.append(verify);

    Json::Value history;
    history["method"] = "GET";
    history["path"] = "/api/pa/history";
    history["description"] = "Get PA verification history";
    endpoints.append(history);

    Json::Value stats;
    stats["method"] = "GET";
    stats["path"] = "/api/pa/statistics";
    stats["description"] = "Get PA verification statistics";
    endpoints.append(stats);

    result["endpoints"] = endpoints;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void InfoHandler::handleOpenApiSpec(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/openapi.yaml");

    std::string openApiSpec = R"(openapi: 3.0.3
info:
  title: PA Service API
  description: ICAO 9303 Passive Authentication Verification Service
  version: 2.0.0
servers:
  - url: /
tags:
  - name: Health
    description: Health check endpoints
  - name: PA
    description: Passive Authentication operations
  - name: Parser
    description: Document parsing utilities
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
  /api/pa/verify:
    post:
      tags: [PA]
      summary: Verify Passive Authentication
      description: Perform complete ICAO 9303 PA verification
      requestBody:
        content:
          application/json:
            schema:
              type: object
              required: [sod, dataGroups]
              properties:
                sod:
                  type: string
                  description: Base64 encoded SOD
                dataGroups:
                  type: object
                  description: Map of DG number to Base64 data
      responses:
        '200':
          description: Verification result
  /api/pa/statistics:
    get:
      tags: [PA]
      summary: Get PA statistics
      responses:
        '200':
          description: PA verification statistics
  /api/pa/history:
    get:
      tags: [PA]
      summary: Get PA verification history
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
          description: PA history list
  /api/pa/{id}:
    get:
      tags: [PA]
      summary: Get verification details
      parameters:
        - name: id
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: Verification details
  /api/pa/{id}/datagroups:
    get:
      tags: [PA]
      summary: Get data groups info
      parameters:
        - name: id
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: Data groups information
  /api/pa/parse-dg1:
    post:
      tags: [Parser]
      summary: Parse DG1 (MRZ) data
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                dg1:
                  type: string
      responses:
        '200':
          description: Parsed MRZ data
  /api/pa/parse-dg2:
    post:
      tags: [Parser]
      summary: Parse DG2 (Face Image)
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                dg2:
                  type: string
      responses:
        '200':
          description: Extracted face image
  /api/pa/parse-mrz-text:
    post:
      tags: [Parser]
      summary: Parse MRZ text
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                mrz:
                  type: string
      responses:
        '200':
          description: Parsed MRZ data
  /api/pa/parse-sod:
    post:
      tags: [Parser]
      summary: Parse SOD (Security Object)
      description: Extract metadata from SOD including DSC certificate, hash algorithm, and contained data groups
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                sod:
                  type: string
                  description: Base64 encoded SOD data
      responses:
        '200':
          description: Parsed SOD metadata
          content:
            application/json:
              schema:
                type: object
                properties:
                  success:
                    type: boolean
                  hashAlgorithm:
                    type: string
                  signatureAlgorithm:
                    type: string
                  dscCertificate:
                    type: object
                  containedDataGroups:
                    type: array
)";

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody(openApiSpec);
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    resp->addHeader("Content-Type", "application/x-yaml");
    callback(resp);
}

void InfoHandler::handleDocs(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto resp = drogon::HttpResponse::newRedirectionResponse("/swagger-ui/index.html");
    callback(resp);
}

} // namespace handlers

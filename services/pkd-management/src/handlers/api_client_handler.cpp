/** @file api_client_handler.cpp
 *  @brief ApiClientHandler implementation — 7 API endpoints for client management
 */

#include "api_client_handler.h"
#include "../auth/api_key_generator.h"
#include <spdlog/spdlog.h>
#include <cstdlib>

using namespace drogon;

namespace handlers {

ApiClientHandler::ApiClientHandler(repositories::ApiClientRepository* repository)
    : repository_(repository)
{
    // Initialize JWT service for admin validation
    const char* jwtSecret = std::getenv("JWT_SECRET_KEY");
    const char* jwtIssuer = std::getenv("JWT_ISSUER");
    const char* jwtExpirationStr = std::getenv("JWT_EXPIRATION_SECONDS");
    int jwtExpiration = jwtExpirationStr ? std::atoi(jwtExpirationStr) : 3600;

    if (jwtSecret && strlen(jwtSecret) >= 32) {
        jwtService_ = std::make_shared<auth::JwtService>(
            jwtSecret, jwtIssuer ? jwtIssuer : "icao-pkd", jwtExpiration);
    }

    spdlog::info("[ApiClientHandler] Initialized");
}

ApiClientHandler::~ApiClientHandler() = default;

void ApiClientHandler::registerRoutes(HttpAppFramework& app) {
    spdlog::info("[ApiClientHandler] Registering API Client routes");

    // POST /api/auth/api-clients — Create
    app.registerHandler(
        "/api/auth/api-clients",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleCreate(req, std::move(callback));
        },
        {Post});

    // GET /api/auth/api-clients — List all
    app.registerHandler(
        "/api/auth/api-clients",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleGetAll(req, std::move(callback));
        },
        {Get});

    // GET /api/auth/api-clients/{id} — Detail
    app.registerHandler(
        "/api/auth/api-clients/{id}",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleGetById(req, std::move(callback), id);
        },
        {Get});

    // PUT /api/auth/api-clients/{id} — Update
    app.registerHandler(
        "/api/auth/api-clients/{id}",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleUpdate(req, std::move(callback), id);
        },
        {Put});

    // DELETE /api/auth/api-clients/{id} — Deactivate
    app.registerHandler(
        "/api/auth/api-clients/{id}",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleDelete(req, std::move(callback), id);
        },
        {Delete});

    // POST /api/auth/api-clients/{id}/regenerate — Regenerate key
    app.registerHandler(
        "/api/auth/api-clients/{id}/regenerate",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleRegenerate(req, std::move(callback), id);
        },
        {Post});

    // GET /api/auth/api-clients/{id}/usage — Usage stats
    app.registerHandler(
        "/api/auth/api-clients/{id}/usage",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleGetUsage(req, std::move(callback), id);
        },
        {Get});

    // GET /api/auth/internal/check — Internal auth check for nginx auth_request
    // The actual logic is handled by AuthMiddleware in registerPreHandlingAdvice.
    // This handler is a fallback that should never be reached.
    app.registerHandler(
        "/api/auth/internal/check",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            callback(resp);
        },
        {Get});

    spdlog::info("[ApiClientHandler] Routes registered: 7 endpoints on /api/auth/api-clients + internal auth-check");
}

// ============================================================================
// POST /api/auth/api-clients — Create new API client
// ============================================================================
void ApiClientHandler::handleCreate(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        auto body = req->getJsonObject();
        if (!body || !(*body).isMember("client_name")) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "client_name is required";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k400BadRequest);
            callback(response);
            return;
        }

        // Generate API key
        auto keyInfo = auth::generateApiKey();

        domain::models::ApiClient client;
        client.clientName = (*body)["client_name"].asString();
        client.apiKeyHash = keyInfo.hash;
        client.apiKeyPrefix = keyInfo.prefix;
        client.description = (*body).isMember("description") ? std::optional((*body)["description"].asString()) : std::nullopt;
        client.createdBy = admin->userId;

        // Parse permissions
        if ((*body).isMember("permissions") && (*body)["permissions"].isArray()) {
            for (const auto& p : (*body)["permissions"]) {
                client.permissions.push_back(p.asString());
            }
        }

        // Parse allowed endpoints
        if ((*body).isMember("allowed_endpoints") && (*body)["allowed_endpoints"].isArray()) {
            for (const auto& e : (*body)["allowed_endpoints"]) {
                client.allowedEndpoints.push_back(e.asString());
            }
        }

        // Parse allowed IPs
        if ((*body).isMember("allowed_ips") && (*body)["allowed_ips"].isArray()) {
            for (const auto& ip : (*body)["allowed_ips"]) {
                client.allowedIps.push_back(ip.asString());
            }
        }

        // Rate limits
        if ((*body).isMember("rate_limit_per_minute"))
            client.rateLimitPerMinute = (*body)["rate_limit_per_minute"].asInt();
        if ((*body).isMember("rate_limit_per_hour"))
            client.rateLimitPerHour = (*body)["rate_limit_per_hour"].asInt();
        if ((*body).isMember("rate_limit_per_day"))
            client.rateLimitPerDay = (*body)["rate_limit_per_day"].asInt();

        // Expiration
        if ((*body).isMember("expires_at") && !(*body)["expires_at"].isNull()) {
            client.expiresAt = (*body)["expires_at"].asString();
        }

        std::string id = repository_->insert(client);
        if (id.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Failed to create API client";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k500InternalServerError);
            callback(response);
            return;
        }

        // Fetch created client for response
        auto created = repository_->findById(id);

        Json::Value resp;
        resp["success"] = true;
        resp["warning"] = "API Key is only shown in this response. Store it securely.";
        if (created) {
            resp["client"] = modelToJson(*created);
        }
        resp["client"]["api_key"] = keyInfo.key;  // Only time the raw key is returned

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientHandler] handleCreate failed: {}", e.what());
        Json::Value resp;
        resp["success"] = false;
        resp["message"] = std::string("Error: ") + e.what();
        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// GET /api/auth/api-clients — List all clients
// ============================================================================
void ApiClientHandler::handleGetAll(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        int limit = 100, offset = 0;
        auto limitParam = req->getParameter("limit");
        auto offsetParam = req->getParameter("offset");
        if (!limitParam.empty()) limit = std::stoi(limitParam);
        if (!offsetParam.empty()) offset = std::stoi(offsetParam);

        bool activeOnly = req->getParameter("active_only") == "true";

        auto clients = repository_->findAll(activeOnly, limit, offset);
        int total = repository_->countAll(activeOnly);

        Json::Value resp;
        resp["success"] = true;
        resp["total"] = total;

        Json::Value items(Json::arrayValue);
        for (const auto& c : clients) {
            items.append(modelToJson(c));
        }
        resp["clients"] = items;

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientHandler] handleGetAll failed: {}", e.what());
        Json::Value resp;
        resp["success"] = false;
        resp["message"] = std::string("Error: ") + e.what();
        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// GET /api/auth/api-clients/{id}
// ============================================================================
void ApiClientHandler::handleGetById(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        auto client = repository_->findById(id);
        if (!client) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Client not found";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k404NotFound);
            callback(response);
            return;
        }

        Json::Value resp;
        resp["success"] = true;
        resp["client"] = modelToJson(*client);

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientHandler] handleGetById failed: {}", e.what());
        Json::Value resp;
        resp["success"] = false;
        resp["message"] = std::string("Error: ") + e.what();
        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// PUT /api/auth/api-clients/{id}
// ============================================================================
void ApiClientHandler::handleUpdate(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        auto existing = repository_->findById(id);
        if (!existing) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Client not found";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k404NotFound);
            callback(response);
            return;
        }

        auto body = req->getJsonObject();
        if (!body) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Invalid JSON body";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k400BadRequest);
            callback(response);
            return;
        }

        // Update fields
        auto& client = *existing;
        if ((*body).isMember("client_name")) client.clientName = (*body)["client_name"].asString();
        if ((*body).isMember("description")) client.description = (*body)["description"].asString();
        if ((*body).isMember("is_active")) client.isActive = (*body)["is_active"].asBool();

        if ((*body).isMember("permissions") && (*body)["permissions"].isArray()) {
            client.permissions.clear();
            for (const auto& p : (*body)["permissions"]) client.permissions.push_back(p.asString());
        }
        if ((*body).isMember("allowed_endpoints") && (*body)["allowed_endpoints"].isArray()) {
            client.allowedEndpoints.clear();
            for (const auto& e : (*body)["allowed_endpoints"]) client.allowedEndpoints.push_back(e.asString());
        }
        if ((*body).isMember("allowed_ips") && (*body)["allowed_ips"].isArray()) {
            client.allowedIps.clear();
            for (const auto& ip : (*body)["allowed_ips"]) client.allowedIps.push_back(ip.asString());
        }

        if ((*body).isMember("rate_limit_per_minute")) client.rateLimitPerMinute = (*body)["rate_limit_per_minute"].asInt();
        if ((*body).isMember("rate_limit_per_hour")) client.rateLimitPerHour = (*body)["rate_limit_per_hour"].asInt();
        if ((*body).isMember("rate_limit_per_day")) client.rateLimitPerDay = (*body)["rate_limit_per_day"].asInt();

        client.id = id;
        bool updated = repository_->update(client);

        Json::Value resp;
        resp["success"] = updated;
        if (updated) {
            auto refreshed = repository_->findById(id);
            if (refreshed) resp["client"] = modelToJson(*refreshed);
        }

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientHandler] handleUpdate failed: {}", e.what());
        Json::Value resp;
        resp["success"] = false;
        resp["message"] = std::string("Error: ") + e.what();
        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// DELETE /api/auth/api-clients/{id}
// ============================================================================
void ApiClientHandler::handleDelete(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        bool deactivated = repository_->deactivate(id);

        Json::Value resp;
        resp["success"] = deactivated;
        resp["message"] = deactivated ? "Client deactivated" : "Client not found";

        auto response = HttpResponse::newHttpJsonResponse(resp);
        if (!deactivated) response->setStatusCode(k404NotFound);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientHandler] handleDelete failed: {}", e.what());
        Json::Value resp;
        resp["success"] = false;
        resp["message"] = std::string("Error: ") + e.what();
        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// POST /api/auth/api-clients/{id}/regenerate
// ============================================================================
void ApiClientHandler::handleRegenerate(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        auto existing = repository_->findById(id);
        if (!existing) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Client not found";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k404NotFound);
            callback(response);
            return;
        }

        // Generate new key
        auto keyInfo = auth::generateApiKey();

        // Update hash and prefix in DB (dedicated method, not general update)
        repository_->updateKeyHash(id, keyInfo.hash, keyInfo.prefix);
        existing->apiKeyPrefix = keyInfo.prefix;

        Json::Value resp;
        resp["success"] = true;
        resp["warning"] = "New API Key is only shown in this response. Store it securely.";
        resp["client"] = modelToJson(*existing);
        resp["client"]["api_key"] = keyInfo.key;
        resp["client"]["api_key_prefix"] = keyInfo.prefix;

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientHandler] handleRegenerate failed: {}", e.what());
        Json::Value resp;
        resp["success"] = false;
        resp["message"] = std::string("Error: ") + e.what();
        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// GET /api/auth/api-clients/{id}/usage
// ============================================================================
void ApiClientHandler::handleGetUsage(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        int days = 7;
        auto daysParam = req->getParameter("days");
        if (!daysParam.empty()) days = std::stoi(daysParam);

        auto stats = repository_->getUsageStats(id, days);

        Json::Value resp;
        resp["success"] = true;
        resp["client_id"] = id;
        resp["days"] = days;
        resp["usage"] = stats;

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientHandler] handleGetUsage failed: {}", e.what());
        Json::Value resp;
        resp["success"] = false;
        resp["message"] = std::string("Error: ") + e.what();
        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// Helpers
// ============================================================================
std::optional<auth::JwtClaims> ApiClientHandler::validateRequestToken(
    const drogon::HttpRequestPtr& req) {

    if (!jwtService_) return std::nullopt;

    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") {
        return std::nullopt;
    }

    std::string token = authHeader.substr(7);
    return jwtService_->validateToken(token);
}

std::optional<auth::JwtClaims> ApiClientHandler::requireAdmin(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>& callback) {

    auto claims = validateRequestToken(req);
    if (!claims) {
        // Token missing or expired → 401
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Unauthorized";
        resp["message"] = "Invalid or missing authentication token";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k401Unauthorized);
        callback(response);
        return std::nullopt;
    }
    if (!claims->isAdmin) {
        // Not admin → 403
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Forbidden";
        resp["message"] = "Admin privileges required";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k403Forbidden);
        callback(response);
        return std::nullopt;
    }
    return claims;
}

Json::Value ApiClientHandler::modelToJson(const domain::models::ApiClient& client) {
    Json::Value json;
    json["id"] = client.id;
    json["client_name"] = client.clientName;
    json["api_key_prefix"] = client.apiKeyPrefix;
    json["description"] = client.description.value_or("");

    Json::Value perms(Json::arrayValue);
    for (const auto& p : client.permissions) perms.append(p);
    json["permissions"] = perms;

    Json::Value endpoints(Json::arrayValue);
    for (const auto& e : client.allowedEndpoints) endpoints.append(e);
    json["allowed_endpoints"] = endpoints;

    Json::Value ips(Json::arrayValue);
    for (const auto& ip : client.allowedIps) ips.append(ip);
    json["allowed_ips"] = ips;

    json["rate_limit_per_minute"] = client.rateLimitPerMinute;
    json["rate_limit_per_hour"] = client.rateLimitPerHour;
    json["rate_limit_per_day"] = client.rateLimitPerDay;

    json["is_active"] = client.isActive;
    json["expires_at"] = client.expiresAt.value_or("");
    json["last_used_at"] = client.lastUsedAt.value_or("");
    json["total_requests"] = static_cast<Json::Int64>(client.totalRequests);

    json["created_by"] = client.createdBy.value_or("");
    json["created_at"] = client.createdAt;
    json["updated_at"] = client.updatedAt;

    return json;
}

} // namespace handlers

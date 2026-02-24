#pragma once

/**
 * @file api_client_handler.h
 * @brief HTTP handler for API Client management endpoints (Admin only)
 *
 * Provides CRUD operations for external API client management.
 * All endpoints require JWT admin authentication.
 */

#include <drogon/drogon.h>
#include "../auth/jwt_service.h"
#include "../repositories/api_client_repository.h"

namespace handlers {

class ApiClientHandler {
public:
    explicit ApiClientHandler(repositories::ApiClientRepository* repository);
    ~ApiClientHandler();

    void registerRoutes(drogon::HttpAppFramework& app);

private:
    repositories::ApiClientRepository* repository_;

    /** POST /api/auth/api-clients — Create new client + generate API key */
    void handleCreate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** GET /api/auth/api-clients — List all clients */
    void handleGetAll(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** GET /api/auth/api-clients/{id} — Get client detail */
    void handleGetById(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** PUT /api/auth/api-clients/{id} — Update client */
    void handleUpdate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** DELETE /api/auth/api-clients/{id} — Deactivate client */
    void handleDelete(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** POST /api/auth/api-clients/{id}/regenerate — Regenerate API key */
    void handleRegenerate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** GET /api/auth/api-clients/{id}/usage — Usage statistics */
    void handleGetUsage(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** Helper: require admin JWT and return claims */
    std::optional<auth::JwtClaims> requireAdmin(const drogon::HttpRequestPtr& req);
    std::optional<auth::JwtClaims> validateRequestToken(const drogon::HttpRequestPtr& req);

    /** Helper: convert ApiClient to JSON (without hash) */
    Json::Value modelToJson(const domain::models::ApiClient& client);

    std::shared_ptr<auth::JwtService> jwtService_;
};

} // namespace handlers

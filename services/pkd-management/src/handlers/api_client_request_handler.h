#pragma once

/**
 * @file api_client_request_handler.h
 * @brief HTTP handler for API Client Request endpoints (public + admin approval)
 *
 * Public: POST (submit request), GET by ID (check status)
 * Admin: GET all, POST approve/reject
 */

#include <drogon/drogon.h>
#include "../auth/jwt_service.h"
#include "../repositories/api_client_request_repository.h"
#include "../repositories/api_client_repository.h"
#include "i_query_executor.h"

namespace handlers {

class ApiClientRequestHandler {
public:
    ApiClientRequestHandler(repositories::ApiClientRequestRepository* requestRepo,
                            repositories::ApiClientRepository* clientRepo,
                            common::IQueryExecutor* queryExecutor);
    ~ApiClientRequestHandler();

    void registerRoutes(drogon::HttpAppFramework& app);

private:
    repositories::ApiClientRequestRepository* requestRepo_;
    repositories::ApiClientRepository* clientRepo_;

    /** POST /api/auth/api-client-requests — Submit new request (public) */
    void handleSubmit(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** GET /api/auth/api-client-requests — List all requests (admin) */
    void handleGetAll(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** GET /api/auth/api-client-requests/{id} — Get request detail (public: check own status) */
    void handleGetById(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** POST /api/auth/api-client-requests/{id}/approve — Approve request (admin) */
    void handleApprove(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** POST /api/auth/api-client-requests/{id}/reject — Reject request (admin) */
    void handleReject(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** Helper: require admin JWT */
    std::optional<auth::JwtClaims> requireAdmin(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>& callback);
    std::optional<auth::JwtClaims> validateRequestToken(const drogon::HttpRequestPtr& req);

    /** Helper: convert model to JSON (full detail for admin) */
    Json::Value modelToJson(const domain::models::ApiClientRequest& request);
    /** Helper: convert model to JSON with PII masked (for public endpoint) */
    Json::Value modelToMaskedJson(const domain::models::ApiClientRequest& request);

    std::shared_ptr<auth::JwtService> jwtService_;
    common::IQueryExecutor* queryExecutor_;
};

} // namespace handlers

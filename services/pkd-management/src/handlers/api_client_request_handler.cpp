/** @file api_client_request_handler.cpp
 *  @brief ApiClientRequestHandler implementation — request-approval workflow
 */

#include "api_client_request_handler.h"
#include "handler_utils.h"
#include "../auth/api_key_generator.h"
#include <icao/audit/audit_log.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <chrono>

using namespace drogon;

namespace handlers {

ApiClientRequestHandler::ApiClientRequestHandler(
    repositories::ApiClientRequestRepository* requestRepo,
    repositories::ApiClientRepository* clientRepo,
    common::IQueryExecutor* queryExecutor)
    : requestRepo_(requestRepo), clientRepo_(clientRepo), queryExecutor_(queryExecutor)
{
    const char* jwtSecret = std::getenv("JWT_SECRET_KEY");
    const char* jwtIssuer = std::getenv("JWT_ISSUER");
    const char* jwtExpirationStr = std::getenv("JWT_EXPIRATION_SECONDS");
    int jwtExpiration = jwtExpirationStr ? std::atoi(jwtExpirationStr) : 3600;

    if (jwtSecret && strlen(jwtSecret) >= 32) {
        jwtService_ = std::make_shared<auth::JwtService>(
            jwtSecret, jwtIssuer ? jwtIssuer : "icao-pkd", jwtExpiration);
    }

    spdlog::info("[ApiClientRequestHandler] Initialized");
}

ApiClientRequestHandler::~ApiClientRequestHandler() = default;

void ApiClientRequestHandler::registerRoutes(HttpAppFramework& app) {
    spdlog::info("[ApiClientRequestHandler] Registering routes");

    // POST /api/auth/api-client-requests — Submit (public)
    app.registerHandler(
        "/api/auth/api-client-requests",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleSubmit(req, std::move(callback));
        },
        {Post});

    // GET /api/auth/api-client-requests — List all (admin)
    app.registerHandler(
        "/api/auth/api-client-requests",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleGetAll(req, std::move(callback));
        },
        {Get});

    // GET /api/auth/api-client-requests/{id} — Detail (public: check status)
    app.registerHandler(
        "/api/auth/api-client-requests/{id}",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleGetById(req, std::move(callback), id);
        },
        {Get});

    // POST /api/auth/api-client-requests/{id}/approve — Approve (admin)
    app.registerHandler(
        "/api/auth/api-client-requests/{id}/approve",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleApprove(req, std::move(callback), id);
        },
        {Post});

    // POST /api/auth/api-client-requests/{id}/reject — Reject (admin)
    app.registerHandler(
        "/api/auth/api-client-requests/{id}/reject",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleReject(req, std::move(callback), id);
        },
        {Post});

    spdlog::info("[ApiClientRequestHandler] Routes registered: 5 endpoints on /api/auth/api-client-requests");
}

// ============================================================================
// POST /api/auth/api-client-requests — Submit new request (public, no auth)
// ============================================================================
void ApiClientRequestHandler::handleSubmit(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    try {
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

        // Validate required fields
        const auto& b = *body;
        if (!b.isMember("requester_name") || b["requester_name"].asString().empty() ||
            !b.isMember("requester_org") || b["requester_org"].asString().empty() ||
            !b.isMember("requester_contact_email") || b["requester_contact_email"].asString().empty() ||
            !b.isMember("request_reason") || b["request_reason"].asString().empty() ||
            !b.isMember("client_name") || b["client_name"].asString().empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "requester_name, requester_org, requester_contact_email, request_reason, client_name are required";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k400BadRequest);
            callback(response);
            return;
        }

        // Input length validation
        if (b["requester_name"].asString().size() > 255 ||
            b["requester_org"].asString().size() > 255 ||
            b["requester_contact_email"].asString().size() > 255 ||
            b["request_reason"].asString().size() > 4000 ||
            b["client_name"].asString().size() > 255) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Input exceeds maximum length";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k400BadRequest);
            callback(response);
            return;
        }

        domain::models::ApiClientRequest request;
        request.requesterName = b["requester_name"].asString();
        request.requesterOrg = b["requester_org"].asString();
        request.requesterContactEmail = b["requester_contact_email"].asString();
        request.requestReason = b["request_reason"].asString();
        request.clientName = b["client_name"].asString();

        if (b.isMember("requester_contact_phone") && !b["requester_contact_phone"].asString().empty()) {
            request.requesterContactPhone = b["requester_contact_phone"].asString();
        }
        if (b.isMember("description")) {
            request.description = b["description"].asString();
        }

        // Device type (SERVER, DESKTOP, MOBILE, OTHER)
        if (b.isMember("device_type") && !b["device_type"].asString().empty()) {
            std::string dt = b["device_type"].asString();
            if (dt == "SERVER" || dt == "DESKTOP" || dt == "MOBILE" || dt == "OTHER") {
                request.deviceType = dt;
            }
        }

        // Parse permissions
        if (b.isMember("permissions") && b["permissions"].isArray()) {
            for (const auto& p : b["permissions"]) {
                request.permissions.push_back(p.asString());
            }
        }
        // Parse allowed IPs (requester proposes, admin confirms at approval)
        if (b.isMember("allowed_ips") && b["allowed_ips"].isArray()) {
            for (const auto& ip : b["allowed_ips"]) {
                request.allowedIps.push_back(ip.asString());
            }
        }

        std::string id = requestRepo_->insert(request);
        if (id.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Failed to submit request";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k500InternalServerError);
            callback(response);
            return;
        }

        auto created = requestRepo_->findById(id);

        Json::Value resp;
        resp["success"] = true;
        resp["message"] = "API 클라이언트 등록 요청이 접수되었습니다. 관리자 승인 후 API Key가 발급됩니다.";
        resp["request_id"] = id;
        if (created) {
            resp["request"] = modelToJson(*created);
        }

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::API_CLIENT_CREATE);
        auditEntry.success = true;
        auditEntry.resourceId = id;
        auditEntry.resourceType = "API_CLIENT_REQUEST";
        Json::Value meta;
        meta["action"] = "SUBMIT";
        meta["requester_name"] = request.requesterName;
        meta["requester_org"] = request.requesterOrg;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        auditEntry.metadata = Json::writeString(wb, meta);
        try { icao::audit::logOperation(queryExecutor_, auditEntry); } catch (...) { /* non-critical */ }

        auto response = HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(k201Created);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestHandler] handleSubmit failed: {}", e.what());
        callback(common::handler::internalError("ApiClientRequestHandler::submit", e));
    }
}

// ============================================================================
// GET /api/auth/api-client-requests — List all (admin only)
// ============================================================================
void ApiClientRequestHandler::handleGetAll(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        std::string statusFilter = req->getParameter("status");
        int limit = common::handler::safeStoi(req->getParameter("limit"), 100, 1, 1000);
        int offset = common::handler::safeStoi(req->getParameter("offset"), 0, 0, 100000);

        auto requests = requestRepo_->findAll(statusFilter, limit, offset);
        int total = requestRepo_->countAll(statusFilter);

        Json::Value resp;
        resp["success"] = true;
        resp["total"] = total;

        Json::Value items(Json::arrayValue);
        for (const auto& r : requests) {
            items.append(modelToJson(r));
        }
        resp["requests"] = items;

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("ApiClientRequestHandler::getAll", e));
    }
}

// ============================================================================
// GET /api/auth/api-client-requests/{id} — Detail (public: check status by ID)
// ============================================================================
void ApiClientRequestHandler::handleGetById(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto request = requestRepo_->findById(id);
        if (!request) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Request not found";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k404NotFound);
            callback(response);
            return;
        }

        Json::Value resp;
        resp["success"] = true;
        resp["request"] = modelToJson(*request);

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("ApiClientRequestHandler::getById", e));
    }
}

// ============================================================================
// POST /api/auth/api-client-requests/{id}/approve — Approve (admin)
// ============================================================================
void ApiClientRequestHandler::handleApprove(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        auto request = requestRepo_->findById(id);
        if (!request) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Request not found";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k404NotFound);
            callback(response);
            return;
        }

        if (request->status != "PENDING") {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Request is not in PENDING status (current: " + request->status + ")";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k400BadRequest);
            callback(response);
            return;
        }

        // Parse approval body: review_comment + admin-configured settings
        std::string reviewComment;
        int rateLimitPerMinute = 60;
        int rateLimitPerHour = 1000;
        int rateLimitPerDay = 10000;
        int requestedDays = 365;
        std::vector<std::string> allowedEndpoints;

        auto body = req->getJsonObject();
        if (body) {
            const auto& bdy = *body;
            if (bdy.isMember("review_comment")) reviewComment = bdy["review_comment"].asString();
            if (bdy.isMember("rate_limit_per_minute")) rateLimitPerMinute = bdy["rate_limit_per_minute"].asInt();
            if (bdy.isMember("rate_limit_per_hour")) rateLimitPerHour = bdy["rate_limit_per_hour"].asInt();
            if (bdy.isMember("rate_limit_per_day")) rateLimitPerDay = bdy["rate_limit_per_day"].asInt();
            if (bdy.isMember("requested_days")) requestedDays = bdy["requested_days"].asInt();
            if (bdy.isMember("allowed_endpoints") && bdy["allowed_endpoints"].isArray()) {
                for (const auto& e : bdy["allowed_endpoints"]) {
                    allowedEndpoints.push_back(e.asString());
                }
            }
        }

        // Clamp rate limits to sane ranges
        rateLimitPerMinute = std::max(1, std::min(10000, rateLimitPerMinute));
        rateLimitPerHour = std::max(1, std::min(100000, rateLimitPerHour));
        rateLimitPerDay = std::max(1, std::min(1000000, rateLimitPerDay));
        requestedDays = std::max(1, std::min(3650, requestedDays));

        // 1. Generate API key and create actual API client
        auto keyInfo = auth::generateApiKey();

        domain::models::ApiClient client;
        client.clientName = request->clientName;
        client.apiKeyHash = keyInfo.hash;
        client.apiKeyPrefix = keyInfo.prefix;
        client.description = request->description;
        client.permissions = request->permissions;
        client.allowedEndpoints = allowedEndpoints;          // Admin sets
        client.allowedIps = request->allowedIps;             // From requester proposal
        client.rateLimitPerMinute = rateLimitPerMinute;      // Admin sets
        client.rateLimitPerHour = rateLimitPerHour;          // Admin sets
        client.rateLimitPerDay = rateLimitPerDay;            // Admin sets
        client.createdBy = admin->userId;

        // Calculate expiration date from admin-configured days
        if (requestedDays > 0) {
            auto now = std::chrono::system_clock::now();
            auto expiry = now + std::chrono::hours(24 * requestedDays);
            auto tt = std::chrono::system_clock::to_time_t(expiry);
            struct tm tmBuf;
            gmtime_r(&tt, &tmBuf);
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmBuf);
            client.expiresAt = std::string(buf);
        }

        std::string clientId = clientRepo_->insert(client);
        if (clientId.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Failed to create API client from request";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k500InternalServerError);
            callback(response);
            return;
        }

        // 2. Update request status to APPROVED
        bool updated = requestRepo_->updateStatus(
            id, "APPROVED", admin->userId, reviewComment, clientId);

        if (!updated) {
            spdlog::warn("[ApiClientRequestHandler] Request {} approved but status update failed", id);
        }

        // 3. Fetch created client for response
        auto createdClient = clientRepo_->findById(clientId);

        Json::Value resp;
        resp["success"] = true;
        resp["message"] = "요청이 승인되었습니다. API Key가 발급되었습니다.";
        resp["warning"] = "API Key is only shown in this response. Store it securely.";
        if (createdClient) {
            Json::Value clientJson;
            clientJson["id"] = createdClient->id;
            clientJson["client_name"] = createdClient->clientName;
            clientJson["api_key_prefix"] = createdClient->apiKeyPrefix;
            clientJson["api_key"] = keyInfo.key;  // Only time raw key is shown
            resp["client"] = clientJson;
        }
        auto updatedRequest = requestRepo_->findById(id);
        if (updatedRequest) {
            resp["request"] = modelToJson(updatedRequest.value());
        }

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::API_CLIENT_CREATE);
        auditEntry.success = true;
        auditEntry.resourceId = id;
        auditEntry.resourceType = "API_CLIENT_REQUEST";
        Json::Value meta;
        meta["action"] = "APPROVE";
        meta["approved_client_id"] = clientId;
        meta["requester_name"] = request->requesterName;
        meta["requester_org"] = request->requesterOrg;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        auditEntry.metadata = Json::writeString(wb, meta);
        try { icao::audit::logOperation(queryExecutor_, auditEntry); } catch (...) { /* non-critical */ }

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestHandler] handleApprove failed: {}", e.what());
        callback(common::handler::internalError("ApiClientRequestHandler::approve", e));
    }
}

// ============================================================================
// POST /api/auth/api-client-requests/{id}/reject — Reject (admin)
// ============================================================================
void ApiClientRequestHandler::handleReject(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto admin = requireAdmin(req, callback);
        if (!admin) return;

        auto request = requestRepo_->findById(id);
        if (!request) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Request not found";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k404NotFound);
            callback(response);
            return;
        }

        if (request->status != "PENDING") {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "Request is not in PENDING status (current: " + request->status + ")";
            auto response = HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(k400BadRequest);
            callback(response);
            return;
        }

        std::string reviewComment;
        auto body = req->getJsonObject();
        if (body && (*body).isMember("review_comment")) {
            reviewComment = (*body)["review_comment"].asString();
        }

        bool updated = requestRepo_->updateStatus(id, "REJECTED", admin->userId, reviewComment);

        Json::Value resp;
        resp["success"] = updated;
        resp["message"] = updated ? "요청이 거절되었습니다." : "상태 업데이트에 실패했습니다.";
        if (updated) {
            auto updatedReq = requestRepo_->findById(id);
            if (updatedReq) resp["request"] = modelToJson(*updatedReq);
        }

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::API_CLIENT_DELETE);
        auditEntry.success = updated;
        auditEntry.resourceId = id;
        auditEntry.resourceType = "API_CLIENT_REQUEST";
        Json::Value meta;
        meta["action"] = "REJECT";
        meta["requester_name"] = request->requesterName;
        meta["review_comment"] = reviewComment;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        auditEntry.metadata = Json::writeString(wb, meta);
        try { icao::audit::logOperation(queryExecutor_, auditEntry); } catch (...) { /* non-critical */ }

        auto response = HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestHandler] handleReject failed: {}", e.what());
        callback(common::handler::internalError("ApiClientRequestHandler::reject", e));
    }
}

// ============================================================================
// Helpers
// ============================================================================
std::optional<auth::JwtClaims> ApiClientRequestHandler::validateRequestToken(
    const drogon::HttpRequestPtr& req) {
    if (!jwtService_) return std::nullopt;
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") return std::nullopt;
    return jwtService_->validateToken(authHeader.substr(7));
}

std::optional<auth::JwtClaims> ApiClientRequestHandler::requireAdmin(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>& callback) {

    auto claims = validateRequestToken(req);
    if (!claims) {
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

Json::Value ApiClientRequestHandler::modelToJson(const domain::models::ApiClientRequest& r) {
    Json::Value json;
    json["id"] = r.id;
    json["requester_name"] = r.requesterName;
    json["requester_org"] = r.requesterOrg;
    json["requester_contact_phone"] = r.requesterContactPhone.value_or("");
    json["requester_contact_email"] = r.requesterContactEmail;
    json["request_reason"] = r.requestReason;
    json["client_name"] = r.clientName;
    json["description"] = r.description.value_or("");
    json["device_type"] = r.deviceType;

    Json::Value perms(Json::arrayValue);
    for (const auto& p : r.permissions) perms.append(p);
    json["permissions"] = perms;

    Json::Value ips(Json::arrayValue);
    for (const auto& ip : r.allowedIps) ips.append(ip);
    json["allowed_ips"] = ips;

    json["status"] = r.status;
    json["reviewed_by"] = r.reviewedBy.value_or("");
    json["reviewed_at"] = r.reviewedAt.value_or("");
    json["review_comment"] = r.reviewComment.value_or("");
    json["approved_client_id"] = r.approvedClientId.value_or("");

    json["created_at"] = r.createdAt;
    json["updated_at"] = r.updatedAt;

    return json;
}

} // namespace handlers

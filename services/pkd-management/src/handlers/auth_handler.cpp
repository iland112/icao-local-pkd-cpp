#include "auth_handler.h"
#include <spdlog/spdlog.h>
#include <json/json.h>
#include <numeric>
#include <sstream>

namespace handlers {

AuthHandler::AuthHandler(const std::string& dbConnInfo)
    : dbConnInfo_(dbConnInfo) {

    // Load JWT configuration from environment
    const char* jwtSecret = std::getenv("JWT_SECRET_KEY");
    if (!jwtSecret || strlen(jwtSecret) < 32) {
        throw std::runtime_error(
            "JWT_SECRET_KEY environment variable not set or too short (min 32 chars)");
    }

    const char* jwtIssuer = std::getenv("JWT_ISSUER");
    const char* jwtExpirationStr = std::getenv("JWT_EXPIRATION_SECONDS");

    int jwtExpiration = 3600; // Default: 1 hour
    if (jwtExpirationStr) {
        jwtExpiration = std::atoi(jwtExpirationStr);
    }

    jwtService_ = std::make_shared<auth::JwtService>(
        jwtSecret,
        jwtIssuer ? jwtIssuer : "icao-pkd",
        jwtExpiration
    );

    spdlog::info("[AuthHandler] Initialized");
}

void AuthHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // POST /api/auth/login
    app.registerHandler(
        "/api/auth/login",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLogin(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/auth/logout (requires authentication)
    app.registerHandler(
        "/api/auth/logout",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLogout(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/auth/refresh (requires authentication)
    app.registerHandler(
        "/api/auth/refresh",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleRefresh(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/auth/me (requires authentication)
    app.registerHandler(
        "/api/auth/me",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleMe(req, std::move(callback));
        },
        {drogon::Get}
    );

    // ========================================================================
    // User Management Routes (Admin only)
    // ========================================================================

    // GET /api/auth/users - List users
    app.registerHandler(
        "/api/auth/users",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleListUsers(req, std::move(callback));
        },
        {drogon::Get}
    );

    // POST /api/auth/users - Create user
    app.registerHandler(
        "/api/auth/users",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleCreateUser(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/auth/users/{userId} - Get user by ID
    app.registerHandler(
        "/api/auth/users/{userId}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& userId) {
            handleGetUser(req, std::move(callback), userId);
        },
        {drogon::Get}
    );

    // PUT /api/auth/users/{userId} - Update user
    app.registerHandler(
        "/api/auth/users/{userId}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& userId) {
            handleUpdateUser(req, std::move(callback), userId);
        },
        {drogon::Put}
    );

    // DELETE /api/auth/users/{userId} - Delete user
    app.registerHandler(
        "/api/auth/users/{userId}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& userId) {
            handleDeleteUser(req, std::move(callback), userId);
        },
        {drogon::Delete}
    );

    // PUT /api/auth/users/{userId}/password - Change password
    app.registerHandler(
        "/api/auth/users/{userId}/password",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& userId) {
            handleChangePassword(req, std::move(callback), userId);
        },
        {drogon::Put}
    );

    // GET /api/auth/audit-log - Get audit logs (admin only)
    app.registerHandler(
        "/api/auth/audit-log",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetAuditLog(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/auth/audit-log/stats - Get audit statistics (admin only)
    app.registerHandler(
        "/api/auth/audit-log/stats",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetAuditStats(req, std::move(callback));
        },
        {drogon::Get}
    );

    spdlog::info("[AuthHandler] Routes registered: /api/auth/login, /api/auth/logout, "
                 "/api/auth/refresh, /api/auth/me, /api/auth/users (CRUD + password), "
                 "/api/auth/audit-log (logs + stats)");
}

void AuthHandler::handleLogin(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        // Parse request body
        auto json = req->getJsonObject();
        if (!json) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid JSON body";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        std::string username = (*json).get("username", "").asString();
        std::string password = (*json).get("password", "").asString();

        if (username.empty() || password.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Missing username or password";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        spdlog::info("[AuthHandler] Login attempt: username={}, ip={}",
                     username, req->peerAddr().toIp());

        // Connect to database
        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            spdlog::error("[AuthHandler] Database connection failed: {}",
                          PQerrorMessage(conn));
            PQfinish(conn);

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Internal server error";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k500InternalServerError);
            callback(response);
            return;
        }

        // Query user from database (parameterized)
        const char* query =
            "SELECT id, password_hash, email, full_name, permissions, is_admin "
            "FROM users WHERE username = $1 AND is_active = true";

        const char* paramValues[1] = {username.c_str()};
        PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            spdlog::error("[AuthHandler] Query failed: {}", PQerrorMessage(conn));
            PQclear(res);
            PQfinish(conn);

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Internal server error";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k500InternalServerError);
            callback(response);
            return;
        }

        // User not found or inactive
        if (PQntuples(res) == 0) {
            PQclear(res);
            PQfinish(conn);

            logAuthEvent("", username, "LOGIN_FAILED", false,
                         req->peerAddr().toIp(),
                         req->getHeader("User-Agent"),
                         "User not found or inactive");

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid credentials";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        // Extract user data
        std::string userId = PQgetvalue(res, 0, 0);
        std::string passwordHash = PQgetvalue(res, 0, 1);
        std::string email = PQgetvalue(res, 0, 2) ? PQgetvalue(res, 0, 2) : "";
        std::string fullName = PQgetvalue(res, 0, 3) ? PQgetvalue(res, 0, 3) : "";
        std::string permissionsJson = PQgetvalue(res, 0, 4);
        bool isAdmin = strcmp(PQgetvalue(res, 0, 5), "t") == 0;

        PQclear(res);
        PQfinish(conn);

        // Verify password
        if (!auth::verifyPassword(password, passwordHash)) {
            logAuthEvent(userId, username, "LOGIN_FAILED", false,
                         req->peerAddr().toIp(),
                         req->getHeader("User-Agent"),
                         "Invalid password");

            spdlog::warn("[AuthHandler] Login failed: username={}, reason=invalid_password",
                         username);

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid credentials";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        // Parse permissions from JSON
        std::vector<std::string> permissions;
        try {
            Json::Value permsJson;
            Json::CharReaderBuilder reader;
            std::istringstream iss(permissionsJson);
            std::string errs;

            if (Json::parseFromStream(reader, iss, &permsJson, &errs)) {
                if (permsJson.isArray()) {
                    for (const auto& perm : permsJson) {
                        if (perm.isString()) {
                            permissions.push_back(perm.asString());
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("[AuthHandler] Failed to parse permissions: {}", e.what());
        }

        // Generate JWT token
        std::string token = jwtService_->generateToken(userId, username, permissions, isAdmin);

        // Update last_login_at
        updateLastLogin(userId);

        // Log successful login
        logAuthEvent(userId, username, "LOGIN_SUCCESS", true,
                     req->peerAddr().toIp(),
                     req->getHeader("User-Agent"));

        spdlog::info("[AuthHandler] Login successful: username={}, userId={}",
                     username, userId);

        // Build response
        Json::Value resp;
        resp["success"] = true;
        resp["access_token"] = token;
        resp["token_type"] = "Bearer";
        resp["expires_in"] = 3600; // TODO: Get from jwtService

        Json::Value userJson;
        userJson["id"] = userId;
        userJson["username"] = username;
        userJson["email"] = email;
        userJson["full_name"] = fullName;
        userJson["is_admin"] = isAdmin;

        Json::Value permsArray(Json::arrayValue);
        for (const auto& perm : permissions) {
            permsArray.append(perm);
        }
        userJson["permissions"] = permsArray;

        resp["user"] = userJson;

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Login error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleLogout(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        // Validate JWT token and get claims
        auto claims = validateRequestToken(req);
        if (!claims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Unauthorized";
            resp["message"] = "Invalid or missing authentication token";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        // Get user from claims
        std::string username = claims->username;
        std::string userId = claims->userId;

        // Log logout event
        logAuthEvent(userId, username, "LOGOUT", true,
                     req->peerAddr().toIp(),
                     req->getHeader("User-Agent"));

        spdlog::info("[AuthHandler] Logout: username={}", username);

        Json::Value resp;
        resp["success"] = true;
        resp["message"] = "Logged out successfully";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Logout error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleRefresh(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        // Parse request body
        auto json = req->getJsonObject();
        if (!json) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid JSON body";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        std::string token = (*json).get("token", "").asString();

        if (token.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Missing token";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        // Refresh token
        std::string newToken = jwtService_->refreshToken(token);

        if (newToken.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid or expired token";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        // Get username from old token for logging
        auto claims = jwtService_->validateToken(token);
        if (claims) {
            logAuthEvent(claims->userId, claims->username, "TOKEN_REFRESH", true,
                         req->peerAddr().toIp(),
                         req->getHeader("User-Agent"));

            spdlog::info("[AuthHandler] Token refreshed: username={}", claims->username);
        }

        Json::Value resp;
        resp["success"] = true;
        resp["access_token"] = newToken;
        resp["token_type"] = "Bearer";
        resp["expires_in"] = 3600; // TODO: Get from jwtService

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Refresh error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleMe(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        spdlog::debug("[AuthHandler] handleMe: START");

        // Validate JWT token and get claims
        auto claims = validateRequestToken(req);
        if (!claims) {
            spdlog::debug("[AuthHandler] handleMe: Token validation failed");
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Unauthorized";
            resp["message"] = "Invalid or missing authentication token";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        spdlog::debug("[AuthHandler] handleMe: Token validated, building response");

        Json::Value resp;
        resp["success"] = true;

        spdlog::debug("[AuthHandler] handleMe: Creating userJson");
        Json::Value userJson;
        userJson["id"] = claims->userId;
        userJson["username"] = claims->username;
        userJson["is_admin"] = claims->isAdmin;

        spdlog::debug("[AuthHandler] handleMe: Building permissions array");
        Json::Value permsArray(Json::arrayValue);
        for (const auto& perm : claims->permissions) {
            permsArray.append(perm);
        }
        userJson["permissions"] = permsArray;

        spdlog::debug("[AuthHandler] handleMe: Setting user in response");
        resp["user"] = userJson;

        spdlog::debug("[AuthHandler] handleMe: Creating HTTP response");
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);

        spdlog::debug("[AuthHandler] handleMe: Calling callback");
        callback(response);

        spdlog::debug("[AuthHandler] handleMe: DONE");

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Me error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::updateLastLogin(const std::string& userId) {
    PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[AuthHandler] Database connection failed (updateLastLogin): {}",
                      PQerrorMessage(conn));
        PQfinish(conn);
        return;
    }

    const char* query = "UPDATE users SET last_login_at = NOW() WHERE id = $1";
    const char* paramValues[1] = {userId.c_str()};

    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[AuthHandler] Failed to update last_login_at: {}",
                      PQerrorMessage(conn));
    }

    PQclear(res);
    PQfinish(conn);
}

void AuthHandler::logAuthEvent(
    const std::string& userId,
    const std::string& username,
    const std::string& eventType,
    bool success,
    const std::string& ipAddress,
    const std::string& userAgent,
    const std::string& errorMessage) {

    PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[AuthHandler] Database connection failed (logAuthEvent): {}",
                      PQerrorMessage(conn));
        PQfinish(conn);
        return;
    }

    const char* query =
        "INSERT INTO auth_audit_log "
        "(user_id, username, event_type, success, ip_address, user_agent, error_message) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)";

    const char* successStr = success ? "true" : "false";
    const char* paramValues[7] = {
        userId.empty() ? nullptr : userId.c_str(),
        username.empty() ? nullptr : username.c_str(),
        eventType.c_str(),
        successStr,
        ipAddress.c_str(),
        userAgent.c_str(),
        errorMessage.empty() ? nullptr : errorMessage.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 7, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[AuthHandler] Failed to insert audit log: {}",
                      PQerrorMessage(conn));
    }

    PQclear(res);
    PQfinish(conn);
}

std::optional<auth::JwtClaims> AuthHandler::validateRequestToken(
    const drogon::HttpRequestPtr& req) {

    spdlog::debug("[AuthHandler] validateRequestToken: START");

    // Extract Authorization header
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty()) {
        spdlog::warn("[AuthHandler] Missing Authorization header");
        return std::nullopt;
    }

    spdlog::debug("[AuthHandler] validateRequestToken: Got authorization header");

    // Validate Bearer token format
    if (authHeader.substr(0, 7) != "Bearer ") {
        spdlog::warn("[AuthHandler] Invalid Authorization header format");
        return std::nullopt;
    }

    spdlog::debug("[AuthHandler] validateRequestToken: Bearer format valid");

    // Extract and validate JWT
    std::string token = authHeader.substr(7);
    auto claims = jwtService_->validateToken(token);

    if (!claims) {
        spdlog::warn("[AuthHandler] Invalid or expired token");
        return std::nullopt;
    }

    spdlog::debug("[AuthHandler] validateRequestToken: JWT validated, user={}", claims->username);

    // NOTE: We don't store claims in session because:
    // 1. Drogon's session->insert() causes crashes with complex types
    // 2. We return claims directly, so handlers can access them
    // 3. JWT is stateless - no need for server-side session storage

    spdlog::debug("[AuthHandler] validateRequestToken: DONE, user={}", claims->username);
    return claims;
}


std::optional<auth::JwtClaims> AuthHandler::requireAdmin(
    const drogon::HttpRequestPtr& req) {

    auto claims = validateRequestToken(req);
    if (!claims) {
        return std::nullopt;
    }

    // Check if user is admin
    if (!claims->isAdmin) {
        spdlog::warn("[AuthHandler] Non-admin user {} attempted admin operation",
                     claims->username);
        return std::nullopt;
    }

    return claims;
}

// ============================================================================
// User Management Endpoints
// ============================================================================

void AuthHandler::handleListUsers(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        // Require admin privileges
        auto adminClaims = requireAdmin(req);
        if (!adminClaims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Admin privileges required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        // Parse query parameters
        auto params = req->getParameters();
        int limit = 50;
        int offset = 0;
        std::string search;
        std::string isActiveFilter;

        if (params.find("limit") != params.end()) {
            limit = std::stoi(params.at("limit"));
            if (limit > 100) limit = 100; // Max limit
        }
        if (params.find("offset") != params.end()) {
            offset = std::stoi(params.at("offset"));
        }
        if (params.find("search") != params.end()) {
            search = params.at("search");
        }
        if (params.find("is_active") != params.end()) {
            isActiveFilter = params.at("is_active");
        }

        // Database query
        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        // Build query with filters
        std::string query = "SELECT id, username, email, full_name, is_admin, is_active, "
                           "permissions, created_at, last_login_at, updated_at "
                           "FROM users WHERE 1=1";

        std::vector<std::string> paramValues;
        int paramIndex = 1;

        if (!search.empty()) {
            query += " AND (username ILIKE $" + std::to_string(paramIndex) +
                    " OR email ILIKE $" + std::to_string(paramIndex) +
                    " OR full_name ILIKE $" + std::to_string(paramIndex) + ")";
            paramValues.push_back("%" + search + "%");
            paramIndex++;
        }

        if (!isActiveFilter.empty()) {
            query += " AND is_active = $" + std::to_string(paramIndex);
            paramValues.push_back(isActiveFilter == "true" ? "true" : "false");
            paramIndex++;
        }

        query += " ORDER BY created_at DESC";
        query += " LIMIT $" + std::to_string(paramIndex);
        paramValues.push_back(std::to_string(limit));
        paramIndex++;

        query += " OFFSET $" + std::to_string(paramIndex);
        paramValues.push_back(std::to_string(offset));

        // Convert to C-style array
        std::vector<const char*> paramPtrs;
        for (const auto& p : paramValues) {
            paramPtrs.push_back(p.c_str());
        }

        PGresult* res = PQexecParams(conn, query.c_str(), paramPtrs.size(), nullptr,
                                     paramPtrs.data(), nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQfinish(conn);
            throw std::runtime_error(std::string("Query failed: ") + PQerrorMessage(conn));
        }

        int rows = PQntuples(res);

        // Build response
        Json::Value usersArray(Json::arrayValue);
        for (int i = 0; i < rows; i++) {
            Json::Value userObj;
            userObj["id"] = PQgetvalue(res, i, 0);
            userObj["username"] = PQgetvalue(res, i, 1);
            userObj["email"] = PQgetvalue(res, i, 2) ? PQgetvalue(res, i, 2) : "";
            userObj["full_name"] = PQgetvalue(res, i, 3) ? PQgetvalue(res, i, 3) : "";
            userObj["is_admin"] = strcmp(PQgetvalue(res, i, 4), "t") == 0;
            userObj["is_active"] = strcmp(PQgetvalue(res, i, 5), "t") == 0;

            // Parse permissions JSON
            std::string permsJson = PQgetvalue(res, i, 6);
            Json::Value permsValue;
            Json::CharReaderBuilder builder;
            std::istringstream permsStream(permsJson);
            std::string errs;
            if (Json::parseFromStream(builder, permsStream, &permsValue, &errs)) {
                userObj["permissions"] = permsValue;
            } else {
                userObj["permissions"] = Json::Value(Json::arrayValue);
            }

            userObj["created_at"] = PQgetvalue(res, i, 7);
            userObj["last_login_at"] = PQgetvalue(res, i, 8) ? PQgetvalue(res, i, 8) : "";
            userObj["updated_at"] = PQgetvalue(res, i, 9);

            usersArray.append(userObj);
        }

        PQclear(res);

        // Get total count (without pagination)
        std::string countQuery = "SELECT COUNT(*) FROM users WHERE 1=1";
        if (!search.empty()) {
            countQuery += " AND (username ILIKE $1 OR email ILIKE $1 OR full_name ILIKE $1)";
        }
        if (!isActiveFilter.empty()) {
            int idx = search.empty() ? 1 : 2;
            countQuery += " AND is_active = $" + std::to_string(idx);
        }

        std::vector<const char*> countParams;
        if (!search.empty()) {
            std::string searchPattern = "%" + search + "%";
            countParams.push_back(searchPattern.c_str());
        }
        if (!isActiveFilter.empty()) {
            countParams.push_back(isActiveFilter == "true" ? "true" : "false");
        }

        PGresult* countRes = PQexecParams(conn, countQuery.c_str(), countParams.size(),
                                          nullptr, countParams.data(), nullptr, nullptr, 0);

        int total = 0;
        if (PQresultStatus(countRes) == PGRES_TUPLES_OK && PQntuples(countRes) > 0) {
            total = std::stoi(PQgetvalue(countRes, 0, 0));
        }

        PQclear(countRes);
        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["total"] = total;
        resp["users"] = usersArray;

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] List users error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleGetUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId) {

    try {
        // Require admin privileges
        auto adminClaims = requireAdmin(req);
        if (!adminClaims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Admin privileges required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        const char* query = "SELECT id, username, email, full_name, is_admin, is_active, "
                           "permissions, created_at, last_login_at, updated_at "
                           "FROM users WHERE id = $1";
        const char* paramValues[1] = {userId.c_str()};

        PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQfinish(conn);
            throw std::runtime_error(std::string("Query failed: ") + PQerrorMessage(conn));
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            PQfinish(conn);

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not found";
            resp["message"] = "User not found";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }

        Json::Value userObj;
        userObj["id"] = PQgetvalue(res, 0, 0);
        userObj["username"] = PQgetvalue(res, 0, 1);
        userObj["email"] = PQgetvalue(res, 0, 2) ? PQgetvalue(res, 0, 2) : "";
        userObj["full_name"] = PQgetvalue(res, 0, 3) ? PQgetvalue(res, 0, 3) : "";
        userObj["is_admin"] = strcmp(PQgetvalue(res, 0, 4), "t") == 0;
        userObj["is_active"] = strcmp(PQgetvalue(res, 0, 5), "t") == 0;

        // Parse permissions JSON
        std::string permsJson = PQgetvalue(res, 0, 6);
        Json::Value permsValue;
        Json::CharReaderBuilder builder;
        std::istringstream permsStream(permsJson);
        std::string errs;
        if (Json::parseFromStream(builder, permsStream, &permsValue, &errs)) {
            userObj["permissions"] = permsValue;
        } else {
            userObj["permissions"] = Json::Value(Json::arrayValue);
        }

        userObj["created_at"] = PQgetvalue(res, 0, 7);
        userObj["last_login_at"] = PQgetvalue(res, 0, 8) ? PQgetvalue(res, 0, 8) : "";
        userObj["updated_at"] = PQgetvalue(res, 0, 9);

        PQclear(res);
        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["user"] = userObj;

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Get user error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleCreateUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        // Require admin privileges
        auto adminClaims = requireAdmin(req);
        if (!adminClaims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Admin privileges required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        // Parse request body
        auto json = req->getJsonObject();
        if (!json) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid JSON body";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        std::string username = (*json).get("username", "").asString();
        std::string password = (*json).get("password", "").asString();
        std::string email = (*json).get("email", "").asString();
        std::string fullName = (*json).get("full_name", "").asString();
        bool isAdmin = (*json).get("is_admin", false).asBool();

        if (username.empty() || password.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Missing required fields";
            resp["message"] = "Username and password are required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        // Hash password
        std::string passwordHash = auth::hashPassword(password);

        // Serialize permissions
        Json::Value permissions = (*json).get("permissions", Json::Value(Json::arrayValue));
        Json::StreamWriterBuilder writerBuilder;
        std::string permissionsJson = Json::writeString(writerBuilder, permissions);

        // Insert user
        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        const char* query =
            "INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions) "
            "VALUES ($1, $2, $3, $4, $5, $6::jsonb) "
            "RETURNING id, username, email, full_name, is_admin, is_active, permissions, created_at";

        const char* isAdminStr = isAdmin ? "true" : "false";
        const char* paramValues[6] = {
            username.c_str(),
            passwordHash.c_str(),
            email.empty() ? nullptr : email.c_str(),
            fullName.empty() ? nullptr : fullName.c_str(),
            isAdminStr,
            permissionsJson.c_str()
        };

        PGresult* res = PQexecParams(conn, query, 6, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn);
            PQclear(res);
            PQfinish(conn);

            // Check for duplicate username
            if (error.find("unique") != std::string::npos) {
                Json::Value resp;
                resp["success"] = false;
                resp["error"] = "Conflict";
                resp["message"] = "Username already exists";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k409Conflict);
                callback(response);
                return;
            }

            throw std::runtime_error(std::string("Insert failed: ") + error);
        }

        // Build response
        Json::Value userObj;
        userObj["id"] = PQgetvalue(res, 0, 0);
        userObj["username"] = PQgetvalue(res, 0, 1);
        userObj["email"] = PQgetvalue(res, 0, 2) ? PQgetvalue(res, 0, 2) : "";
        userObj["full_name"] = PQgetvalue(res, 0, 3) ? PQgetvalue(res, 0, 3) : "";
        userObj["is_admin"] = strcmp(PQgetvalue(res, 0, 4), "t") == 0;
        userObj["is_active"] = strcmp(PQgetvalue(res, 0, 5), "t") == 0;

        std::string permsJson = PQgetvalue(res, 0, 6);
        Json::Value permsValue;
        Json::CharReaderBuilder builder;
        std::istringstream permsStream(permsJson);
        std::string errs;
        if (Json::parseFromStream(builder, permsStream, &permsValue, &errs)) {
            userObj["permissions"] = permsValue;
        } else {
            userObj["permissions"] = Json::Value(Json::arrayValue);
        }

        userObj["created_at"] = PQgetvalue(res, 0, 7);

        PQclear(res);
        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["user"] = userObj;
        resp["message"] = "User created successfully";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k201Created);
        callback(response);

        spdlog::info("[AuthHandler] User created: {} by admin {}", username, adminClaims->username);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Create user error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleUpdateUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId) {

    try {
        // Require admin privileges
        auto adminClaims = requireAdmin(req);
        if (!adminClaims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Admin privileges required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        // Parse request body
        auto json = req->getJsonObject();
        if (!json) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid JSON body";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        // Build dynamic UPDATE query
        std::vector<std::string> setClauses;
        std::vector<std::string> paramValues;
        int paramIndex = 1;

        if (json->isMember("email")) {
            setClauses.push_back("email = $" + std::to_string(paramIndex++));
            paramValues.push_back((*json)["email"].asString());
        }
        if (json->isMember("full_name")) {
            setClauses.push_back("full_name = $" + std::to_string(paramIndex++));
            paramValues.push_back((*json)["full_name"].asString());
        }
        if (json->isMember("is_admin")) {
            setClauses.push_back("is_admin = $" + std::to_string(paramIndex++));
            paramValues.push_back((*json)["is_admin"].asBool() ? "true" : "false");
        }
        if (json->isMember("is_active")) {
            setClauses.push_back("is_active = $" + std::to_string(paramIndex++));
            paramValues.push_back((*json)["is_active"].asBool() ? "true" : "false");
        }
        if (json->isMember("permissions")) {
            setClauses.push_back("permissions = $" + std::to_string(paramIndex++) + "::jsonb");
            Json::StreamWriterBuilder writerBuilder;
            std::string permissionsJson = Json::writeString(writerBuilder, (*json)["permissions"]);
            paramValues.push_back(permissionsJson);
        }

        if (setClauses.empty()) {
            PQfinish(conn);
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "No fields to update";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        std::string query = "UPDATE users SET " +
                           std::accumulate(setClauses.begin(), setClauses.end(), std::string(),
                                          [](const std::string& a, const std::string& b) {
                                              return a.empty() ? b : a + ", " + b;
                                          }) +
                           " WHERE id = $" + std::to_string(paramIndex) +
                           " RETURNING id, username, email, full_name, is_admin, is_active, "
                           "permissions, created_at, updated_at";

        paramValues.push_back(userId);

        std::vector<const char*> paramPtrs;
        for (const auto& p : paramValues) {
            paramPtrs.push_back(p.c_str());
        }

        PGresult* res = PQexecParams(conn, query.c_str(), paramPtrs.size(), nullptr,
                                     paramPtrs.data(), nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQfinish(conn);
            throw std::runtime_error(std::string("Update failed: ") + PQerrorMessage(conn));
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            PQfinish(conn);

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not found";
            resp["message"] = "User not found";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }

        // Build response
        Json::Value userObj;
        userObj["id"] = PQgetvalue(res, 0, 0);
        userObj["username"] = PQgetvalue(res, 0, 1);
        userObj["email"] = PQgetvalue(res, 0, 2) ? PQgetvalue(res, 0, 2) : "";
        userObj["full_name"] = PQgetvalue(res, 0, 3) ? PQgetvalue(res, 0, 3) : "";
        userObj["is_admin"] = strcmp(PQgetvalue(res, 0, 4), "t") == 0;
        userObj["is_active"] = strcmp(PQgetvalue(res, 0, 5), "t") == 0;

        std::string permsJson = PQgetvalue(res, 0, 6);
        Json::Value permsValue;
        Json::CharReaderBuilder builder;
        std::istringstream permsStream(permsJson);
        std::string errs;
        if (Json::parseFromStream(builder, permsStream, &permsValue, &errs)) {
            userObj["permissions"] = permsValue;
        } else {
            userObj["permissions"] = Json::Value(Json::arrayValue);
        }

        userObj["created_at"] = PQgetvalue(res, 0, 7);
        userObj["updated_at"] = PQgetvalue(res, 0, 8);

        PQclear(res);
        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["user"] = userObj;
        resp["message"] = "User updated successfully";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

        spdlog::info("[AuthHandler] User {} updated by admin {}", userId, adminClaims->username);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Update user error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleDeleteUser(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId) {

    try {
        // Require admin privileges
        auto adminClaims = requireAdmin(req);
        if (!adminClaims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Admin privileges required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        // Prevent self-deletion
        if (userId == adminClaims->userId) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Cannot delete your own account";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        const char* query = "DELETE FROM users WHERE id = $1 RETURNING username";
        const char* paramValues[1] = {userId.c_str()};

        PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQfinish(conn);
            throw std::runtime_error(std::string("Delete failed: ") + PQerrorMessage(conn));
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            PQfinish(conn);

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not found";
            resp["message"] = "User not found";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }

        std::string deletedUsername = PQgetvalue(res, 0, 0);
        PQclear(res);
        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["message"] = "User deleted successfully";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

        spdlog::info("[AuthHandler] User {} deleted by admin {}", deletedUsername, adminClaims->username);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Delete user error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleChangePassword(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& userId) {

    try {
        // Validate token
        auto claims = validateRequestToken(req);
        if (!claims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Unauthorized";
            resp["message"] = "Invalid or missing authentication token";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        // Check authorization: admin or self
        bool isSelf = (userId == claims->userId);
        bool isAdmin = claims->isAdmin;

        if (!isSelf && !isAdmin) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "You can only change your own password unless you are an admin";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        // Parse request body
        auto json = req->getJsonObject();
        if (!json) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid JSON body";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        std::string newPassword = (*json).get("new_password", "").asString();
        if (newPassword.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Missing required field";
            resp["message"] = "new_password is required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        // If changing own password, verify current password
        if (isSelf) {
            std::string currentPassword = (*json).get("current_password", "").asString();
            if (currentPassword.empty()) {
                Json::Value resp;
                resp["success"] = false;
                resp["error"] = "Missing required field";
                resp["message"] = "current_password is required when changing your own password";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }

            // Verify current password
            PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                throw std::runtime_error(std::string("Database connection failed: ") +
                                         PQerrorMessage(conn));
            }

            const char* checkQuery = "SELECT password_hash FROM users WHERE id = $1";
            const char* checkParams[1] = {userId.c_str()};
            PGresult* checkRes = PQexecParams(conn, checkQuery, 1, nullptr, checkParams,
                                              nullptr, nullptr, 0);

            if (PQresultStatus(checkRes) != PGRES_TUPLES_OK || PQntuples(checkRes) == 0) {
                PQclear(checkRes);
                PQfinish(conn);
                Json::Value resp;
                resp["success"] = false;
                resp["error"] = "Not found";
                resp["message"] = "User not found";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k404NotFound);
                callback(response);
                return;
            }

            std::string storedHash = PQgetvalue(checkRes, 0, 0);
            PQclear(checkRes);

            if (!auth::verifyPassword(currentPassword, storedHash)) {
                PQfinish(conn);
                Json::Value resp;
                resp["success"] = false;
                resp["error"] = "Forbidden";
                resp["message"] = "Current password is incorrect";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k403Forbidden);
                callback(response);
                return;
            }

            PQfinish(conn);
        }

        // Hash new password
        std::string newPasswordHash = auth::hashPassword(newPassword);

        // Update password
        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        const char* updateQuery = "UPDATE users SET password_hash = $1 WHERE id = $2";
        const char* updateParams[2] = {newPasswordHash.c_str(), userId.c_str()};

        PGresult* updateRes = PQexecParams(conn, updateQuery, 2, nullptr, updateParams,
                                           nullptr, nullptr, 0);

        if (PQresultStatus(updateRes) != PGRES_COMMAND_OK) {
            PQclear(updateRes);
            PQfinish(conn);
            throw std::runtime_error(std::string("Update failed: ") + PQerrorMessage(conn));
        }

        PQclear(updateRes);
        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["message"] = "Password changed successfully";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

        spdlog::info("[AuthHandler] Password changed for user {}", userId);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Change password error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

// ============================================================================
// Audit Log Endpoints
// ============================================================================

void AuthHandler::handleGetAuditLog(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        // Require admin privileges
        auto adminClaims = requireAdmin(req);
        if (!adminClaims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Admin privileges required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        // Parse query parameters
        auto params = req->getParameters();
        int limit = 50;
        int offset = 0;
        std::string userId, username, eventType, successFilter, startDate, endDate;

        if (params.find("limit") != params.end()) {
            limit = std::stoi(params.at("limit"));
            if (limit > 200) limit = 200; // Max limit
        }
        if (params.find("offset") != params.end()) {
            offset = std::stoi(params.at("offset"));
        }
        if (params.find("user_id") != params.end()) {
            userId = params.at("user_id");
        }
        if (params.find("username") != params.end()) {
            username = params.at("username");
        }
        if (params.find("event_type") != params.end()) {
            eventType = params.at("event_type");
        }
        if (params.find("success") != params.end()) {
            successFilter = params.at("success");
        }
        if (params.find("start_date") != params.end()) {
            startDate = params.at("start_date");
        }
        if (params.find("end_date") != params.end()) {
            endDate = params.at("end_date");
        }

        // Database query
        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        // Build query with filters
        std::string query = "SELECT id, user_id, username, event_type, ip_address, "
                           "user_agent, success, error_message, created_at "
                           "FROM auth_audit_log WHERE 1=1";

        std::vector<std::string> paramValues;
        int paramIndex = 1;

        if (!userId.empty()) {
            query += " AND user_id = $" + std::to_string(paramIndex);
            paramValues.push_back(userId);
            paramIndex++;
        }

        if (!username.empty()) {
            query += " AND username ILIKE $" + std::to_string(paramIndex);
            paramValues.push_back("%" + username + "%");
            paramIndex++;
        }

        if (!eventType.empty()) {
            query += " AND event_type = $" + std::to_string(paramIndex);
            paramValues.push_back(eventType);
            paramIndex++;
        }

        if (!successFilter.empty()) {
            query += " AND success = $" + std::to_string(paramIndex);
            paramValues.push_back(successFilter == "true" ? "true" : "false");
            paramIndex++;
        }

        if (!startDate.empty()) {
            query += " AND created_at >= $" + std::to_string(paramIndex);
            paramValues.push_back(startDate);
            paramIndex++;
        }

        if (!endDate.empty()) {
            query += " AND created_at <= $" + std::to_string(paramIndex);
            paramValues.push_back(endDate);
            paramIndex++;
        }

        query += " ORDER BY created_at DESC";
        query += " LIMIT $" + std::to_string(paramIndex);
        paramValues.push_back(std::to_string(limit));
        paramIndex++;

        query += " OFFSET $" + std::to_string(paramIndex);
        paramValues.push_back(std::to_string(offset));

        // Convert to C-style array
        std::vector<const char*> paramPtrs;
        for (const auto& p : paramValues) {
            paramPtrs.push_back(p.c_str());
        }

        PGresult* res = PQexecParams(conn, query.c_str(), paramPtrs.size(), nullptr,
                                     paramPtrs.data(), nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQfinish(conn);
            throw std::runtime_error(std::string("Query failed: ") + PQerrorMessage(conn));
        }

        int rows = PQntuples(res);

        // Build response
        Json::Value logsArray(Json::arrayValue);
        for (int i = 0; i < rows; i++) {
            Json::Value logObj;
            logObj["id"] = PQgetvalue(res, i, 0);
            logObj["user_id"] = PQgetvalue(res, i, 1) ? PQgetvalue(res, i, 1) : "";
            logObj["username"] = PQgetvalue(res, i, 2) ? PQgetvalue(res, i, 2) : "";
            logObj["event_type"] = PQgetvalue(res, i, 3);
            logObj["ip_address"] = PQgetvalue(res, i, 4) ? PQgetvalue(res, i, 4) : "";
            logObj["user_agent"] = PQgetvalue(res, i, 5) ? PQgetvalue(res, i, 5) : "";
            logObj["success"] = strcmp(PQgetvalue(res, i, 6), "t") == 0;
            logObj["error_message"] = PQgetvalue(res, i, 7) ? PQgetvalue(res, i, 7) : "";
            logObj["created_at"] = PQgetvalue(res, i, 8);

            logsArray.append(logObj);
        }

        PQclear(res);

        // Get total count (without pagination)
        std::string countQuery = "SELECT COUNT(*) FROM auth_audit_log WHERE 1=1";
        std::vector<std::string> countParamValues;
        paramIndex = 1;

        if (!userId.empty()) {
            countQuery += " AND user_id = $" + std::to_string(paramIndex++);
            countParamValues.push_back(userId);
        }
        if (!username.empty()) {
            countQuery += " AND username ILIKE $" + std::to_string(paramIndex++);
            countParamValues.push_back("%" + username + "%");
        }
        if (!eventType.empty()) {
            countQuery += " AND event_type = $" + std::to_string(paramIndex++);
            countParamValues.push_back(eventType);
        }
        if (!successFilter.empty()) {
            countQuery += " AND success = $" + std::to_string(paramIndex++);
            countParamValues.push_back(successFilter == "true" ? "true" : "false");
        }
        if (!startDate.empty()) {
            countQuery += " AND created_at >= $" + std::to_string(paramIndex++);
            countParamValues.push_back(startDate);
        }
        if (!endDate.empty()) {
            countQuery += " AND created_at <= $" + std::to_string(paramIndex++);
            countParamValues.push_back(endDate);
        }

        std::vector<const char*> countParamPtrs;
        for (const auto& p : countParamValues) {
            countParamPtrs.push_back(p.c_str());
        }

        PGresult* countRes = PQexecParams(conn, countQuery.c_str(), countParamPtrs.size(),
                                          nullptr, countParamPtrs.data(), nullptr, nullptr, 0);

        int total = 0;
        if (PQresultStatus(countRes) == PGRES_TUPLES_OK && PQntuples(countRes) > 0) {
            total = std::stoi(PQgetvalue(countRes, 0, 0));
        }

        PQclear(countRes);
        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["total"] = total;
        resp["logs"] = logsArray;

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Get audit log error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

void AuthHandler::handleGetAuditStats(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    try {
        // Require admin privileges
        auto adminClaims = requireAdmin(req);
        if (!adminClaims) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Forbidden";
            resp["message"] = "Admin privileges required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            callback(response);
            return;
        }

        PGconn* conn = PQconnectdb(dbConnInfo_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error(std::string("Database connection failed: ") +
                                     PQerrorMessage(conn));
        }

        Json::Value stats;

        // Total events
        const char* totalQuery = "SELECT COUNT(*) FROM auth_audit_log";
        PGresult* totalRes = PQexec(conn, totalQuery);
        if (PQresultStatus(totalRes) == PGRES_TUPLES_OK && PQntuples(totalRes) > 0) {
            stats["total_events"] = std::stoi(PQgetvalue(totalRes, 0, 0));
        }
        PQclear(totalRes);

        // By event type
        const char* eventTypeQuery = "SELECT event_type, COUNT(*) FROM auth_audit_log "
                                     "GROUP BY event_type ORDER BY COUNT(*) DESC";
        PGresult* eventTypeRes = PQexec(conn, eventTypeQuery);
        Json::Value byEventType;
        if (PQresultStatus(eventTypeRes) == PGRES_TUPLES_OK) {
            for (int i = 0; i < PQntuples(eventTypeRes); i++) {
                std::string eventType = PQgetvalue(eventTypeRes, i, 0);
                int count = std::stoi(PQgetvalue(eventTypeRes, i, 1));
                byEventType[eventType] = count;
            }
        }
        stats["by_event_type"] = byEventType;
        PQclear(eventTypeRes);

        // By user (top 10)
        const char* userQuery = "SELECT username, COUNT(*) FROM auth_audit_log "
                               "WHERE username IS NOT NULL "
                               "GROUP BY username ORDER BY COUNT(*) DESC LIMIT 10";
        PGresult* userRes = PQexec(conn, userQuery);
        Json::Value byUser;
        if (PQresultStatus(userRes) == PGRES_TUPLES_OK) {
            for (int i = 0; i < PQntuples(userRes); i++) {
                std::string username = PQgetvalue(userRes, i, 0);
                int count = std::stoi(PQgetvalue(userRes, i, 1));
                byUser[username] = count;
            }
        }
        stats["by_user"] = byUser;
        PQclear(userRes);

        // Recent failed logins (last 24 hours)
        const char* failedQuery = "SELECT COUNT(*) FROM auth_audit_log "
                                 "WHERE event_type = 'LOGIN_FAILED' "
                                 "AND created_at >= NOW() - INTERVAL '24 hours'";
        PGresult* failedRes = PQexec(conn, failedQuery);
        if (PQresultStatus(failedRes) == PGRES_TUPLES_OK && PQntuples(failedRes) > 0) {
            stats["recent_failed_logins"] = std::stoi(PQgetvalue(failedRes, 0, 0));
        }
        PQclear(failedRes);

        // Last 24h events
        const char* last24hQuery = "SELECT COUNT(*) FROM auth_audit_log "
                                  "WHERE created_at >= NOW() - INTERVAL '24 hours'";
        PGresult* last24hRes = PQexec(conn, last24hQuery);
        if (PQresultStatus(last24hRes) == PGRES_TUPLES_OK && PQntuples(last24hRes) > 0) {
            stats["last_24h_events"] = std::stoi(PQgetvalue(last24hRes, 0, 0));
        }
        PQclear(last24hRes);

        PQfinish(conn);

        Json::Value resp;
        resp["success"] = true;
        resp["stats"] = stats;

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Get audit stats error: {}", e.what());

        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "Internal server error";
        resp["message"] = e.what();
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k500InternalServerError);
        callback(response);
    }
}

} // namespace handlers

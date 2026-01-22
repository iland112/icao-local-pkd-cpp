#include "auth_handler.h"
#include <spdlog/spdlog.h>
#include <json/json.h>

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

    // POST /api/auth/logout
    app.registerHandler(
        "/api/auth/logout",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLogout(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/auth/refresh
    app.registerHandler(
        "/api/auth/refresh",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleRefresh(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/auth/me
    app.registerHandler(
        "/api/auth/me",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleMe(req, std::move(callback));
        },
        {drogon::Get}
    );

    spdlog::info("[AuthHandler] Routes registered: /api/auth/login, /api/auth/logout, "
                 "/api/auth/refresh, /api/auth/me");
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
        // Get user from session (set by AuthMiddleware)
        auto session = req->getSession();
        std::string username = session->get<std::string>("username");
        std::string userId = session->get<std::string>("user_id");

        // Log logout event
        if (!username.empty()) {
            logAuthEvent(userId, username, "LOGOUT", true,
                         req->peerAddr().toIp(),
                         req->getHeader("User-Agent"));

            spdlog::info("[AuthHandler] Logout: username={}", username);
        }

        // Clear session (optional, token is deleted client-side)
        session->clear();

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
        // Get user from session (set by AuthMiddleware)
        auto session = req->getSession();
        std::string userId = session->get<std::string>("user_id");
        std::string username = session->get<std::string>("username");
        bool isAdmin = session->get<bool>("is_admin");
        std::string permsJson = session->get<std::string>("permissions");

        if (userId.empty() || username.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not authenticated";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        // Parse permissions
        std::vector<std::string> permissions;
        try {
            Json::Value permsJsonValue;
            Json::CharReaderBuilder reader;
            std::istringstream iss(permsJson);
            std::string errs;

            if (Json::parseFromStream(reader, iss, &permsJsonValue, &errs)) {
                if (permsJsonValue.isArray()) {
                    for (const auto& perm : permsJsonValue) {
                        if (perm.isString()) {
                            permissions.push_back(perm.asString());
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("[AuthHandler] Failed to parse permissions: {}", e.what());
        }

        Json::Value resp;
        resp["success"] = true;

        Json::Value userJson;
        userJson["id"] = userId;
        userJson["username"] = username;
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

} // namespace handlers

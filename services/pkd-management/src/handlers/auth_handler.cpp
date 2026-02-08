#include "auth_handler.h"
#include <spdlog/spdlog.h>
#include <json/json.h>
#include <numeric>
#include <sstream>

namespace handlers {

AuthHandler::AuthHandler(
    repositories::UserRepository* userRepository,
    repositories::AuthAuditRepository* authAuditRepository)
    : userRepository_(userRepository),
      authAuditRepository_(authAuditRepository) {

    if (!userRepository_ || !authAuditRepository_) {
        throw std::invalid_argument("AuthHandler: repositories cannot be nullptr");
    }

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

    spdlog::info("[AuthHandler] Initialized with Repository Pattern (Phase 5.4)");
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

        // Find user by username using Repository
        auto userOpt = userRepository_->findByUsername(username);

        if (!userOpt.has_value()) {
            // User not found
            authAuditRepository_->insert(
                std::nullopt, username, "LOGIN_FAILED", false,
                req->peerAddr().toIp(), req->getHeader("User-Agent"),
                "User not found or inactive"
            );

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid credentials";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        domain::User user = userOpt.value();

        // Check if user is active
        if (!user.isActive()) {
            authAuditRepository_->insert(
                user.getId(), username, "LOGIN_FAILED", false,
                req->peerAddr().toIp(), req->getHeader("User-Agent"),
                "User account is inactive"
            );

            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Invalid credentials";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k401Unauthorized);
            callback(response);
            return;
        }

        std::string userId = user.getId();
        std::string passwordHash = user.getPasswordHash();
        std::string email = user.getEmail().value_or("");
        std::string fullName = user.getFullName().value_or("");
        std::vector<std::string> permissions = user.getPermissions();
        bool isAdmin = user.isAdmin();

        // Verify password
        if (!auth::verifyPassword(password, passwordHash)) {
            authAuditRepository_->insert(
                userId, username, "LOGIN_FAILED", false,
                req->peerAddr().toIp(), req->getHeader("User-Agent"),
                "Invalid password"
            );

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

        // Generate JWT token
        std::string token = jwtService_->generateToken(userId, username, permissions, isAdmin);

        // Update last_login_at using Repository
        userRepository_->updateLastLogin(userId);

        // Log successful login using Repository
        authAuditRepository_->insert(
            userId, username, "LOGIN_SUCCESS", true,
            req->peerAddr().toIp(), req->getHeader("User-Agent"),
            std::nullopt
        );

        spdlog::info("[AuthHandler] Login successful: username={}, userId={}",
                     username, userId);

        // Build response
        Json::Value resp;
        resp["success"] = true;
        resp["access_token"] = token;
        resp["token_type"] = "Bearer";
        resp["expires_in"] = 3600; // 1 hour (matches JwtService default)

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
        resp["expires_in"] = 3600; // 1 hour (matches JwtService default)

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
    try {
        userRepository_->updateLastLogin(userId);
        spdlog::debug("[AuthHandler] Updated last_login_at for user: {}", userId);
    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Failed to update last_login_at: {}", e.what());
    }
}

void AuthHandler::logAuthEvent(
    const std::string& userId,
    const std::string& username,
    const std::string& eventType,
    bool success,
    const std::string& ipAddress,
    const std::string& userAgent,
    const std::string& errorMessage) {

    try {
        std::optional<std::string> userIdOpt = userId.empty() ? std::nullopt : std::make_optional(userId);
        std::optional<std::string> ipOpt = ipAddress.empty() ? std::nullopt : std::make_optional(ipAddress);
        std::optional<std::string> agentOpt = userAgent.empty() ? std::nullopt : std::make_optional(userAgent);
        std::optional<std::string> errorOpt = errorMessage.empty() ? std::nullopt : std::make_optional(errorMessage);

        authAuditRepository_->insert(userIdOpt, username, eventType, success, ipOpt, agentOpt, errorOpt);
        spdlog::debug("[AuthHandler] Logged auth event: {} for user {}", eventType, username);
    } catch (const std::exception& e) {
        spdlog::error("[AuthHandler] Failed to log auth event: {}", e.what());
    }
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

        // Use repository to get users
        Json::Value usersArray = userRepository_->findAll(limit, offset, search, isActiveFilter);
        int total = userRepository_->count(search, isActiveFilter);

        Json::Value resp;
        resp["success"] = true;
        resp["total"] = total;
        resp["data"] = usersArray;  // Consistent with API convention

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

        // Use repository to find user
        auto userOpt = userRepository_->findById(userId);
        if (!userOpt.has_value()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not found";
            resp["message"] = "User not found";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }

        domain::User user = userOpt.value();

        // Build JSON response from domain object
        Json::Value userObj;
        userObj["id"] = user.getId();
        userObj["username"] = user.getUsername();
        userObj["email"] = user.getEmail().value_or("");
        userObj["full_name"] = user.getFullName().value_or("");
        userObj["is_admin"] = user.isAdmin();
        userObj["is_active"] = user.isActive();

        // Permissions array
        Json::Value permsArray(Json::arrayValue);
        for (const auto& perm : user.getPermissions()) {
            permsArray.append(perm);
        }
        userObj["permissions"] = permsArray;

        // Timestamps (convert to ISO 8601 string using strftime)
        auto createdTime = std::chrono::system_clock::to_time_t(user.getCreatedAt());
        auto updatedTime = std::chrono::system_clock::to_time_t(user.getUpdatedAt());

        char createdBuf[64], updatedBuf[64];
        std::strftime(createdBuf, sizeof(createdBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&createdTime));
        std::strftime(updatedBuf, sizeof(updatedBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&updatedTime));

        userObj["created_at"] = createdBuf;
        userObj["updated_at"] = updatedBuf;

        if (user.getLastLoginAt().has_value()) {
            auto lastLoginTime = std::chrono::system_clock::to_time_t(user.getLastLoginAt().value());
            char lastLoginBuf[64];
            std::strftime(lastLoginBuf, sizeof(lastLoginBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&lastLoginTime));
            userObj["last_login_at"] = lastLoginBuf;
        } else {
            userObj["last_login_at"] = "";
        }

        Json::Value resp;
        resp["success"] = true;
        resp["data"] = userObj;  // Consistent with API convention

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

        // Parse permissions array
        Json::Value permissionsJson = (*json).get("permissions", Json::Value(Json::arrayValue));
        std::vector<std::string> permissionsList;
        for (const auto& perm : permissionsJson) {
            permissionsList.push_back(perm.asString());
        }

        // Create domain User object
        domain::User newUser;
        newUser.setUsername(username);
        newUser.setPasswordHash(passwordHash);
        newUser.setEmail(email.empty() ? std::nullopt : std::make_optional(email));
        newUser.setFullName(fullName.empty() ? std::nullopt : std::make_optional(fullName));
        newUser.setIsAdmin(isAdmin);
        newUser.setPermissions(permissionsList);

        // Create user via repository
        try {
            std::string userId = userRepository_->create(newUser);

            // Fetch created user
            auto userOpt = userRepository_->findById(userId);
            if (!userOpt.has_value()) {
                throw std::runtime_error("Failed to retrieve created user");
            }

            domain::User createdUser = userOpt.value();

            // Build JSON response
            Json::Value userObj;
            userObj["id"] = createdUser.getId();
            userObj["username"] = createdUser.getUsername();
            userObj["email"] = createdUser.getEmail().value_or("");
            userObj["full_name"] = createdUser.getFullName().value_or("");
            userObj["is_admin"] = createdUser.isAdmin();
            userObj["is_active"] = createdUser.isActive();

            Json::Value permsArray(Json::arrayValue);
            for (const auto& perm : createdUser.getPermissions()) {
                permsArray.append(perm);
            }
            userObj["permissions"] = permsArray;

            // Timestamp (convert to ISO 8601 string using strftime)
            auto createdTime = std::chrono::system_clock::to_time_t(createdUser.getCreatedAt());
            char createdBuf[64];
            std::strftime(createdBuf, sizeof(createdBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&createdTime));
            userObj["created_at"] = createdBuf;

            Json::Value resp;
            resp["success"] = true;
            resp["user"] = userObj;
            resp["message"] = "User created successfully";

            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k201Created);
            callback(response);

        } catch (const std::exception& e) {
            // Check for duplicate username
            std::string error = e.what();
            if (error.find("unique") != std::string::npos || error.find("duplicate") != std::string::npos) {
                Json::Value resp;
                resp["success"] = false;
                resp["error"] = "Conflict";
                resp["message"] = "Username already exists";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k409Conflict);
                callback(response);
                return;
            }
            throw;  // Re-throw for outer catch block
        }

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

        // Parse optional fields
        std::optional<std::string> email;
        std::optional<std::string> fullName;
        std::optional<bool> isAdmin;
        std::optional<bool> isActive;
        std::vector<std::string> permissions;

        if (json->isMember("email")) {
            email = (*json)["email"].asString();
        }
        if (json->isMember("full_name")) {
            fullName = (*json)["full_name"].asString();
        }
        if (json->isMember("is_admin")) {
            isAdmin = (*json)["is_admin"].asBool();
        }
        if (json->isMember("is_active")) {
            isActive = (*json)["is_active"].asBool();
        }
        if (json->isMember("permissions") && (*json)["permissions"].isArray()) {
            for (const auto& perm : (*json)["permissions"]) {
                permissions.push_back(perm.asString());
            }
        }

        // Check if any field provided
        if (!email && !fullName && !isAdmin.has_value() && !isActive.has_value() && permissions.empty()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "No fields to update";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }

        // Update user via repository
        bool success = userRepository_->update(userId, email, fullName, isAdmin, permissions, isActive);

        if (!success) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not found";
            resp["message"] = "User not found";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }

        // Fetch updated user
        auto userOpt = userRepository_->findById(userId);
        if (!userOpt.has_value()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not found";
            resp["message"] = "User not found after update";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }

        domain::User user = userOpt.value();

        // Build response from domain object
        Json::Value userObj;
        userObj["id"] = user.getId();
        userObj["username"] = user.getUsername();
        userObj["email"] = user.getEmail().value_or("");
        userObj["full_name"] = user.getFullName().value_or("");
        userObj["is_admin"] = user.isAdmin();
        userObj["is_active"] = user.isActive();

        Json::Value permsArray(Json::arrayValue);
        for (const auto& perm : user.getPermissions()) {
            permsArray.append(perm);
        }
        userObj["permissions"] = permsArray;

        // Format timestamps (using strftime workaround for std::format)
        auto createdTime = std::chrono::system_clock::to_time_t(user.getCreatedAt());
        auto updatedTime = std::chrono::system_clock::to_time_t(user.getUpdatedAt());

        char createdBuf[64], updatedBuf[64];
        std::strftime(createdBuf, sizeof(createdBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&createdTime));
        std::strftime(updatedBuf, sizeof(updatedBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&updatedTime));

        userObj["created_at"] = createdBuf;
        userObj["updated_at"] = updatedBuf;

        Json::Value resp;
        resp["success"] = true;
        resp["data"] = userObj;  // Consistent with API convention
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

        // Delete user via repository
        auto deletedUsernameOpt = userRepository_->remove(userId);

        if (!deletedUsernameOpt.has_value()) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Not found";
            resp["message"] = "User not found";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }

        Json::Value resp;
        resp["success"] = true;
        resp["message"] = "User deleted successfully";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        callback(response);

        spdlog::info("[AuthHandler] User {} deleted by admin {}", deletedUsernameOpt.value(), adminClaims->username);

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

            // Fetch user to verify current password
            auto userOpt = userRepository_->findById(userId);
            if (!userOpt.has_value()) {
                Json::Value resp;
                resp["success"] = false;
                resp["error"] = "Not found";
                resp["message"] = "User not found";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k404NotFound);
                callback(response);
                return;
            }

            domain::User user = userOpt.value();
            if (!auth::verifyPassword(currentPassword, user.getPasswordHash())) {
                Json::Value resp;
                resp["success"] = false;
                resp["error"] = "Forbidden";
                resp["message"] = "Current password is incorrect";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k403Forbidden);
                callback(response);
                return;
            }
        }

        // Hash new password
        std::string newPasswordHash = auth::hashPassword(newPassword);

        // Update password via repository
        bool success = userRepository_->updatePassword(userId, newPasswordHash);
        if (!success) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = "Internal server error";
            resp["message"] = "Failed to update password";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k500InternalServerError);
            callback(response);
            return;
        }

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

        // Fetch logs via repository
        Json::Value logsArray = authAuditRepository_->findAll(
            limit, offset, userId, username, eventType, successFilter, startDate, endDate
        );

        // Get total count
        int total = authAuditRepository_->count(
            userId, username, eventType, successFilter, startDate, endDate
        );

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

        // Get statistics via repository
        Json::Value stats = authAuditRepository_->getStatistics();

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

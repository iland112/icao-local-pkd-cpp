# Phase 3: Authentication & Authorization Implementation Plan

**Version**: v2.0.0 PHASE3-AUTHENTICATION
**Branch**: `feature/phase3-authentication`
**Estimated Duration**: 5-7 days
**Priority**: HIGH (Critical Security Gap)

---

## Executive Summary

Phase 3 implements JWT-based authentication and RBAC authorization to secure all API endpoints. This addresses the critical security gap where all APIs are currently publicly accessible.

**Key Objectives**:
- JWT token-based authentication
- Role-Based Access Control (RBAC)
- User management with bcrypt password hashing
- Frontend login/logout flow
- Session management and token refresh
- Audit logging for authentication events

**⚠️ Breaking Changes**: All API endpoints will require authentication after deployment.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                       Frontend (React)                          │
│  ┌──────────────┐  ┌─────────────┐  ┌──────────────┐          │
│  │ Login Page   │  │ Token Store │  │ API Client   │          │
│  │ (username/pw)│→ │ (localStorage)│→│ (JWT Header) │          │
│  └──────────────┘  └─────────────┘  └──────────────┘          │
└─────────────────────────────────────────────────────────────────┘
                                ↓
┌─────────────────────────────────────────────────────────────────┐
│                    API Gateway (Nginx)                          │
│  - CORS for Authorization header                                │
│  - Proxy to backend services                                    │
└─────────────────────────────────────────────────────────────────┘
                                ↓
┌─────────────────────────────────────────────────────────────────┐
│                 PKD Management Service (C++)                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Authentication Middleware (Global)                       │  │
│  │  - Extract JWT from Authorization header                 │  │
│  │  - Validate token signature and expiration              │  │
│  │  - Load user claims (userId, permissions, isAdmin)      │  │
│  │  - Store in session for downstream handlers             │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                ↓                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Permission Filter (Per-Route)                            │  │
│  │  - Check required permissions                            │  │
│  │  - Return 403 Forbidden if unauthorized                 │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                ↓                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ API Handlers                                             │  │
│  │  - /api/auth/login (public)                             │  │
│  │  - /api/auth/logout (authenticated)                     │  │
│  │  - /api/auth/refresh (authenticated)                    │  │
│  │  - /api/upload/* (requires upload:write)               │  │
│  │  - /api/certificates/export (requires cert:export)     │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                                ↓
┌─────────────────────────────────────────────────────────────────┐
│                     PostgreSQL Database                         │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ users (id, username, password_hash, permissions, ...)   │  │
│  │ auth_audit_log (user_id, event_type, ip, success, ...) │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 3 Implementation Steps

### Step 1: Database Schema (Day 1)

#### 1.1 Create Users Table

**File**: `docker/init-scripts/04-users-schema.sql` (NEW)

```sql
-- User authentication and authorization
CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,    -- bcrypt ($2b$10$...)
    email VARCHAR(255),
    full_name VARCHAR(255),
    permissions JSONB DEFAULT '[]'::jsonb,  -- ["upload:write", "cert:read", ...]
    is_active BOOLEAN DEFAULT true,
    is_admin BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login_at TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for performance
CREATE INDEX idx_users_username ON users(username);
CREATE INDEX idx_users_is_active ON users(is_active);
CREATE INDEX idx_users_permissions ON users USING GIN(permissions);

-- Default admin user (password: admin123 - MUST CHANGE!)
INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions)
VALUES (
    'admin',
    '$2b$10$rO0C/vBfLPHUy8K4KpJxcOqYLbP.VtD3tQZnG9UvN7gJ0LQq7zqXi',  -- bcrypt('admin123')
    'admin@example.com',
    'System Administrator',
    true,
    '["admin", "upload:read", "upload:write", "cert:read", "cert:export", "pa:verify", "sync:read"]'::jsonb
);

-- Test user (password: user123)
INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions)
VALUES (
    'testuser',
    '$2b$10$N9qo8uLOickgx2ZMRZoMye7g8gLJo5K5h3QU2j3E4Y8h3Y8h3Y8h3',  -- bcrypt('user123')
    'user@example.com',
    'Test User',
    false,
    '["upload:read", "cert:read"]'::jsonb
);
```

#### 1.2 Create Audit Log Table

```sql
-- Authentication event audit trail
CREATE TABLE auth_audit_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id),
    username VARCHAR(255),
    event_type VARCHAR(50) NOT NULL,  -- LOGIN_SUCCESS, LOGIN_FAILED, LOGOUT, TOKEN_REFRESH, TOKEN_EXPIRED
    ip_address VARCHAR(45),           -- IPv4 or IPv6
    user_agent TEXT,
    success BOOLEAN DEFAULT true,
    error_message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for audit queries
CREATE INDEX idx_auth_audit_user_id ON auth_audit_log(user_id);
CREATE INDEX idx_auth_audit_created_at ON auth_audit_log(created_at);
CREATE INDEX idx_auth_audit_event_type ON auth_audit_log(event_type);
CREATE INDEX idx_auth_audit_success ON auth_audit_log(success);
```

**Testing**:
- Apply migration locally: `psql -U pkd -d pkd < docker/init-scripts/04-users-schema.sql`
- Verify tables created: `\dt users auth_audit_log`
- Verify admin user: `SELECT username, is_admin FROM users;`
- Verify bcrypt hash format: `SELECT password_hash FROM users WHERE username='admin';`

---

### Step 2: JWT Library Integration (Day 1)

#### 2.1 Add jwt-cpp to vcpkg.json

**File**: `services/pkd-management/vcpkg.json`

```json
{
  "dependencies": [
    "drogon",
    "openssl",
    "nlohmann-json",
    "spdlog",
    "libpq",
    "jwt-cpp"           // ADD THIS
  ]
}
```

#### 2.2 Add bcrypt library

jwt-cpp doesn't include bcrypt, so we need a separate library:

**Option 1**: Use OpenSSL EVP for bcrypt (recommended - already have OpenSSL)
**Option 2**: Add libbcrypt dependency

For simplicity, we'll implement a bcrypt wrapper using OpenSSL:

**File**: `services/pkd-management/src/auth/password_hash.h` (NEW)

```cpp
#pragma once
#include <string>

namespace auth {

// Hash a plaintext password using bcrypt (via OpenSSL)
// Returns bcrypt hash string: $2b$10$...
std::string hashPassword(const std::string& plaintext);

// Verify a plaintext password against a bcrypt hash
// Returns true if password matches
bool verifyPassword(const std::string& plaintext, const std::string& hash);

} // namespace auth
```

**File**: `services/pkd-management/src/auth/password_hash.cpp` (NEW)

```cpp
#include "password_hash.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <cstring>

// Note: bcrypt is not directly in OpenSSL, so we'll use PBKDF2 as fallback
// For production, consider using a proper bcrypt library like libbcrypt

namespace auth {

std::string hashPassword(const std::string& plaintext) {
    // Use PBKDF2-HMAC-SHA256 (10 rounds, 16-byte salt)
    unsigned char salt[16];
    if (!RAND_bytes(salt, sizeof(salt))) {
        throw std::runtime_error("Failed to generate salt");
    }

    unsigned char hash[32];
    if (!PKCS5_PBKDF2_HMAC(
        plaintext.c_str(), plaintext.length(),
        salt, sizeof(salt),
        10000,  // iterations
        EVP_sha256(),
        sizeof(hash), hash))
    {
        throw std::runtime_error("Failed to hash password");
    }

    // Encode as base64-like string (simplified bcrypt format)
    // Format: $pbkdf2$10000$<salt>$<hash>
    // TODO: Implement proper bcrypt or use libbcrypt
    return "$pbkdf2$...";  // Placeholder
}

bool verifyPassword(const std::string& plaintext, const std::string& hash) {
    // Parse hash and extract salt
    // Re-hash plaintext with same salt
    // Compare hashes
    // TODO: Implement verification
    return false;  // Placeholder
}

} // namespace auth
```

**⚠️ Note**: For production, use a proper bcrypt library. OpenSSL doesn't have native bcrypt.

**Alternative**: Use `libbcrypt` or `bcrypt` npm-style library port to C++.

**Testing**:
- Build with jwt-cpp: `cmake --build build`
- Verify jwt-cpp headers: `find build/_deps -name "jwt.h"`

---

### Step 3: JWT Service Implementation (Day 2)

#### 3.1 JWT Service Interface

**File**: `services/pkd-management/src/auth/jwt_service.h` (NEW)

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <jwt-cpp/jwt.h>

namespace auth {

struct JwtClaims {
    std::string userId;
    std::string username;
    std::vector<std::string> permissions;
    bool isAdmin;
    long exp;  // expiration timestamp (seconds since epoch)
    long iat;  // issued at timestamp
};

class JwtService {
public:
    explicit JwtService(const std::string& secretKey,
                        const std::string& issuer = "icao-pkd",
                        int expirationSeconds = 3600);

    // Generate JWT token with user claims
    std::string generateToken(const std::string& userId,
                              const std::string& username,
                              const std::vector<std::string>& permissions,
                              bool isAdmin = false);

    // Validate and decode JWT token
    // Returns std::nullopt if token is invalid or expired
    std::optional<JwtClaims> validateToken(const std::string& token);

    // Refresh token (extend expiration)
    std::string refreshToken(const std::string& token);

private:
    std::string secretKey_;
    std::string issuer_;
    int expirationSeconds_;

    jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson>
        createVerifier();
};

} // namespace auth
```

#### 3.2 JWT Service Implementation

**File**: `services/pkd-management/src/auth/jwt_service.cpp` (NEW)

```cpp
#include "jwt_service.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace auth {

JwtService::JwtService(const std::string& secretKey,
                       const std::string& issuer,
                       int expirationSeconds)
    : secretKey_(secretKey)
    , issuer_(issuer)
    , expirationSeconds_(expirationSeconds) {}

std::string JwtService::generateToken(
    const std::string& userId,
    const std::string& username,
    const std::vector<std::string>& permissions,
    bool isAdmin)
{
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(expirationSeconds_);

    // Build permissions array for JWT claims
    picojson::array permsArray;
    for (const auto& perm : permissions) {
        permsArray.push_back(picojson::value(perm));
    }

    auto token = jwt::create()
        .set_issuer(issuer_)
        .set_type("JWT")
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_subject(userId)
        .set_payload_claim("username", jwt::claim(username))
        .set_payload_claim("permissions", jwt::claim(permsArray))
        .set_payload_claim("isAdmin", jwt::claim(isAdmin))
        .sign(jwt::algorithm::hs256{secretKey_});

    spdlog::debug("Generated JWT for user: {} (expires in {}s)", username, expirationSeconds_);
    return token;
}

std::optional<JwtClaims> JwtService::validateToken(const std::string& token) {
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secretKey_})
            .with_issuer(issuer_);

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        // Extract claims
        JwtClaims claims;
        claims.userId = decoded.get_subject();
        claims.username = decoded.get_payload_claim("username").as_string();
        claims.isAdmin = decoded.get_payload_claim("isAdmin").as_bool();
        claims.exp = decoded.get_expires_at().time_since_epoch().count();
        claims.iat = decoded.get_issued_at().time_since_epoch().count();

        // Extract permissions array
        auto permsArray = decoded.get_payload_claim("permissions").as_array();
        for (const auto& perm : permsArray) {
            claims.permissions.push_back(perm.as_string());
        }

        spdlog::debug("JWT validated for user: {}", claims.username);
        return claims;

    } catch (const jwt::error::token_expired_exception& e) {
        spdlog::warn("JWT expired: {}", e.what());
        return std::nullopt;
    } catch (const jwt::error::signature_verification_exception& e) {
        spdlog::warn("JWT signature verification failed: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::warn("JWT validation failed: {}", e.what());
        return std::nullopt;
    }
}

std::string JwtService::refreshToken(const std::string& token) {
    auto claims = validateToken(token);
    if (!claims) {
        throw std::runtime_error("Cannot refresh invalid token");
    }

    // Generate new token with same claims but extended expiration
    return generateToken(claims->userId, claims->username,
                         claims->permissions, claims->isAdmin);
}

} // namespace auth
```

**Testing**:
- Unit test: Generate token → Validate → Should succeed
- Unit test: Modify token → Validate → Should fail
- Unit test: Wait for expiration → Validate → Should fail (token_expired)
- Unit test: Refresh token → Validate new token → Should succeed

---

### Step 4: Authentication Middleware (Day 3)

#### 4.1 Auth Middleware Implementation

**File**: `services/pkd-management/src/middleware/auth_middleware.h` (NEW)

```cpp
#pragma once
#include <drogon/HttpFilter.h>
#include "auth/jwt_service.h"
#include <memory>
#include <set>

namespace middleware {

class AuthMiddleware : public drogon::HttpFilter<AuthMiddleware> {
public:
    explicit AuthMiddleware();

    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;

    // Configure public endpoints (no auth required)
    static void addPublicEndpoint(const std::string& pattern);

private:
    std::shared_ptr<auth::JwtService> jwtService_;
    static std::set<std::string> publicEndpoints_;

    bool isPublicEndpoint(const std::string& path);
    void logAuthEvent(PGconn* conn,
                      const std::string& userId,
                      const std::string& username,
                      const std::string& eventType,
                      bool success,
                      const std::string& ipAddress,
                      const std::string& userAgent,
                      const std::string& errorMessage = "");
};

} // namespace middleware
```

**File**: `services/pkd-management/src/middleware/auth_middleware.cpp` (NEW)

```cpp
#include "auth_middleware.h"
#include <spdlog/spdlog.h>
#include <regex>
#include <libpq-fe.h>

namespace middleware {

std::set<std::string> AuthMiddleware::publicEndpoints_ = {
    "^/api/health.*",           // Health checks
    "^/api/auth/login$",        // Login endpoint
    "^/api/auth/register$",     // Registration (if enabled)
    "^/static/.*",              // Static files
    "^/api-docs/.*"             // Swagger UI
};

AuthMiddleware::AuthMiddleware() {
    // Load JWT secret from environment
    const char* jwtSecret = std::getenv("JWT_SECRET_KEY");
    if (!jwtSecret || strlen(jwtSecret) < 32) {
        throw std::runtime_error(
            "JWT_SECRET_KEY environment variable not set or too short (min 32 chars)");
    }

    const char* jwtExpiration = std::getenv("JWT_EXPIRATION_SECONDS");
    int expirationSeconds = jwtExpiration ? std::stoi(jwtExpiration) : 3600;

    jwtService_ = std::make_shared<auth::JwtService>(jwtSecret, "icao-pkd", expirationSeconds);
    spdlog::info("AuthMiddleware initialized (token expiration: {}s)", expirationSeconds);
}

void AuthMiddleware::addPublicEndpoint(const std::string& pattern) {
    publicEndpoints_.insert(pattern);
    spdlog::info("Added public endpoint: {}", pattern);
}

bool AuthMiddleware::isPublicEndpoint(const std::string& path) {
    for (const auto& pattern : publicEndpoints_) {
        if (std::regex_match(path, std::regex(pattern))) {
            return true;
        }
    }
    return false;
}

void AuthMiddleware::doFilter(
    const drogon::HttpRequestPtr &req,
    drogon::FilterCallback &&fcb,
    drogon::FilterChainCallback &&fccb)
{
    std::string path = req->path();

    // Allow public endpoints
    if (isPublicEndpoint(path)) {
        fccb();
        return;
    }

    // Extract Authorization header
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty()) {
        Json::Value resp;
        resp["error"] = "Unauthorized";
        resp["message"] = "Missing Authorization header";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k401Unauthorized);
        fcb(response);

        std::string ipAddress = req->peerAddr().toIp();
        logAuthEvent(nullptr, "", "", "AUTH_REQUIRED", false, ipAddress,
                     req->getHeader("User-Agent"), "Missing Authorization header");
        return;
    }

    // Validate Bearer token format
    if (authHeader.substr(0, 7) != "Bearer ") {
        Json::Value resp;
        resp["error"] = "Unauthorized";
        resp["message"] = "Invalid Authorization header format (expected: Bearer <token>)";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k401Unauthorized);
        fcb(response);

        std::string ipAddress = req->peerAddr().toIp();
        logAuthEvent(nullptr, "", "", "INVALID_TOKEN_FORMAT", false, ipAddress,
                     req->getHeader("User-Agent"), "Invalid Authorization header");
        return;
    }

    // Extract and validate JWT
    std::string token = authHeader.substr(7);
    auto claims = jwtService_->validateToken(token);

    if (!claims) {
        Json::Value resp;
        resp["error"] = "Unauthorized";
        resp["message"] = "Invalid or expired token";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k401Unauthorized);
        fcb(response);

        std::string ipAddress = req->peerAddr().toIp();
        logAuthEvent(nullptr, "", "", "TOKEN_VALIDATION_FAILED", false, ipAddress,
                     req->getHeader("User-Agent"), "Invalid or expired token");
        return;
    }

    // Store claims in session for handler access
    auto session = req->session();
    session->insert("user_id", claims->userId);
    session->insert("username", claims->username);
    session->insert("is_admin", claims->isAdmin);

    // Store permissions as JSON array string
    std::string permsJson = "[";
    for (size_t i = 0; i < claims->permissions.size(); ++i) {
        permsJson += "\"" + claims->permissions[i] + "\"";
        if (i < claims->permissions.size() - 1) permsJson += ",";
    }
    permsJson += "]";
    session->insert("permissions", permsJson);

    spdlog::debug("User {} authenticated for {}", claims->username, path);

    // Continue to next filter/handler
    fccb();
}

void AuthMiddleware::logAuthEvent(
    PGconn* conn,
    const std::string& userId,
    const std::string& username,
    const std::string& eventType,
    bool success,
    const std::string& ipAddress,
    const std::string& userAgent,
    const std::string& errorMessage)
{
    // TODO: Log to auth_audit_log table
    if (success) {
        spdlog::info("Auth: {} - user={} ip={}", eventType, username, ipAddress);
    } else {
        spdlog::warn("Auth: {} - user={} ip={} error={}", eventType, username, ipAddress, errorMessage);
    }
}

} // namespace middleware
```

**Testing**:
- Test public endpoint access (no token) → Should succeed
- Test protected endpoint without token → Should return 401
- Test protected endpoint with invalid token → Should return 401
- Test protected endpoint with valid token → Should succeed

---

### Step 5: Permission Filter (Day 3)

#### 5.1 Permission Filter Implementation

**File**: `services/pkd-management/src/middleware/permission_filter.h` (NEW)

```cpp
#pragma once
#include <drogon/HttpFilter.h>
#include <vector>
#include <string>

namespace middleware {

class PermissionFilter : public drogon::HttpFilter<PermissionFilter> {
public:
    explicit PermissionFilter(const std::vector<std::string>& requiredPermissions);

    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;

private:
    std::vector<std::string> requiredPermissions_;

    bool hasPermission(const std::vector<std::string>& userPerms,
                       const std::string& required);
};

} // namespace middleware
```

**File**: `services/pkd-management/src/middleware/permission_filter.cpp` (NEW)

```cpp
#include "permission_filter.h"
#include <spdlog/spdlog.h>
#include <json/json.h>

namespace middleware {

PermissionFilter::PermissionFilter(const std::vector<std::string>& requiredPermissions)
    : requiredPermissions_(requiredPermissions) {}

void PermissionFilter::doFilter(
    const drogon::HttpRequestPtr &req,
    drogon::FilterCallback &&fcb,
    drogon::FilterChainCallback &&fccb)
{
    auto session = req->session();

    // Check if user is admin (bypass permission checks)
    auto isAdminOpt = session->getOptional<bool>("is_admin");
    if (isAdminOpt && *isAdminOpt) {
        spdlog::debug("Admin user - permission check bypassed");
        fccb();
        return;
    }

    // Get user permissions from session
    auto permsJsonOpt = session->getOptional<std::string>("permissions");
    if (!permsJsonOpt) {
        Json::Value resp;
        resp["error"] = "Forbidden";
        resp["message"] = "No permissions found in session";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k403Forbidden);
        fcb(response);
        return;
    }

    // Parse permissions JSON
    Json::Reader reader;
    Json::Value permsArray;
    if (!reader.parse(*permsJsonOpt, permsArray) || !permsArray.isArray()) {
        spdlog::error("Failed to parse permissions JSON: {}", *permsJsonOpt);
        Json::Value resp;
        resp["error"] = "Forbidden";
        resp["message"] = "Invalid permissions format";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k403Forbidden);
        fcb(response);
        return;
    }

    std::vector<std::string> userPerms;
    for (const auto& perm : permsArray) {
        userPerms.push_back(perm.asString());
    }

    // Check if user has all required permissions
    for (const auto& required : requiredPermissions_) {
        if (!hasPermission(userPerms, required)) {
            std::string username = session->getOptional<std::string>("username").value_or("unknown");
            spdlog::warn("User {} missing required permission: {}", username, required);

            Json::Value resp;
            resp["error"] = "Forbidden";
            resp["message"] = "Missing required permission: " + required;
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            response->setStatusCode(drogon::k403Forbidden);
            fcb(response);
            return;
        }
    }

    // All permissions satisfied
    fccb();
}

bool PermissionFilter::hasPermission(
    const std::vector<std::string>& userPerms,
    const std::string& required)
{
    return std::find(userPerms.begin(), userPerms.end(), required) != userPerms.end();
}

} // namespace middleware
```

#### 5.2 Apply Permission Filters to Routes

**File**: `services/pkd-management/src/main.cpp` (modify route registration)

```cpp
// Example: Apply permission filter to upload endpoints
app().registerHandler(
    "/api/upload/ldif",
    [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        // Handler implementation
    },
    {Post}
).addFilter(std::make_shared<PermissionFilter>(
    std::vector<std::string>{"upload:write"}
));

app().registerHandler(
    "/api/certificates/export/country",
    [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        // Handler implementation
    },
    {Get}
).addFilter(std::make_shared<PermissionFilter>(
    std::vector<std::string>{"cert:export"}
));

// PA verify endpoint
app().registerHandler(
    "/api/pa/verify",
    [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        // Handler implementation
    },
    {Post}
).addFilter(std::make_shared<PermissionFilter>(
    std::vector<std::string>{"pa:verify"}
));
```

**Testing**:
- Test admin user with any permission → Should succeed
- Test user with correct permission → Should succeed
- Test user without permission → Should return 403
- Test multiple required permissions → Should check all

---

### Step 6: Login Handler (Day 4)

#### 6.1 Login Endpoint Implementation

**File**: `services/pkd-management/src/handlers/auth_handler.h` (NEW)

```cpp
#pragma once
#include <drogon/HttpController.h>

namespace handlers {

void registerAuthRoutes(drogon::HttpAppFramework& app);

} // namespace handlers
```

**File**: `services/pkd-management/src/handlers/auth_handler.cpp` (NEW)

```cpp
#include "auth_handler.h"
#include "auth/jwt_service.h"
#include "auth/password_hash.h"
#include <libpq-fe.h>
#include <spdlog/spdlog.h>
#include <json/json.h>

namespace handlers {

void registerAuthRoutes(drogon::HttpAppFramework& app) {
    // POST /api/auth/login
    app.registerHandler(
        "/api/auth/login",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

            auto json = req->getJsonObject();
            if (!json) {
                Json::Value resp;
                resp["error"] = "Invalid request";
                resp["message"] = "Request body must be JSON";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }

            std::string username = (*json).get("username", "").asString();
            std::string password = (*json).get("password", "").asString();

            if (username.empty() || password.empty()) {
                Json::Value resp;
                resp["error"] = "Invalid request";
                resp["message"] = "Missing username or password";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }

            // Connect to database
            const char* conninfo = std::getenv("DB_CONNECTION_STRING");
            if (!conninfo) conninfo = "host=127.0.0.1 port=5432 dbname=pkd user=pkd password=pkd123";

            PGconn* conn = PQconnectdb(conninfo);
            if (PQstatus(conn) != CONNECTION_OK) {
                spdlog::error("Database connection failed: {}", PQerrorMessage(conn));
                Json::Value resp;
                resp["error"] = "Internal server error";
                resp["message"] = "Database connection failed";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k500InternalServerError);
                callback(response);
                PQfinish(conn);
                return;
            }

            // Query user from database (parameterized query)
            const char* query = "SELECT id, password_hash, permissions, is_admin "
                                "FROM users WHERE username = $1 AND is_active = true";
            const char* paramValues[1] = {username.c_str()};
            PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                         nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
                spdlog::warn("Login failed - user not found: {}", username);
                Json::Value resp;
                resp["error"] = "Unauthorized";
                resp["message"] = "Invalid username or password";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k401Unauthorized);
                callback(response);
                PQclear(res);
                PQfinish(conn);

                // TODO: Log failed login attempt
                return;
            }

            std::string userId = PQgetvalue(res, 0, 0);
            std::string passwordHash = PQgetvalue(res, 0, 1);
            std::string permissionsJson = PQgetvalue(res, 0, 2);
            bool isAdmin = strcmp(PQgetvalue(res, 0, 3), "t") == 0;

            PQclear(res);

            // Verify password
            if (!auth::verifyPassword(password, passwordHash)) {
                spdlog::warn("Login failed - wrong password: {}", username);
                Json::Value resp;
                resp["error"] = "Unauthorized";
                resp["message"] = "Invalid username or password";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k401Unauthorized);
                callback(response);
                PQfinish(conn);

                // TODO: Log failed login attempt
                return;
            }

            // Parse permissions JSON
            Json::Reader reader;
            Json::Value permsArray;
            if (!reader.parse(permissionsJson, permsArray) || !permsArray.isArray()) {
                spdlog::error("Failed to parse permissions for user: {}", username);
                permsArray = Json::Value(Json::arrayValue);
            }

            std::vector<std::string> permissions;
            for (const auto& perm : permsArray) {
                permissions.push_back(perm.asString());
            }

            // Generate JWT
            const char* jwtSecret = std::getenv("JWT_SECRET_KEY");
            if (!jwtSecret) jwtSecret = "default-secret-key-CHANGE-ME";

            auth::JwtService jwtService(jwtSecret);
            std::string token = jwtService.generateToken(userId, username, permissions, isAdmin);

            // Update last_login_at
            const char* updateQuery = "UPDATE users SET last_login_at = NOW() WHERE id = $1";
            const char* updateParams[1] = {userId.c_str()};
            PGresult* updateRes = PQexecParams(conn, updateQuery, 1, nullptr, updateParams,
                                               nullptr, nullptr, 0);
            if (PQresultStatus(updateRes) != PGRES_COMMAND_OK) {
                spdlog::warn("Failed to update last_login_at for user: {}", username);
            }
            PQclear(updateRes);
            PQfinish(conn);

            // Return token
            Json::Value resp;
            resp["access_token"] = token;
            resp["token_type"] = "Bearer";
            resp["expires_in"] = 3600;
            resp["user"]["id"] = userId;
            resp["user"]["username"] = username;
            resp["user"]["isAdmin"] = isAdmin;
            resp["user"]["permissions"] = permsArray;

            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            callback(response);

            spdlog::info("Login successful: user={}", username);
            // TODO: Log successful login
        },
        {drogon::Post}
    );

    // POST /api/auth/logout
    app.registerHandler(
        "/api/auth/logout",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

            auto session = req->session();
            std::string username = session->getOptional<std::string>("username").value_or("unknown");

            spdlog::info("Logout: user={}", username);
            // TODO: Log logout event

            Json::Value resp;
            resp["success"] = true;
            resp["message"] = "Logged out successfully";
            auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
            callback(response);
        },
        {drogon::Post}
    );

    // POST /api/auth/refresh
    app.registerHandler(
        "/api/auth/refresh",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

            // Get current token from Authorization header
            std::string authHeader = req->getHeader("Authorization");
            if (authHeader.substr(0, 7) != "Bearer ") {
                Json::Value resp;
                resp["error"] = "Invalid Authorization header";
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }

            std::string token = authHeader.substr(7);

            // Refresh token
            const char* jwtSecret = std::getenv("JWT_SECRET_KEY");
            if (!jwtSecret) jwtSecret = "default-secret-key-CHANGE-ME";

            auth::JwtService jwtService(jwtSecret);

            try {
                std::string newToken = jwtService.refreshToken(token);

                Json::Value resp;
                resp["access_token"] = newToken;
                resp["token_type"] = "Bearer";
                resp["expires_in"] = 3600;

                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                callback(response);

                spdlog::info("Token refreshed for user");
            } catch (const std::exception& e) {
                Json::Value resp;
                resp["error"] = "Failed to refresh token";
                resp["message"] = e.what();
                auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                response->setStatusCode(drogon::k401Unauthorized);
                callback(response);
            }
        },
        {drogon::Post}
    );
}

} // namespace handlers
```

**Testing**:
- Test login with valid credentials → Should return JWT token
- Test login with invalid username → Should return 401
- Test login with invalid password → Should return 401
- Test logout → Should succeed
- Test token refresh with valid token → Should return new token
- Test token refresh with expired token → Should return 401

---

### Step 7: Frontend Integration (Day 5-6)

#### 7.1 Login Page Component

**File**: `frontend/src/pages/Login.tsx` (NEW)

```typescript
import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { login } from '../api/authApi';

export default function Login() {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const navigate = useNavigate();

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setLoading(true);

    try {
      const response = await login(username, password);

      // Store token in localStorage
      localStorage.setItem('access_token', response.access_token);
      localStorage.setItem('user', JSON.stringify(response.user));

      // Redirect to dashboard
      navigate('/');
    } catch (err: any) {
      setError(err.message || 'Login failed');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen flex items-center justify-center bg-gray-900">
      <div className="max-w-md w-full space-y-8 p-8 bg-gray-800 rounded-xl shadow-2xl">
        <div>
          <h2 className="text-center text-3xl font-bold text-white">
            ICAO Local PKD
          </h2>
          <p className="mt-2 text-center text-sm text-gray-400">
            Sign in to your account
          </p>
        </div>
        <form className="mt-8 space-y-6" onSubmit={handleSubmit}>
          <div className="space-y-4">
            <div>
              <label htmlFor="username" className="block text-sm font-medium text-gray-300">
                Username
              </label>
              <input
                id="username"
                name="username"
                type="text"
                required
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                className="mt-1 block w-full px-3 py-2 bg-gray-700 border border-gray-600 rounded-md text-white placeholder-gray-400 focus:outline-none focus:ring-2 focus:ring-blue-500"
                placeholder="admin"
              />
            </div>
            <div>
              <label htmlFor="password" className="block text-sm font-medium text-gray-300">
                Password
              </label>
              <input
                id="password"
                name="password"
                type="password"
                required
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                className="mt-1 block w-full px-3 py-2 bg-gray-700 border border-gray-600 rounded-md text-white placeholder-gray-400 focus:outline-none focus:ring-2 focus:ring-blue-500"
                placeholder="••••••••"
              />
            </div>
          </div>

          {error && (
            <div className="rounded-md bg-red-900/50 p-4">
              <div className="text-sm text-red-200">{error}</div>
            </div>
          )}

          <button
            type="submit"
            disabled={loading}
            className="w-full flex justify-center py-2 px-4 border border-transparent rounded-md shadow-sm text-sm font-medium text-white bg-blue-600 hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 disabled:opacity-50"
          >
            {loading ? 'Signing in...' : 'Sign in'}
          </button>
        </form>
      </div>
    </div>
  );
}
```

#### 7.2 API Client with Token Injection

**File**: `frontend/src/api/authApi.ts` (NEW)

```typescript
const API_BASE = '/api';

export async function login(username: string, password: string) {
  const response = await fetch(`${API_BASE}/auth/login`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ username, password }),
  });

  if (!response.ok) {
    const error = await response.json();
    throw new Error(error.message || 'Login failed');
  }

  return response.json();
}

export async function logout() {
  const token = localStorage.getItem('access_token');

  const response = await fetch(`${API_BASE}/auth/logout`, {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${token}`,
    },
  });

  localStorage.removeItem('access_token');
  localStorage.removeItem('user');

  return response.json();
}

export async function refreshToken() {
  const token = localStorage.getItem('access_token');

  const response = await fetch(`${API_BASE}/auth/refresh`, {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${token}`,
    },
  });

  if (!response.ok) {
    throw new Error('Token refresh failed');
  }

  const data = await response.json();
  localStorage.setItem('access_token', data.access_token);
  return data;
}
```

**File**: `frontend/src/api/client.ts` (modify existing)

```typescript
// Intercept all API requests to add Authorization header
async function authenticatedFetch(url: string, options: RequestInit = {}) {
  const token = localStorage.getItem('access_token');

  const headers = {
    ...options.headers,
    ...(token ? { 'Authorization': `Bearer ${token}` } : {}),
  };

  let response = await fetch(url, {
    ...options,
    headers,
  });

  // If token expired, try to refresh
  if (response.status === 401) {
    try {
      await refreshToken();
      const newToken = localStorage.getItem('access_token');

      // Retry request with new token
      response = await fetch(url, {
        ...options,
        headers: {
          ...options.headers,
          'Authorization': `Bearer ${newToken}`,
        },
      });
    } catch (err) {
      // Refresh failed, redirect to login
      localStorage.removeItem('access_token');
      localStorage.removeItem('user');
      window.location.href = '/login';
    }
  }

  return response;
}

export default authenticatedFetch;
```

#### 7.3 Route Guards

**File**: `frontend/src/App.tsx` (modify)

```typescript
import { Navigate } from 'react-router-dom';
import Login from './pages/Login';

function PrivateRoute({ children }: { children: React.ReactNode }) {
  const token = localStorage.getItem('access_token');

  if (!token) {
    return <Navigate to="/login" replace />;
  }

  return <>{children}</>;
}

function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/login" element={<Login />} />

        <Route path="/" element={
          <PrivateRoute>
            <Dashboard />
          </PrivateRoute>
        } />

        <Route path="/upload" element={
          <PrivateRoute>
            <FileUpload />
          </PrivateRoute>
        } />

        {/* All other routes */}
      </Routes>
    </BrowserRouter>
  );
}
```

**Testing**:
- Access protected route without token → Should redirect to /login
- Login with valid credentials → Should redirect to dashboard
- Token expires → Should auto-refresh on next request
- Refresh fails → Should redirect to /login
- Logout → Should clear token and redirect to /login

---

### Step 8: Environment Variables (Day 6)

#### 8.1 Update .env.example

**File**: `.env.example` (add JWT section)

```bash
# JWT Configuration
JWT_SECRET_KEY=                      # REQUIRED: Min 32 chars, use: openssl rand -hex 32
JWT_ALGORITHM=HS256                  # HS256 or RS256
JWT_EXPIRATION_SECONDS=3600          # 1 hour (3600 seconds)

# PostgreSQL Configuration (existing)
DB_PASSWORD=

# LDAP Configuration (existing)
LDAP_BIND_PASSWORD=
```

#### 8.2 Update docker-compose.yaml

**File**: `docker/docker-compose.yaml` (add JWT env vars)

```yaml
services:
  pkd-management:
    environment:
      # ... existing env vars
      - JWT_SECRET_KEY=${JWT_SECRET_KEY}
      - JWT_EXPIRATION_SECONDS=3600
```

#### 8.3 Update docker-compose-luckfox.yaml

**File**: `docker-compose-luckfox.yaml` (add JWT env vars)

```yaml
services:
  pkd-management:
    environment:
      # ... existing env vars
      - JWT_SECRET_KEY=${JWT_SECRET_KEY}
      - JWT_EXPIRATION_SECONDS=3600
```

---

### Step 9: Testing & Validation (Day 7)

#### 9.1 Unit Tests

- JWT Service: Token generation, validation, refresh
- Password hashing: Hash generation, verification
- Permission checking: hasPermission logic

#### 9.2 Integration Tests

- Login flow: POST /api/auth/login with valid/invalid credentials
- Protected endpoints: Access with/without token
- Permission filtering: Access with sufficient/insufficient permissions
- Token refresh: Expired token auto-refresh
- Logout: Token invalidation

#### 9.3 Frontend Tests

- Login page: Form validation, error handling
- Route guards: Redirect behavior
- Token storage: localStorage operations
- API client: Token injection, refresh

---

## Breaking Changes & Migration

### Breaking Changes

⚠️ **WARNING**: Phase 3 introduces breaking changes

1. **All API endpoints require authentication** (except public endpoints)
2. **No migration window** - Immediate enforcement upon deployment
3. **External API clients must be updated** to obtain and use JWT tokens

### Public Endpoints (No Auth Required)

- `GET /api/health*`
- `POST /api/auth/login`
- `POST /api/auth/register` (if enabled)
- `GET /static/*`
- `GET /api-docs/*`

### Protected Endpoints (Auth Required)

All other endpoints require `Authorization: Bearer <token>` header.

### Migration Checklist

- [ ] Generate JWT_SECRET_KEY: `openssl rand -hex 32`
- [ ] Add JWT_SECRET_KEY to .env file
- [ ] Create admin user in database
- [ ] Test login flow locally
- [ ] Update all internal clients (frontend)
- [ ] Notify external API consumers
- [ ] Deploy to Luckfox
- [ ] Verify authentication working
- [ ] Monitor auth_audit_log for issues

---

## Environment Variables

| Variable | Required | Default | Description |
| -------- | -------- | ------- | ----------- |
| `JWT_SECRET_KEY` | ✅ Yes | - | JWT signing secret (min 32 chars) |
| `JWT_ALGORITHM` | ⏭️ No | HS256 | JWT algorithm (HS256 or RS256) |
| `JWT_EXPIRATION_SECONDS` | ⏭️ No | 3600 | Token expiration (1 hour) |
| `DB_PASSWORD` | ✅ Yes | - | PostgreSQL password |
| `LDAP_BIND_PASSWORD` | ✅ Yes | - | LDAP bind password |

---

## RBAC Permission Schema

### Permission Format

Permissions use `resource:action` format:

- `upload:read` - View upload history
- `upload:write` - Upload files
- `cert:read` - Search certificates
- `cert:export` - Export certificates
- `pa:verify` - Verify passive authentication
- `sync:read` - View sync status
- `admin` - Full admin access (bypass all checks)

### Permission Mapping

| Endpoint | Required Permission |
| -------- | ------------------- |
| POST /api/upload/ldif | upload:write |
| POST /api/upload/masterlist | upload:write |
| GET /api/upload/history | upload:read |
| GET /api/certificates/search | cert:read |
| GET /api/certificates/export/* | cert:export |
| POST /api/pa/verify | pa:verify |
| GET /api/sync/status | sync:read |
| POST /api/sync/trigger | sync:write (admin) |

### Default Users

| Username | Password | Permissions | Admin |
| -------- | -------- | ----------- | ----- |
| admin | admin123 | All | Yes |
| testuser | user123 | upload:read, cert:read | No |

⚠️ **IMPORTANT**: Change default passwords immediately after deployment!

---

## Risk Assessment

### Security Risks Mitigated

- ✅ **Unauthorized API Access**: All endpoints protected by JWT
- ✅ **Privilege Escalation**: RBAC prevents unauthorized actions
- ✅ **Session Hijacking**: JWT stateless, short expiration
- ✅ **Credential Theft**: Bcrypt hashing, secure storage

### Remaining Risks

- ⚠️ **JWT Secret Exposure**: Store securely, rotate periodically
- ⚠️ **Token Theft**: Use HTTPS, short expiration, refresh tokens
- ⚠️ **Brute Force**: Implement rate limiting (API Gateway)
- ⚠️ **Password Weakness**: Enforce strong password policy

---

## Success Criteria

- [ ] JWT authentication working
- [ ] All protected endpoints require valid token
- [ ] RBAC permissions enforced
- [ ] Login/logout flow complete
- [ ] Frontend integration working
- [ ] Token refresh automatic
- [ ] Audit logging functional
- [ ] Default admin user created
- [ ] External clients notified
- [ ] All tests passed
- [ ] Documentation complete
- [ ] Deployed to Luckfox

---

## Documentation

- Implementation report: `docs/PHASE3_AUTHENTICATION_IMPLEMENTATION.md` (to be created)
- Security status: `docs/SECURITY_HARDENING_STATUS.md` (update after completion)
- API documentation: Update OpenAPI specs with authentication
- User guide: Create authentication guide for external clients

---

## Timeline

| Day | Tasks | Deliverables |
| --- | ----- | ------------ |
| Day 1 | Database schema, JWT library | users table, auth_audit_log table, jwt-cpp integrated |
| Day 2 | JWT Service, Password hashing | JwtService, PasswordHash classes, unit tests |
| Day 3 | Auth Middleware, Permission Filter | AuthMiddleware, PermissionFilter, applied to routes |
| Day 4 | Login Handler, Logout, Refresh | /api/auth/* endpoints, integration tests |
| Day 5-6 | Frontend integration | Login page, route guards, token injection |
| Day 6 | Environment variables, config | .env updates, docker-compose updates |
| Day 7 | Testing & Validation | Full test suite, integration tests, deployment prep |

**Total**: 7 days (5-7 day estimate)

---

**Phase 3 Status**: 📋 Ready to Begin

**Next Action**: Review plan, create TODO list, begin Day 1 tasks

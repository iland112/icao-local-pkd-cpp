/** @file auth_middleware.cpp
 *  @brief AuthMiddleware implementation
 */

#include "auth_middleware.h"
#include "../repositories/auth_audit_repository.h"
#include "../repositories/api_client_repository.h"
#include "../auth/api_key_generator.h"
#include "../infrastructure/service_container.h"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <regex>
#include <chrono>

// Global service container (defined in main.cpp)
extern infrastructure::ServiceContainer* g_services;

namespace middleware {

// Static members initialization
std::set<std::string> AuthMiddleware::publicEndpoints_ = {
    // --- System & Authentication ---
    "^/api/health.*",              // Health check endpoints
    "^/api/auth/login$",           // Login endpoint
    "^/api/auth/register$",        // Registration endpoint (future)
    "^/api/auth/logout$",          // Logout endpoint (handler validates JWT)
    "^/api/auth/refresh$",         // Token refresh endpoint (handler validates JWT)
    "^/api/auth/me$",              // Current user info endpoint (handler validates JWT)
    "^/api/auth/users.*",          // User management endpoints (handler validates JWT + admin)
    "^/api/auth/audit-log.*",      // Auth audit log endpoints (handler validates JWT + admin)

    // --- Dashboard & Statistics (Read-only public information) ---
    "^/api/upload/countries$",     // Dashboard country statistics (homepage)
    "^/api/upload/countries/detailed.*",  // Detailed country statistics (homepage, with query params)
    "^/api/upload/history.*",      // Upload history (development access)
    "^/api/upload/statistics$",    // Upload statistics
    "^/api/upload/statistics/validation-reasons$",  // Validation reason breakdown
    "^/api/upload/changes.*",      // Recent upload changes
    "^/api/upload/[a-f0-9\\-]+$",  // Upload by ID (deprecated pattern)
    "^/api/upload/detail/[a-f0-9\\-]+$", // Upload detail by ID (fixes 401 error)
    "^/api/upload/[a-f0-9\\-]+/.*", // Upload sub-resources (validations, issues, etc.)

    // --- Certificate Preview & Upload Progress (Read-only, no data modification) ---
    "^/api/upload/certificate/preview$",   // Certificate preview (parse only, no save)
    "^/api/progress.*",            // Upload progress SSE stream (EventSource cannot send custom headers)

    // --- Certificate Search (Public directory service) ---
    "^/api/certificates/countries$", // Country list for certificate search
    "^/api/certificates/search.*",   // Certificate search with filters
    "^/api/certificates/validation.*", // Certificate validation results (trust chain)
    "^/api/certificates/pa-lookup$",   // PA lightweight lookup (subject DN / fingerprint)
    "^/api/certificates/export/.*",  // Certificate export endpoints
    "^/api/certificates/dsc-nc/report$",           // DSC_NC non-conformant report
    "^/api/certificates/crl/report$",              // CRL report
    "^/api/certificates/crl/[a-f0-9\\-]+$",       // CRL detail by ID
    "^/api/certificates/crl/[a-f0-9\\-]+/download$", // CRL binary download
    "^/api/certificates/doc9303-checklist.*",      // Doc 9303 compliance checklist

    // --- Code Master (Read-only reference data) ---
    "^/api/code-master.*",             // Code master list, categories, detail (read-only GET)

    // --- ICAO PKD Version Monitoring (Read-only public information) ---
    "^/api/icao/status$",          // ICAO version status comparison
    "^/api/icao/latest$",          // Latest ICAO version information
    "^/api/icao/history.*",        // Version check history
    "^/api/icao/check-updates$",   // Trigger ICAO version check

    // --- Sync Dashboard (Read-only monitoring) ---
    "^/api/sync/status$",          // DB-LDAP sync status
    "^/api/sync/stats$",           // Sync statistics
    "^/api/reconcile/history.*",   // Reconciliation history

    // --- Audit Logs (Read-only monitoring) ---
    "^/api/audit/operations$",     // Operation audit logs
    "^/api/audit/operations/stats$", // Operation statistics

    // --- PA (Passive Authentication) Service (Demo/Verification functionality) ---
    "^/api/pa/verify$",            // PA verification (main function)
    "^/api/pa/parse-sod$",         // Parse SOD (Security Object Document)
    "^/api/pa/parse-dg1$",         // Parse DG1 (MRZ data)
    "^/api/pa/parse-dg2$",         // Parse DG2 (Face image)
    "^/api/pa/parse-mrz-text$",    // Parse MRZ text
    "^/api/pa/history.*",          // PA verification history
    "^/api/pa/statistics$",        // PA statistics
    "^/api/pa/[a-f0-9\\-]+$",      // PA verification detail by ID (UUID)
    "^/api/pa/[a-f0-9\\-]+/datagroups$", // DataGroups detail

    // --- Static Files & Documentation ---
    "^/static/.*",                 // Static files (CSS, JS, images)
    "^/api-docs.*",                // API documentation
    "^/swagger-ui/.*",             // Swagger UI

    // --- Validation (Admin operations) ---
    "^/api/validation/revalidate$"  // DSC trust chain re-validation
};

bool AuthMiddleware::authEnabled_ = true;
std::vector<std::regex> AuthMiddleware::compiledPatterns_;
std::once_flag AuthMiddleware::patternsInitFlag_;
std::unique_ptr<middleware::ApiRateLimiter> AuthMiddleware::rateLimiter_;

AuthMiddleware::AuthMiddleware() {
    // Check if authentication is disabled (for testing)
    const char* authEnabledEnv = std::getenv("AUTH_ENABLED");
    if (authEnabledEnv && std::string(authEnabledEnv) == "false") {
        authEnabled_ = false;
        spdlog::warn("[AuthMiddleware] ⚠️  Authentication DISABLED (AUTH_ENABLED=false)");
        return;
    }

    // Load JWT secret from environment
    const char* jwtSecret = std::getenv("JWT_SECRET_KEY");
    if (!jwtSecret || strlen(jwtSecret) < 32) {
        throw std::runtime_error(
            "JWT_SECRET_KEY environment variable not set or too short (min 32 chars). "
            "Generate one with: openssl rand -hex 32");
    }

    // Load JWT configuration
    const char* jwtIssuer = std::getenv("JWT_ISSUER");
    const char* jwtExpirationStr = std::getenv("JWT_EXPIRATION_SECONDS");

    int jwtExpiration = 3600; // Default: 1 hour
    if (jwtExpirationStr) {
        jwtExpiration = std::atoi(jwtExpirationStr);
    }

    // Initialize JWT service
    jwtService_ = std::make_shared<auth::JwtService>(
        jwtSecret,
        jwtIssuer ? jwtIssuer : "icao-pkd",
        jwtExpiration
    );

    // Initialize rate limiter (singleton, thread-safe)
    if (!rateLimiter_) {
        rateLimiter_ = std::make_unique<middleware::ApiRateLimiter>();
    }

    spdlog::info("[AuthMiddleware] Initialized (issuer={}, expiration={}s)",
                 jwtIssuer ? jwtIssuer : "icao-pkd", jwtExpiration);
}

void AuthMiddleware::doFilter(
    const drogon::HttpRequestPtr& req,
    drogon::FilterCallback&& fcb,
    drogon::FilterChainCallback&& fccb) {

    // Skip authentication if disabled
    if (!authEnabled_) {
        fccb();
        return;
    }

    std::string path = req->path();

    // Allow public endpoints
    if (isPublicEndpoint(path)) {
        spdlog::debug("[AuthMiddleware] Public endpoint: {}", path);
        fccb();
        return;
    }

    // --- Try X-API-Key header first (external client agents) ---
    std::string apiKeyHeader = req->getHeader("X-API-Key");
    if (!apiKeyHeader.empty()) {
        auto client = validateApiKey(apiKeyHeader, path, req->peerAddr().toIp());
        if (client) {
            // Store client info in request attributes
            auto attrs = req->getAttributes();
            attrs->insert("client_id", client->id);
            attrs->insert("client_name", client->clientName);
            attrs->insert("auth_type", std::string("api_key"));

            // Store permissions
            Json::Value permsJson(Json::arrayValue);
            for (const auto& perm : client->permissions) {
                permsJson.append(perm);
            }
            attrs->insert("permissions", permsJson.toStyledString());

            // Check rate limit
            if (rateLimiter_) {
                auto rl = rateLimiter_->checkAndIncrement(
                    client->id,
                    client->rateLimitPerMinute,
                    client->rateLimitPerHour,
                    client->rateLimitPerDay);

                if (!rl.allowed) {
                    Json::Value resp;
                    resp["success"] = false;
                    resp["error"] = "Rate limit exceeded";
                    resp["limit"] = rl.limit;
                    resp["window"] = rl.window;
                    resp["retry_after_seconds"] = static_cast<int>(rl.resetAt -
                        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

                    auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
                    response->setStatusCode(drogon::k429TooManyRequests);
                    response->addHeader("Retry-After",
                        std::to_string(rl.resetAt - std::chrono::system_clock::to_time_t(
                            std::chrono::system_clock::now())));
                    response->addHeader("X-RateLimit-Limit", std::to_string(rl.limit));
                    response->addHeader("X-RateLimit-Remaining", "0");
                    response->addHeader("X-RateLimit-Reset", std::to_string(rl.resetAt));
                    fcb(response);
                    return;
                }
            }

            // Update usage asynchronously (fire-and-forget)
            if (g_services && g_services->apiClientRepository()) {
                g_services->apiClientRepository()->updateUsage(client->id);
            }

            spdlog::debug("[AuthMiddleware] API Key authenticated: {} ({})",
                          client->clientName, client->apiKeyPrefix);
            fccb();
            return;
        }

        // API Key validation failed
        Json::Value resp;
        resp["error"] = "Unauthorized";
        resp["message"] = "Invalid API key";
        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k401Unauthorized);
        fcb(response);

        logAuthEvent("", "API_KEY_INVALID", false,
                     req->peerAddr().toIp(),
                     req->getHeader("User-Agent"),
                     "Invalid API key");
        return;
    }

    // --- Try Bearer JWT (web UI users) ---
    // Extract Authorization header
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty()) {
        Json::Value resp;
        resp["error"] = "Unauthorized";
        resp["message"] = "Missing Authorization header";
        resp["required_format"] = "Bearer <token> or X-API-Key header";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k401Unauthorized);
        fcb(response);

        logAuthEvent("", "AUTH_REQUIRED", false,
                     req->peerAddr().toIp(),
                     req->getHeader("User-Agent"),
                     "Missing Authorization header");
        return;
    }

    // Validate Bearer token format
    if (authHeader.substr(0, 7) != "Bearer ") {
        Json::Value resp;
        resp["error"] = "Unauthorized";
        resp["message"] = "Invalid Authorization header format";
        resp["required_format"] = "Bearer <token>";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k401Unauthorized);
        fcb(response);

        logAuthEvent("", "INVALID_TOKEN_FORMAT", false,
                     req->peerAddr().toIp(),
                     req->getHeader("User-Agent"),
                     "Invalid Authorization header format");
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

        logAuthEvent("", "TOKEN_VALIDATION_FAILED", false,
                     req->peerAddr().toIp(),
                     req->getHeader("User-Agent"),
                     "Invalid or expired token");
        return;
    }

    // Store claims in request attributes for handler access
    // (session may be NULL if Drogon sessions are not enabled)
    auto attrs = req->getAttributes();
    attrs->insert("user_id", claims->userId);
    attrs->insert("username", claims->username);
    attrs->insert("is_admin", claims->isAdmin);

    // Store permissions as JSON string
    Json::Value permsJson(Json::arrayValue);
    for (const auto& perm : claims->permissions) {
        permsJson.append(perm);
    }
    attrs->insert("permissions", permsJson.toStyledString());

    // Also store in session if available (backward compatibility)
    auto session = req->getSession();
    if (session) {
        session->insert("user_id", claims->userId);
        session->insert("username", claims->username);
        session->insert("is_admin", claims->isAdmin);
        session->insert("permissions", permsJson.toStyledString());
    }

    spdlog::debug("[AuthMiddleware] User {} authenticated for {}",
                  claims->username, path);

    logAuthEvent(claims->username, "TOKEN_VALIDATED", true,
                 req->peerAddr().toIp(),
                 req->getHeader("User-Agent"));

    // Continue to next filter/handler
    fccb();
}

void AuthMiddleware::addPublicEndpoint(const std::string& pattern) {
    publicEndpoints_.insert(pattern);
    spdlog::info("[AuthMiddleware] Added public endpoint pattern: {}", pattern);
}

bool AuthMiddleware::isAuthEnabled() {
    return authEnabled_;
}

bool AuthMiddleware::isPublicEndpoint(const std::string& path) const {
    // Pre-compile regex patterns once (thread-safe)
    std::call_once(patternsInitFlag_, []() {
        compiledPatterns_.reserve(publicEndpoints_.size());
        for (const auto& pattern : publicEndpoints_) {
            try {
                compiledPatterns_.emplace_back(pattern, std::regex::optimize);
            } catch (const std::regex_error& e) {
                spdlog::error("[AuthMiddleware] Invalid regex pattern '{}': {}",
                              pattern, e.what());
            }
        }
        spdlog::info("[AuthMiddleware] Pre-compiled {} regex patterns", compiledPatterns_.size());
    });

    for (const auto& re : compiledPatterns_) {
        if (std::regex_match(path, re)) {
            return true;
        }
    }
    return false;
}

void AuthMiddleware::logAuthEvent(
    const std::string& username,
    const std::string& eventType,
    bool success,
    const std::string& ipAddress,
    const std::string& userAgent,
    const std::string& errorMessage) {

    // Use ServiceContainer's AuthAuditRepository (supports both PostgreSQL and Oracle)
    if (!g_services || !g_services->authAuditRepository()) {
        spdlog::warn("[AuthMiddleware] authAuditRepository not available, skipping audit log");
        return;
    }

    try {
        g_services->authAuditRepository()->insert(
            std::nullopt,  // userId (not available at middleware level)
            username.empty() ? "anonymous" : username,
            eventType,
            success,
            ipAddress.empty() ? std::nullopt : std::make_optional(ipAddress),
            userAgent.empty() ? std::nullopt : std::make_optional(userAgent),
            errorMessage.empty() ? std::nullopt : std::make_optional(errorMessage)
        );
    } catch (const std::exception& e) {
        spdlog::error("[AuthMiddleware] Failed to insert audit log: {}", e.what());
    }
}

std::optional<domain::models::ApiClient> AuthMiddleware::validateApiKey(
    const std::string& apiKey,
    const std::string& path,
    const std::string& clientIp) {

    if (!g_services || !g_services->apiClientRepository()) {
        spdlog::warn("[AuthMiddleware] apiClientRepository not available");
        return std::nullopt;
    }

    // Hash the key and look up
    std::string keyHash = auth::hashApiKey(apiKey);
    auto client = g_services->apiClientRepository()->findByKeyHash(keyHash);

    if (!client) {
        spdlog::debug("[AuthMiddleware] API key not found");
        return std::nullopt;
    }

    // Check active status
    if (!client->isActive) {
        spdlog::warn("[AuthMiddleware] API key inactive: {}", client->apiKeyPrefix);
        return std::nullopt;
    }

    // Check expiration
    if (client->expiresAt.has_value() && !client->expiresAt->empty()) {
        // Simple string comparison works for ISO 8601 timestamps
        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        struct tm tmBuf;
        gmtime_r(&nowTime, &tmBuf);
        char nowStr[32];
        strftime(nowStr, sizeof(nowStr), "%Y-%m-%d %H:%M:%S", &tmBuf);

        if (std::string(nowStr) > client->expiresAt.value()) {
            spdlog::warn("[AuthMiddleware] API key expired: {}", client->apiKeyPrefix);
            return std::nullopt;
        }
    }

    // Check IP whitelist
    if (!isIpAllowed(client->allowedIps, clientIp)) {
        spdlog::warn("[AuthMiddleware] API key IP denied: {} from {}", client->apiKeyPrefix, clientIp);
        return std::nullopt;
    }

    // Check endpoint permissions (allowed_endpoints)
    if (!client->allowedEndpoints.empty()) {
        bool endpointAllowed = false;
        for (const auto& pattern : client->allowedEndpoints) {
            try {
                std::regex re(pattern);
                if (std::regex_match(path, re)) {
                    endpointAllowed = true;
                    break;
                }
            } catch (const std::regex_error&) {
                // If pattern is not regex, do prefix match
                if (path.find(pattern) == 0) {
                    endpointAllowed = true;
                    break;
                }
            }
        }
        if (!endpointAllowed) {
            spdlog::warn("[AuthMiddleware] API key endpoint denied: {} for {}", client->apiKeyPrefix, path);
            return std::nullopt;
        }
    }

    return client;
}

bool AuthMiddleware::isIpAllowed(
    const std::vector<std::string>& allowedIps,
    const std::string& clientIp) {

    // Empty list = all IPs allowed
    if (allowedIps.empty()) return true;

    for (const auto& allowed : allowedIps) {
        // Exact match
        if (allowed == clientIp) return true;

        // Simple CIDR prefix match (e.g., "192.168.1." matches "192.168.1.100")
        if (allowed.back() == '.' || allowed.find('/') != std::string::npos) {
            // Strip /xx suffix for simple prefix matching
            std::string prefix = allowed;
            auto slashPos = prefix.find('/');
            if (slashPos != std::string::npos) {
                // For /24 = first 3 octets, /16 = first 2 octets
                prefix = prefix.substr(0, slashPos);
                // Simple approach: match the network portion
                auto lastDot = prefix.rfind('.');
                if (lastDot != std::string::npos) {
                    prefix = prefix.substr(0, lastDot + 1);
                }
            }
            if (clientIp.find(prefix) == 0) return true;
        }
    }

    return false;
}

} // namespace middleware

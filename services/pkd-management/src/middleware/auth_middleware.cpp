/** @file auth_middleware.cpp
 *  @brief AuthMiddleware implementation
 */

#include "auth_middleware.h"
#include "../repositories/auth_audit_repository.h"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <regex>

// Global authAuditRepository (defined in main.cpp)
extern std::shared_ptr<repositories::AuthAuditRepository> authAuditRepository;

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
    "^/api/certificates/dsc-nc/report$", // DSC_NC non-conformant report

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

    // Extract Authorization header
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty()) {
        Json::Value resp;
        resp["error"] = "Unauthorized";
        resp["message"] = "Missing Authorization header";
        resp["required_format"] = "Bearer <token>";

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

    // Use global AuthAuditRepository (supports both PostgreSQL and Oracle)
    if (!::authAuditRepository) {
        spdlog::warn("[AuthMiddleware] authAuditRepository not available, skipping audit log");
        return;
    }

    try {
        ::authAuditRepository->insert(
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

} // namespace middleware

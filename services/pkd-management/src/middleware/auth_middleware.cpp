#include "auth_middleware.h"
#include <spdlog/spdlog.h>
#include <libpq-fe.h>
#include <cstdlib>
#include <regex>

namespace middleware {

// Static members initialization
std::set<std::string> AuthMiddleware::publicEndpoints_ = {
    // ========================================================================
    // System & Authentication
    // ========================================================================
    "^/api/health.*",              // Health check endpoints
    "^/api/auth/login$",           // Login endpoint
    "^/api/auth/register$",        // Registration endpoint (future)
    "^/api/auth/logout$",          // Logout endpoint (handler validates JWT)
    "^/api/auth/refresh$",         // Token refresh endpoint (handler validates JWT)
    "^/api/auth/me$",              // Current user info endpoint (handler validates JWT)
    "^/api/auth/users.*",          // User management endpoints (handler validates JWT + admin)
    "^/api/auth/audit-log.*",      // Auth audit log endpoints (handler validates JWT + admin)

    // ========================================================================
    // Dashboard & Statistics (Read-only public information)
    // ========================================================================
    "^/api/upload/countries$",     // Dashboard country statistics (homepage)
    "^/api/upload/countries/detailed.*",  // Detailed country statistics (homepage, with query params)
    "^/api/upload/history.*",      // Upload history (development access)
    "^/api/upload/statistics$",    // Upload statistics
    "^/api/upload/changes.*",      // Recent upload changes
    "^/api/upload/[a-f0-9\\-]+$",  // Upload by ID (deprecated pattern)
    "^/api/upload/detail/[a-f0-9\\-]+$", // Upload detail by ID (fixes 401 error)
    "^/api/upload/[a-f0-9\\-]+/.*", // Upload sub-resources (validations, issues, etc.)

    // ========================================================================
    // File Upload (TEMPORARY for Oracle testing - REMOVE after testing)
    // ========================================================================
    "^/api/upload/ldif$",          // LDIF file upload
    "^/api/upload/masterlist$",    // Master List upload
    "^/api/progress.*",            // Upload progress SSE stream

    // ========================================================================
    // Certificate Search (Public directory service)
    // ========================================================================
    "^/api/certificates/countries$", // Country list for certificate search
    "^/api/certificates/search.*",   // Certificate search with filters
    "^/api/certificates/validation.*", // Certificate validation results (trust chain)
    "^/api/certificates/export/.*",  // Certificate export endpoints

    // ========================================================================
    // ICAO PKD Version Monitoring (Read-only public information)
    // ========================================================================
    "^/api/icao/status$",          // ICAO version status comparison
    "^/api/icao/latest$",          // Latest ICAO version information
    "^/api/icao/history.*",        // Version check history

    // ========================================================================
    // Sync Dashboard (Read-only monitoring)
    // ========================================================================
    "^/api/sync/status$",          // DB-LDAP sync status
    "^/api/sync/stats$",           // Sync statistics
    "^/api/reconcile/history.*",   // Reconciliation history

    // ========================================================================
    // Audit Logs (Read-only monitoring)
    // ========================================================================
    "^/api/audit/operations$",     // Operation audit logs
    "^/api/audit/operations/stats$", // Operation statistics

    // ========================================================================
    // PA (Passive Authentication) Service (Demo/Verification functionality)
    // ========================================================================
    "^/api/pa/verify$",            // PA verification (main function)
    "^/api/pa/parse-sod$",         // Parse SOD (Security Object Document)
    "^/api/pa/parse-dg1$",         // Parse DG1 (MRZ data)
    "^/api/pa/parse-dg2$",         // Parse DG2 (Face image)
    "^/api/pa/parse-mrz-text$",    // Parse MRZ text
    "^/api/pa/history.*",          // PA verification history
    "^/api/pa/statistics$",        // PA statistics
    "^/api/pa/[a-f0-9\\-]+$",      // PA verification detail by ID (UUID)
    "^/api/pa/[a-f0-9\\-]+/datagroups$", // DataGroups detail

    // ========================================================================
    // Static Files & Documentation
    // ========================================================================
    "^/static/.*",                 // Static files (CSS, JS, images)
    "^/api-docs.*",                // API documentation
    "^/swagger-ui/.*"              // Swagger UI

    // NOTE: Audit endpoints removed for security (was TEMPORARY)
    // Admin users must authenticate to access /api/audit/*
};

bool AuthMiddleware::authEnabled_ = true;

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

    // Store claims in session for handler access
    auto session = req->getSession();
    session->insert("user_id", claims->userId);
    session->insert("username", claims->username);
    session->insert("is_admin", claims->isAdmin);

    // Store permissions as JSON string (session doesn't support vector directly)
    Json::Value permsJson(Json::arrayValue);
    for (const auto& perm : claims->permissions) {
        permsJson.append(perm);
    }
    session->insert("permissions", permsJson.toStyledString());

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
    for (const auto& pattern : publicEndpoints_) {
        try {
            if (std::regex_match(path, std::regex(pattern))) {
                return true;
            }
        } catch (const std::regex_error& e) {
            spdlog::error("[AuthMiddleware] Invalid regex pattern '{}': {}",
                          pattern, e.what());
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

    // Get database connection info from environment
    const char* dbHost = std::getenv("DB_HOST");
    const char* dbPort = std::getenv("DB_PORT");
    const char* dbName = std::getenv("DB_NAME");
    const char* dbUser = std::getenv("DB_USER");
    const char* dbPassword = std::getenv("DB_PASSWORD");

    if (!dbHost || !dbPort || !dbName || !dbUser || !dbPassword) {
        spdlog::warn("[AuthMiddleware] Database connection info not available, skipping audit log");
        return;
    }

    // Build connection string
    std::string conninfo = "host=" + std::string(dbHost) +
                          " port=" + std::string(dbPort) +
                          " dbname=" + std::string(dbName) +
                          " user=" + std::string(dbUser) +
                          " password=" + std::string(dbPassword);

    PGconn* conn = PQconnectdb(conninfo.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[AuthMiddleware] Database connection failed: {}",
                      PQerrorMessage(conn));
        PQfinish(conn);
        return;
    }

    // Insert audit log (parameterized query)
    const char* query =
        "INSERT INTO auth_audit_log "
        "(username, event_type, success, ip_address, user_agent, error_message) "
        "VALUES ($1, $2, $3, $4, $5, $6)";

    const char* successStr = success ? "true" : "false";
    const char* paramValues[6] = {
        username.empty() ? nullptr : username.c_str(),
        eventType.c_str(),
        successStr,
        ipAddress.c_str(),
        userAgent.c_str(),
        errorMessage.empty() ? nullptr : errorMessage.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 6, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[AuthMiddleware] Failed to insert audit log: {}",
                      PQerrorMessage(conn));
    }

    PQclear(res);
    PQfinish(conn);
}

} // namespace middleware

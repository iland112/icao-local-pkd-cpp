#pragma once

#include <drogon/HttpFilter.h>
#include "../auth/jwt_service.h"
#include "../domain/models/api_client.h"
#include "api_rate_limiter.h"
#include <memory>
#include <set>
#include <vector>
#include <regex>
#include <mutex>

namespace middleware {

/**
 * @brief Global authentication middleware
 *
 * This filter validates JWT tokens for all incoming requests except public endpoints.
 * It extracts user claims from the token and stores them in the session for downstream handlers.
 *
 * Public endpoints (no authentication required):
 * - /api/health/*
 * - /api/auth/login
 * - /api/auth/register (future)
 * - /static/*
 *
 * Protected endpoints require valid JWT token in Authorization header:
 * - Format: "Bearer <token>"
 */
class AuthMiddleware : public drogon::HttpFilterBase {
public:
    /**
     * @brief Construct AuthMiddleware
     *
     * Loads JWT secret from environment and initializes JWT service.
     */
    AuthMiddleware();

    /**
     * @brief Filter implementation
     *
     * Checks if endpoint is public, validates JWT token, and stores claims in session.
     */
    void doFilter(
        const drogon::HttpRequestPtr& req,
        drogon::FilterCallback&& fcb,
        drogon::FilterChainCallback&& fccb) override;

    /**
     * @brief Add a public endpoint pattern
     *
     * Public endpoints bypass authentication.
     * Patterns are regex strings.
     *
     * @param pattern Regex pattern (e.g., "^/api/health.*")
     */
    static void addPublicEndpoint(const std::string& pattern);

    /**
     * @brief Check if authentication is enabled
     *
     * Can be disabled via environment variable for testing.
     *
     * @return true if authentication is enabled
     */
    static bool isAuthEnabled();

private:
    std::shared_ptr<auth::JwtService> jwtService_;
    static std::set<std::string> publicEndpoints_;
    static std::vector<std::regex> compiledPatterns_;
    static std::once_flag patternsInitFlag_;
    static bool authEnabled_;

    /**
     * @brief Check if path matches any public endpoint pattern
     */
    bool isPublicEndpoint(const std::string& path) const;

    /**
     * @brief Validate API key and return client info
     * @param apiKey Raw API key from X-API-Key header
     * @param path Request path (for endpoint permission check)
     * @param clientIp Client IP address (for IP whitelist check)
     * @return ApiClient if valid, nullopt otherwise
     */
    std::optional<domain::models::ApiClient> validateApiKey(
        const std::string& apiKey,
        const std::string& path,
        const std::string& clientIp);

    /**
     * @brief Check if client IP is allowed
     */
    bool isIpAllowed(const std::vector<std::string>& allowedIps,
                     const std::string& clientIp);

    /**
     * @brief Log authentication event to database
     *
     * Records authentication attempts (success/failure) in auth_audit_log table.
     */
    void logAuthEvent(
        const std::string& username,
        const std::string& eventType,
        bool success,
        const std::string& ipAddress,
        const std::string& userAgent,
        const std::string& errorMessage = "");

    static std::unique_ptr<middleware::ApiRateLimiter> rateLimiter_;

} // namespace middleware

#pragma once

#include <drogon/HttpController.h>
#include "../auth/jwt_service.h"
#include "../auth/password_hash.h"
#include <memory>
#include <libpq-fe.h>

namespace handlers {

/**
 * @brief Authentication endpoints handler
 *
 * Provides authentication-related API endpoints:
 * - POST /api/auth/login - User login with username/password
 * - POST /api/auth/logout - User logout (client-side token deletion)
 * - POST /api/auth/refresh - Refresh JWT token
 * - GET /api/auth/me - Get current user info
 */
class AuthHandler {
public:
    /**
     * @brief Construct AuthHandler
     *
     * Initializes JWT service and database connection.
     *
     * @param dbConnInfo PostgreSQL connection string
     */
    explicit AuthHandler(const std::string& dbConnInfo);

    /**
     * @brief Register authentication routes
     *
     * Registers all auth endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    std::string dbConnInfo_;
    std::shared_ptr<auth::JwtService> jwtService_;

    /**
     * @brief POST /api/auth/login
     *
     * Request body:
     * {
     *   "username": "admin",
     *   "password": "admin123"
     * }
     *
     * Response (success):
     * {
     *   "success": true,
     *   "access_token": "eyJhbGc...",
     *   "token_type": "Bearer",
     *   "expires_in": 3600,
     *   "user": {
     *     "id": "uuid",
     *     "username": "admin",
     *     "email": "admin@example.com",
     *     "full_name": "System Administrator",
     *     "permissions": ["admin", "upload:write", ...],
     *     "is_admin": true
     *   }
     * }
     */
    void handleLogin(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/auth/logout
     *
     * Server-side logout is optional (client deletes token).
     * This endpoint logs the logout event for audit purposes.
     *
     * Response:
     * {
     *   "success": true,
     *   "message": "Logged out successfully"
     * }
     */
    void handleLogout(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/auth/refresh
     *
     * Request body:
     * {
     *   "token": "current_jwt_token"
     * }
     *
     * Response:
     * {
     *   "success": true,
     *   "access_token": "new_jwt_token",
     *   "token_type": "Bearer",
     *   "expires_in": 3600
     * }
     */
    void handleRefresh(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/auth/me
     *
     * Returns current user information from session.
     * Requires authentication.
     *
     * Response:
     * {
     *   "success": true,
     *   "user": {
     *     "id": "uuid",
     *     "username": "admin",
     *     "permissions": [...],
     *     "is_admin": true
     *   }
     * }
     */
    void handleMe(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief Update last_login_at timestamp
     */
    void updateLastLogin(const std::string& userId);

    /**
     * @brief Log authentication event to auth_audit_log
     */
    void logAuthEvent(
        const std::string& userId,
        const std::string& username,
        const std::string& eventType,
        bool success,
        const std::string& ipAddress,
        const std::string& userAgent,
        const std::string& errorMessage = "");
};

} // namespace handlers

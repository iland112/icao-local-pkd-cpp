#pragma once

#include <drogon/HttpController.h>
#include "../auth/jwt_service.h"
#include "../auth/password_hash.h"
#include "../repositories/user_repository.h"
#include "../repositories/auth_audit_repository.h"
#include <memory>

namespace handlers {

/**
 * @brief Authentication endpoints handler
 *
 * Provides authentication-related API endpoints:
 * - POST /api/auth/login - User login with username/password
 * - POST /api/auth/logout - User logout (client-side token deletion)
 * - POST /api/auth/refresh - Refresh JWT token
 * - GET /api/auth/me - Get current user info
 *
 * Uses Repository Pattern for database-agnostic operation.
 */
class AuthHandler {
public:
    /**
     * @brief Construct AuthHandler
     *
     * Initializes JWT service and repository dependencies.
     *
     * @param userRepository User repository (non-owning pointer)
     * @param authAuditRepository Auth audit repository (non-owning pointer)
     */
    explicit AuthHandler(
        repositories::UserRepository* userRepository,
        repositories::AuthAuditRepository* authAuditRepository);

    /**
     * @brief Register authentication routes
     *
     * Registers all auth endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    repositories::UserRepository* userRepository_;
    repositories::AuthAuditRepository* authAuditRepository_;
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
     * @brief Validate JWT token from Authorization header
     *
     * Extracts and validates JWT token from request header.
     * Stores user claims in session if validation successful.
     *
     * @return JwtClaims if valid, std::nullopt otherwise
     */
    std::optional<auth::JwtClaims> validateRequestToken(
        const drogon::HttpRequestPtr& req);

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

    /// @name User Management Endpoints (Admin Only)
    /// @{

    /**
     * @brief GET /api/auth/users - List all users (admin only)
     */
    void handleListUsers(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/auth/users/{userId} - Get user by ID (admin only)
     */
    void handleGetUser(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& userId);

    /**
     * @brief POST /api/auth/users - Create new user (admin only)
     */
    void handleCreateUser(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief PUT /api/auth/users/{userId} - Update user (admin only)
     */
    void handleUpdateUser(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& userId);

    /**
     * @brief DELETE /api/auth/users/{userId} - Delete user (admin only)
     */
    void handleDeleteUser(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& userId);

    /**
     * @brief PUT /api/auth/users/{userId}/password - Change user password (admin or self)
     */
    void handleChangePassword(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& userId);

    /**
     * @brief Check if request is from admin user
     * @return JwtClaims if admin, std::nullopt otherwise
     */
    std::optional<auth::JwtClaims> requireAdmin(
        const drogon::HttpRequestPtr& req);

    /// @}

    /// @name Audit Log Endpoints (Admin Only)
    /// @{

    /**
     * @brief GET /api/auth/audit-log - Get authentication audit logs (admin only)
     *
     * Query params:
     *   - limit: Max records to return (default: 50, max: 200)
     *   - offset: Pagination offset (default: 0)
     *   - user_id: Filter by user ID (optional)
     *   - username: Filter by username (optional)
     *   - event_type: Filter by event type (optional)
     *   - success: Filter by success status (optional: true/false)
     *   - start_date: Filter by start date (ISO 8601, optional)
     *   - end_date: Filter by end date (ISO 8601, optional)
     *
     * Response:
     * {
     *   "success": true,
     *   "total": 100,
     *   "logs": [
     *     {
     *       "id": "uuid",
     *       "user_id": "uuid",
     *       "username": "admin",
     *       "event_type": "LOGIN_SUCCESS",
     *       "ip_address": "192.168.1.100",
     *       "user_agent": "Mozilla/5.0...",
     *       "success": true,
     *       "error_message": "",
     *       "created_at": "2026-01-22T10:00:00Z"
     *     }
     *   ]
     * }
     */
    void handleGetAuditLog(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/auth/audit-log/stats - Get audit log statistics (admin only)
     *
     * Response:
     * {
     *   "success": true,
     *   "stats": {
     *     "total_events": 1000,
     *     "by_event_type": {
     *       "LOGIN_SUCCESS": 800,
     *       "LOGIN_FAILED": 150,
     *       "LOGOUT": 50
     *     },
     *     "by_user": {
     *       "admin": 500,
     *       "testuser": 300
     *     },
     *     "recent_failed_logins": 10,
     *     "last_24h_events": 250
     *   }
     * }
     */
    void handleGetAuditStats(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// @}
};

} // namespace handlers

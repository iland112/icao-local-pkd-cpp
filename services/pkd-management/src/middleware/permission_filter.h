#pragma once

#include <drogon/HttpFilter.h>
#include <vector>
#include <string>

namespace middleware {

/**
 * @brief Permission-based access control filter
 *
 * This filter checks if the authenticated user has required permissions.
 * Must be applied AFTER AuthMiddleware (requires session with user claims).
 *
 * Permission Format: "resource:action"
 * - upload:read   - View upload history
 * - upload:write  - Upload files
 * - cert:read     - Search and view certificates
 * - cert:export   - Export certificates
 * - pa:verify     - Verify Passive Authentication
 * - sync:read     - View sync status
 * - sync:write    - Trigger manual sync (admin)
 * - admin         - Bypass all permission checks
 *
 * Usage:
 *   app.registerHandler("/api/upload/ldif", handler)
 *      .addFilter(std::make_shared<PermissionFilter>(
 *          std::vector<std::string>{"upload:write"}));
 */
class PermissionFilter : public drogon::HttpFilter<PermissionFilter> {
public:
    /**
     * @brief Construct PermissionFilter with required permissions
     *
     * User must have AT LEAST ONE of the required permissions (OR logic).
     * If user is admin, all permission checks are bypassed.
     *
     * @param requiredPermissions List of permissions (OR condition)
     */
    explicit PermissionFilter(const std::vector<std::string>& requiredPermissions);

    /**
     * @brief Filter implementation
     *
     * Checks user permissions from session against required permissions.
     */
    void doFilter(
        const drogon::HttpRequestPtr& req,
        drogon::FilterCallback&& fcb,
        drogon::FilterChainCallback&& fccb) override;

private:
    std::vector<std::string> requiredPermissions_;

    /**
     * @brief Check if user has any of the required permissions
     *
     * @param userPermissions User's permission list from session
     * @param required Required permission
     * @return true if user has permission
     */
    bool hasPermission(
        const std::vector<std::string>& userPermissions,
        const std::string& required) const;

    /**
     * @brief Parse permissions JSON string from session
     *
     * @param permsJsonStr JSON string of permissions array
     * @return Vector of permission strings
     */
    std::vector<std::string> parsePermissions(const std::string& permsJsonStr) const;
};

/**
 * @brief Helper function to create PermissionFilter
 *
 * Convenience function for route registration.
 *
 * @param permissions Required permissions
 * @return Shared pointer to PermissionFilter
 */
inline std::shared_ptr<PermissionFilter> requirePermissions(
    const std::vector<std::string>& permissions) {
    return std::make_shared<PermissionFilter>(permissions);
}

/**
 * @brief Helper function for single permission
 *
 * @param permission Single required permission
 * @return Shared pointer to PermissionFilter
 */
inline std::shared_ptr<PermissionFilter> requirePermission(
    const std::string& permission) {
    return std::make_shared<PermissionFilter>(
        std::vector<std::string>{permission});
}

} // namespace middleware

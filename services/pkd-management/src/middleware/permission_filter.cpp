#include "permission_filter.h"
#include <spdlog/spdlog.h>
#include <json/json.h>
#include <algorithm>

namespace middleware {

PermissionFilter::PermissionFilter(const std::vector<std::string>& requiredPermissions)
    : requiredPermissions_(requiredPermissions) {

    std::ostringstream oss;
    for (size_t i = 0; i < requiredPermissions_.size(); ++i) {
        oss << requiredPermissions_[i];
        if (i < requiredPermissions_.size() - 1) {
            oss << ", ";
        }
    }

    spdlog::debug("[PermissionFilter] Initialized with required permissions: {}",
                  oss.str());
}

void PermissionFilter::doFilter(
    const drogon::HttpRequestPtr& req,
    drogon::FilterCallback&& fcb,
    drogon::FilterChainCallback&& fccb) {

    auto session = req->getSession();

    // Get user info from session (set by AuthMiddleware)
    std::string username = session->get<std::string>("username");
    bool isAdmin = session->get<bool>("is_admin");
    std::string permsJson = session->get<std::string>("permissions");

    if (username.empty()) {
        Json::Value resp;
        resp["error"] = "Forbidden";
        resp["message"] = "User session not found. Authentication required.";

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k403Forbidden);
        fcb(response);

        spdlog::warn("[PermissionFilter] Session not found for {}", req->path());
        return;
    }

    // Admin bypasses all permission checks
    if (isAdmin) {
        spdlog::debug("[PermissionFilter] Admin user {} bypassing permission check for {}",
                      username, req->path());
        fccb();
        return;
    }

    // Parse user permissions from JSON
    std::vector<std::string> userPermissions = parsePermissions(permsJson);

    // Check if user has any of the required permissions
    bool hasAnyPermission = false;
    for (const auto& required : requiredPermissions_) {
        if (hasPermission(userPermissions, required)) {
            hasAnyPermission = true;
            break;
        }
    }

    if (!hasAnyPermission) {
        Json::Value resp;
        resp["error"] = "Forbidden";
        resp["message"] = "Insufficient permissions";

        Json::Value requiredJson(Json::arrayValue);
        for (const auto& perm : requiredPermissions_) {
            requiredJson.append(perm);
        }
        resp["required_permissions"] = requiredJson;

        Json::Value userPermsJson(Json::arrayValue);
        for (const auto& perm : userPermissions) {
            userPermsJson.append(perm);
        }
        resp["user_permissions"] = userPermsJson;

        auto response = drogon::HttpResponse::newHttpJsonResponse(resp);
        response->setStatusCode(drogon::k403Forbidden);
        fcb(response);

        spdlog::warn("[PermissionFilter] User {} denied access to {} (missing permissions)",
                     username, req->path());
        return;
    }

    spdlog::debug("[PermissionFilter] User {} granted access to {}",
                  username, req->path());

    // Permission granted, continue to handler
    fccb();
}

bool PermissionFilter::hasPermission(
    const std::vector<std::string>& userPermissions,
    const std::string& required) const {

    // Check for exact match
    if (std::find(userPermissions.begin(), userPermissions.end(), required) != userPermissions.end()) {
        return true;
    }

    // Check for "admin" permission (wildcard)
    if (std::find(userPermissions.begin(), userPermissions.end(), "admin") != userPermissions.end()) {
        return true;
    }

    return false;
}

std::vector<std::string> PermissionFilter::parsePermissions(const std::string& permsJsonStr) const {
    std::vector<std::string> permissions;

    try {
        Json::Value permsJson;
        Json::CharReaderBuilder reader;
        std::istringstream iss(permsJsonStr);
        std::string errs;

        if (Json::parseFromStream(reader, iss, &permsJson, &errs)) {
            if (permsJson.isArray()) {
                for (const auto& perm : permsJson) {
                    if (perm.isString()) {
                        permissions.push_back(perm.asString());
                    }
                }
            }
        } else {
            spdlog::error("[PermissionFilter] Failed to parse permissions JSON: {}", errs);
        }
    } catch (const std::exception& e) {
        spdlog::error("[PermissionFilter] Exception parsing permissions: {}", e.what());
    }

    return permissions;
}

} // namespace middleware

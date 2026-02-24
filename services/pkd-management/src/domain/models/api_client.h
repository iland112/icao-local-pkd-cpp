#pragma once

/**
 * @file api_client.h
 * @brief ApiClient domain model — external client agent API key management
 */

#include <string>
#include <vector>
#include <optional>

namespace domain {
namespace models {

struct ApiClient {
    std::string id;
    std::string clientName;
    std::string apiKeyHash;
    std::string apiKeyPrefix;
    std::optional<std::string> description;

    // Access control
    std::vector<std::string> permissions;
    std::vector<std::string> allowedEndpoints;
    std::vector<std::string> allowedIps;

    // Rate limiting
    int rateLimitPerMinute = 60;
    int rateLimitPerHour = 1000;
    int rateLimitPerDay = 10000;

    // Status
    bool isActive = true;
    std::optional<std::string> expiresAt;
    std::optional<std::string> lastUsedAt;
    int64_t totalRequests = 0;

    // Audit
    std::optional<std::string> createdBy;
    std::string createdAt;
    std::string updatedAt;
};

} // namespace models
} // namespace domain

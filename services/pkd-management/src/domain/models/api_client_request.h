#pragma once

/**
 * @file api_client_request.h
 * @brief ApiClientRequest domain model — external user API access request (approval workflow)
 */

#include <string>
#include <vector>
#include <optional>

namespace domain {
namespace models {

struct ApiClientRequest {
    std::string id;

    // Requester information
    std::string requesterName;
    std::string requesterOrg;
    std::optional<std::string> requesterContactPhone;
    std::string requesterContactEmail;
    std::string requestReason;

    // Desired API client configuration
    std::string clientName;
    std::optional<std::string> description;
    std::string deviceType = "SERVER";  // SERVER, DESKTOP, MOBILE, OTHER
    std::vector<std::string> permissions;
    std::vector<std::string> allowedIps;  // Requester proposes, admin confirms

    // Approval workflow
    std::string status = "PENDING";  // PENDING, APPROVED, REJECTED
    std::optional<std::string> reviewedBy;
    std::optional<std::string> reviewedAt;
    std::optional<std::string> reviewComment;
    std::optional<std::string> approvedClientId;

    // Audit
    std::string createdAt;
    std::string updatedAt;
};

} // namespace models
} // namespace domain

#pragma once

/**
 * @file api_client_request_repository.h
 * @brief Repository for api_client_requests table — external user API access request workflow
 */

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"
#include "../domain/models/api_client_request.h"

namespace repositories {

class ApiClientRequestRepository {
public:
    explicit ApiClientRequestRepository(common::IQueryExecutor* executor);
    ~ApiClientRequestRepository();

    /** Insert a new request (public, no auth required) */
    std::string insert(const domain::models::ApiClientRequest& request);

    /** Find request by ID */
    std::optional<domain::models::ApiClientRequest> findById(const std::string& id);

    /** Find all requests with optional status filter */
    std::vector<domain::models::ApiClientRequest> findAll(
        const std::string& statusFilter = "",
        int limit = 100, int offset = 0);

    /** Count requests with optional status filter */
    int countAll(const std::string& statusFilter = "");

    /** Update request status (approve/reject) */
    bool updateStatus(const std::string& id, const std::string& status,
                      const std::string& reviewedBy,
                      const std::string& reviewComment,
                      const std::string& approvedClientId = "");

private:
    common::IQueryExecutor* executor_;

    domain::models::ApiClientRequest jsonToModel(const Json::Value& row);
    std::vector<std::string> parseJsonArray(const Json::Value& val);
};

} // namespace repositories

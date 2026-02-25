#pragma once

/**
 * @file api_client_repository.h
 * @brief Repository for api_clients table — external client API key management
 *
 * Uses IQueryExecutor for database-agnostic operation (PostgreSQL + Oracle).
 */

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"
#include "../domain/models/api_client.h"

namespace repositories {

class ApiClientRepository {
public:
    explicit ApiClientRepository(common::IQueryExecutor* executor);
    ~ApiClientRepository();

    /** Find client by API key hash (for authentication) */
    std::optional<domain::models::ApiClient> findByKeyHash(const std::string& keyHash);

    /** Find client by ID */
    std::optional<domain::models::ApiClient> findById(const std::string& id);

    /** Find all clients (with optional filters) */
    std::vector<domain::models::ApiClient> findAll(
        bool activeOnly = false,
        int limit = 100, int offset = 0);

    /** Count all clients */
    int countAll(bool activeOnly = false);

    /** Insert a new client, returns generated ID */
    std::string insert(const domain::models::ApiClient& client);

    /** Update client (name, description, permissions, rate limits) */
    bool update(const domain::models::ApiClient& client);

    /** Update API key hash and prefix (for key regeneration) */
    bool updateKeyHash(const std::string& id, const std::string& keyHash, const std::string& keyPrefix);

    /** Deactivate a client (soft delete) */
    bool deactivate(const std::string& id);

    /** Update last_used_at and increment total_requests */
    bool updateUsage(const std::string& id);

    /** Insert usage log entry (async-friendly) */
    bool insertUsageLog(const std::string& clientId, const std::string& clientName,
                        const std::string& endpoint, const std::string& method,
                        int statusCode, int responseTimeMs,
                        const std::string& ipAddress, const std::string& userAgent);

    /** Get usage statistics for a client */
    Json::Value getUsageStats(const std::string& clientId, int days = 7);

private:
    common::IQueryExecutor* executor_;

    domain::models::ApiClient jsonToModel(const Json::Value& row);
    std::vector<std::string> parseJsonArray(const Json::Value& val);
    bool parseBool(const Json::Value& val);
};

} // namespace repositories

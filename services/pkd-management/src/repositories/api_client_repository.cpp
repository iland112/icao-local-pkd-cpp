/** @file api_client_repository.cpp
 *  @brief ApiClientRepository implementation
 */

#include "api_client_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

ApiClientRepository::ApiClientRepository(common::IQueryExecutor* executor)
    : executor_(executor)
{
    if (!executor_) {
        throw std::invalid_argument("ApiClientRepository: executor cannot be nullptr");
    }
    spdlog::debug("[ApiClientRepository] Initialized (DB type: {})", executor_->getDatabaseType());
}

ApiClientRepository::~ApiClientRepository() {}

std::optional<domain::models::ApiClient> ApiClientRepository::findByKeyHash(const std::string& keyHash) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            query =
                "SELECT id, client_name, api_key_hash, api_key_prefix, description, "
                "  TO_CHAR(permissions) AS permissions, TO_CHAR(allowed_endpoints) AS allowed_endpoints, "
                "  TO_CHAR(allowed_ips) AS allowed_ips, "
                "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                "  is_active, expires_at, last_used_at, total_requests, "
                "  created_by, created_at, updated_at "
                "FROM api_clients WHERE api_key_hash = $1";
        } else {
            query =
                "SELECT id, client_name, api_key_hash, api_key_prefix, description, "
                "  permissions, allowed_endpoints, allowed_ips, "
                "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                "  is_active, expires_at, last_used_at, total_requests, "
                "  created_by, created_at, updated_at "
                "FROM api_clients WHERE api_key_hash = $1";
        }

        std::vector<std::string> params = { keyHash };
        Json::Value result = executor_->executeQuery(query, params);

        if (result.isArray() && result.size() > 0) {
            return jsonToModel(result[0]);
        }
        return std::nullopt;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] findByKeyHash failed: {}", e.what());
        return std::nullopt;
    }
}

std::optional<domain::models::ApiClient> ApiClientRepository::findById(const std::string& id) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            query =
                "SELECT id, client_name, api_key_hash, api_key_prefix, description, "
                "  TO_CHAR(permissions) AS permissions, TO_CHAR(allowed_endpoints) AS allowed_endpoints, "
                "  TO_CHAR(allowed_ips) AS allowed_ips, "
                "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                "  is_active, expires_at, last_used_at, total_requests, "
                "  created_by, created_at, updated_at "
                "FROM api_clients WHERE id = $1";
        } else {
            query =
                "SELECT id, client_name, api_key_hash, api_key_prefix, description, "
                "  permissions, allowed_endpoints, allowed_ips, "
                "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                "  is_active, expires_at, last_used_at, total_requests, "
                "  created_by, created_at, updated_at "
                "FROM api_clients WHERE id = $1";
        }

        std::vector<std::string> params = { id };
        Json::Value result = executor_->executeQuery(query, params);

        if (result.isArray() && result.size() > 0) {
            return jsonToModel(result[0]);
        }
        return std::nullopt;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] findById failed: {}", e.what());
        return std::nullopt;
    }
}

std::vector<domain::models::ApiClient> ApiClientRepository::findAll(
    bool activeOnly, int limit, int offset) {

    try {
        std::string dbType = executor_->getDatabaseType();
        std::string trueVal = common::db::boolLiteral(dbType, true);

        std::string query;
        if (dbType == "oracle") {
            query =
                "SELECT id, client_name, api_key_hash, api_key_prefix, description, "
                "  TO_CHAR(permissions) AS permissions, TO_CHAR(allowed_endpoints) AS allowed_endpoints, "
                "  TO_CHAR(allowed_ips) AS allowed_ips, "
                "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                "  is_active, expires_at, last_used_at, total_requests, "
                "  created_by, created_at, updated_at "
                "FROM api_clients WHERE 1=1";
        } else {
            query =
                "SELECT id, client_name, api_key_hash, api_key_prefix, description, "
                "  permissions, allowed_endpoints, allowed_ips, "
                "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                "  is_active, expires_at, last_used_at, total_requests, "
                "  created_by, created_at, updated_at "
                "FROM api_clients WHERE 1=1";
        }

        if (activeOnly) {
            query += " AND is_active = " + trueVal;
        }

        query += " ORDER BY created_at DESC ";
        query += common::db::paginationClause(dbType, limit, offset);

        Json::Value result = executor_->executeQuery(query);

        std::vector<domain::models::ApiClient> items;
        if (result.isArray()) {
            for (const auto& row : result) {
                items.push_back(jsonToModel(row));
            }
        }
        return items;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] findAll failed: {}", e.what());
        return {};
    }
}

int ApiClientRepository::countAll(bool activeOnly) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string trueVal = common::db::boolLiteral(dbType, true);

        std::string query = "SELECT COUNT(*) FROM api_clients WHERE 1=1";
        if (activeOnly) {
            query += " AND is_active = " + trueVal;
        }

        Json::Value result = executor_->executeScalar(query);
        return common::db::scalarToInt(result);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] countAll failed: {}", e.what());
        return 0;
    }
}

std::string ApiClientRepository::insert(const domain::models::ApiClient& client) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string isActiveVal = common::db::boolLiteral(dbType, client.isActive);

        // Build JSON strings for array fields
        Json::Value permsJson(Json::arrayValue);
        for (const auto& p : client.permissions) permsJson.append(p);

        Json::Value endpointsJson(Json::arrayValue);
        for (const auto& e : client.allowedEndpoints) endpointsJson.append(e);

        Json::Value ipsJson(Json::arrayValue);
        for (const auto& ip : client.allowedIps) ipsJson.append(ip);

        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string permsStr = Json::writeString(writerBuilder, permsJson);
        std::string endpointsStr = Json::writeString(writerBuilder, endpointsJson);
        std::string ipsStr = Json::writeString(writerBuilder, ipsJson);

        // Build query and params — $11 is expiresAt (only if present), last param is createdBy
        std::string query;
        std::vector<std::string> params = {
            client.clientName,          // $1
            client.apiKeyHash,          // $2
            client.apiKeyPrefix,        // $3
            client.description.value_or(""),  // $4
            permsStr,                   // $5
            endpointsStr,               // $6
            ipsStr,                     // $7
            std::to_string(client.rateLimitPerMinute),   // $8
            std::to_string(client.rateLimitPerHour),     // $9
            std::to_string(client.rateLimitPerDay),      // $10
        };

        if (client.expiresAt.has_value()) {
            params.push_back(client.expiresAt.value());        // $11
            params.push_back(client.createdBy.value_or(""));   // $12
        } else {
            params.push_back(client.createdBy.value_or(""));   // $11
        }

        if (dbType == "oracle") {
            if (client.expiresAt.has_value()) {
                query =
                    "INSERT INTO api_clients (client_name, api_key_hash, api_key_prefix, description, "
                    "  permissions, allowed_endpoints, allowed_ips, "
                    "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                    "  is_active, expires_at, created_by) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, " + isActiveVal + ", "
                    "  TO_TIMESTAMP($11, 'YYYY-MM-DD\"T\"HH24:MI:SS'), $12)";
            } else {
                query =
                    "INSERT INTO api_clients (client_name, api_key_hash, api_key_prefix, description, "
                    "  permissions, allowed_endpoints, allowed_ips, "
                    "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                    "  is_active, expires_at, created_by) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, " + isActiveVal + ", "
                    "  NULL, $11)";
            }
        } else {
            if (client.expiresAt.has_value()) {
                query =
                    "INSERT INTO api_clients (client_name, api_key_hash, api_key_prefix, description, "
                    "  permissions, allowed_endpoints, allowed_ips, "
                    "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                    "  is_active, expires_at, created_by) "
                    "VALUES ($1, $2, $3, $4, $5::jsonb, $6::jsonb, $7::jsonb, $8, $9, $10, " + isActiveVal + ", "
                    "  $11::timestamp, $12) "
                    "RETURNING id";
            } else {
                query =
                    "INSERT INTO api_clients (client_name, api_key_hash, api_key_prefix, description, "
                    "  permissions, allowed_endpoints, allowed_ips, "
                    "  rate_limit_per_minute, rate_limit_per_hour, rate_limit_per_day, "
                    "  is_active, expires_at, created_by) "
                    "VALUES ($1, $2, $3, $4, $5::jsonb, $6::jsonb, $7::jsonb, $8, $9, $10, " + isActiveVal + ", "
                    "  NULL, $11) "
                    "RETURNING id";
            }
        }

        if (dbType == "oracle") {
            executor_->executeCommand(query, params);
            // Retrieve the generated ID
            Json::Value idResult = executor_->executeQuery(
                "SELECT id FROM api_clients WHERE api_key_hash = $1",
                { client.apiKeyHash });
            if (idResult.isArray() && idResult.size() > 0) {
                return idResult[0].get("id", "").asString();
            }
            return "";
        } else {
            Json::Value result = executor_->executeQuery(query, params);
            if (result.isArray() && result.size() > 0) {
                std::string id = result[0].get("id", "").asString();
                spdlog::info("[ApiClientRepository] Inserted client: {} (prefix: {})",
                             client.clientName, client.apiKeyPrefix);
                return id;
            }
            return "";
        }

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] Insert failed: {}", e.what());
        return "";
    }
}

bool ApiClientRepository::update(const domain::models::ApiClient& client) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string isActiveVal = common::db::boolLiteral(dbType, client.isActive);
        std::string tsFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

        Json::Value permsJson(Json::arrayValue);
        for (const auto& p : client.permissions) permsJson.append(p);

        Json::Value endpointsJson(Json::arrayValue);
        for (const auto& e : client.allowedEndpoints) endpointsJson.append(e);

        Json::Value ipsJson(Json::arrayValue);
        for (const auto& ip : client.allowedIps) ipsJson.append(ip);

        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string permsStr = Json::writeString(writerBuilder, permsJson);
        std::string endpointsStr = Json::writeString(writerBuilder, endpointsJson);
        std::string ipsStr = Json::writeString(writerBuilder, ipsJson);

        std::string jsonCast = (dbType == "oracle") ? "" : "::jsonb";

        std::string query =
            "UPDATE api_clients SET "
            "  client_name = $1, description = $2, "
            "  permissions = $3" + jsonCast + ", allowed_endpoints = $4" + jsonCast + ", allowed_ips = $5" + jsonCast + ", "
            "  rate_limit_per_minute = $6, rate_limit_per_hour = $7, rate_limit_per_day = $8, "
            "  is_active = " + isActiveVal + ", updated_at = " + tsFunc + " "
            "WHERE id = $9";

        std::vector<std::string> params = {
            client.clientName,
            client.description.value_or(""),
            permsStr,
            endpointsStr,
            ipsStr,
            std::to_string(client.rateLimitPerMinute),
            std::to_string(client.rateLimitPerHour),
            std::to_string(client.rateLimitPerDay),
            client.id
        };

        int rowsAffected = executor_->executeCommand(query, params);
        if (rowsAffected > 0) {
            spdlog::info("[ApiClientRepository] Updated client: {}", client.id);
            return true;
        }
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] Update failed: {}", e.what());
        return false;
    }
}

bool ApiClientRepository::updateKeyHash(const std::string& id,
                                        const std::string& keyHash,
                                        const std::string& keyPrefix) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string tsFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

        std::string query =
            "UPDATE api_clients SET api_key_hash = $1, api_key_prefix = $2, "
            "  updated_at = " + tsFunc + " WHERE id = $3";

        std::vector<std::string> params = { keyHash, keyPrefix, id };
        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[ApiClientRepository] Updated key hash for client: {}", id);
            return true;
        }
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] updateKeyHash failed: {}", e.what());
        return false;
    }
}

bool ApiClientRepository::deactivate(const std::string& id) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string falseVal = common::db::boolLiteral(dbType, false);
        std::string tsFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

        std::string query =
            "UPDATE api_clients SET is_active = " + falseVal + ", "
            "  updated_at = " + tsFunc + " WHERE id = $1";

        std::vector<std::string> params = { id };
        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[ApiClientRepository] Deactivated client: {}", id);
            return true;
        }
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] Deactivate failed: {}", e.what());
        return false;
    }
}

bool ApiClientRepository::updateUsage(const std::string& id) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string tsFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

        std::string query =
            "UPDATE api_clients SET last_used_at = " + tsFunc + ", "
            "  total_requests = total_requests + 1 WHERE id = $1";

        std::vector<std::string> params = { id };
        executor_->executeCommand(query, params);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] updateUsage failed: {}", e.what());
        return false;
    }
}

bool ApiClientRepository::insertUsageLog(
    const std::string& clientId, const std::string& clientName,
    const std::string& endpoint, const std::string& method,
    int statusCode, int responseTimeMs,
    const std::string& ipAddress, const std::string& userAgent) {

    try {
        const char* query =
            "INSERT INTO api_client_usage_log "
            "  (client_id, client_name, endpoint, method, status_code, "
            "   response_time_ms, ip_address, user_agent) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)";

        std::vector<std::string> params = {
            clientId, clientName, endpoint, method,
            std::to_string(statusCode), std::to_string(responseTimeMs),
            ipAddress, userAgent
        };

        executor_->executeCommand(query, params);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] insertUsageLog failed: {}", e.what());
        return false;
    }
}

Json::Value ApiClientRepository::getUsageStats(const std::string& clientId, int days) {
    try {
        std::string dbType = executor_->getDatabaseType();
        Json::Value stats;

        // Total requests in period
        std::string countQuery;
        if (dbType == "oracle") {
            countQuery =
                "SELECT COUNT(*) FROM api_client_usage_log "
                "WHERE client_id = $1 AND created_at >= SYSTIMESTAMP - INTERVAL '" + std::to_string(days) + "' DAY";
        } else {
            countQuery =
                "SELECT COUNT(*) FROM api_client_usage_log "
                "WHERE client_id = $1 AND created_at >= NOW() - INTERVAL '" + std::to_string(days) + " days'";
        }

        std::vector<std::string> params = { clientId };
        Json::Value countResult = executor_->executeScalar(countQuery, params);
        stats["totalRequests"] = common::db::scalarToInt(countResult);

        // Top endpoints
        std::string topQuery;
        if (dbType == "oracle") {
            topQuery =
                "SELECT endpoint, COUNT(*) AS cnt FROM api_client_usage_log "
                "WHERE client_id = $1 AND created_at >= SYSTIMESTAMP - INTERVAL '" + std::to_string(days) + "' DAY "
                "GROUP BY endpoint ORDER BY cnt DESC FETCH FIRST 10 ROWS ONLY";
        } else {
            topQuery =
                "SELECT endpoint, COUNT(*) AS cnt FROM api_client_usage_log "
                "WHERE client_id = $1 AND created_at >= NOW() - INTERVAL '" + std::to_string(days) + " days' "
                "GROUP BY endpoint ORDER BY cnt DESC LIMIT 10";
        }

        Json::Value topResult = executor_->executeQuery(topQuery, params);
        Json::Value topEndpoints(Json::arrayValue);
        if (topResult.isArray()) {
            for (const auto& row : topResult) {
                Json::Value ep;
                ep["endpoint"] = row.get("endpoint", "").asString();
                ep["count"] = common::db::scalarToInt(row.get("cnt", 0));
                topEndpoints.append(ep);
            }
        }
        stats["topEndpoints"] = topEndpoints;

        return stats;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRepository] getUsageStats failed: {}", e.what());
        return Json::Value();
    }
}

// --- Private Helpers ---

domain::models::ApiClient ApiClientRepository::jsonToModel(const Json::Value& row) {
    domain::models::ApiClient client;

    client.id = row.get("id", "").asString();
    client.clientName = row.get("client_name", "").asString();
    client.apiKeyHash = row.get("api_key_hash", "").asString();
    client.apiKeyPrefix = row.get("api_key_prefix", "").asString();

    std::string desc = row.get("description", "").asString();
    if (!desc.empty()) client.description = desc;

    client.permissions = parseJsonArray(row.get("permissions", "[]"));
    client.allowedEndpoints = parseJsonArray(row.get("allowed_endpoints", "[]"));
    client.allowedIps = parseJsonArray(row.get("allowed_ips", "[]"));

    client.rateLimitPerMinute = common::db::scalarToInt(row.get("rate_limit_per_minute", 60));
    client.rateLimitPerHour = common::db::scalarToInt(row.get("rate_limit_per_hour", 1000));
    client.rateLimitPerDay = common::db::scalarToInt(row.get("rate_limit_per_day", 10000));

    client.isActive = parseBool(row.get("is_active", true));

    std::string expiresAt = row.get("expires_at", "").asString();
    if (!expiresAt.empty()) client.expiresAt = expiresAt;

    std::string lastUsedAt = row.get("last_used_at", "").asString();
    if (!lastUsedAt.empty()) client.lastUsedAt = lastUsedAt;

    // total_requests can be large
    Json::Value trVal = row.get("total_requests", 0);
    if (trVal.isString()) {
        try { client.totalRequests = std::stoll(trVal.asString()); }
        catch (...) { client.totalRequests = 0; }
    } else if (trVal.isInt64()) {
        client.totalRequests = trVal.asInt64();
    } else if (trVal.isInt()) {
        client.totalRequests = trVal.asInt();
    }

    std::string createdBy = row.get("created_by", "").asString();
    if (!createdBy.empty()) client.createdBy = createdBy;

    client.createdAt = row.get("created_at", "").asString();
    client.updatedAt = row.get("updated_at", "").asString();

    return client;
}

std::vector<std::string> ApiClientRepository::parseJsonArray(const Json::Value& val) {
    std::vector<std::string> result;

    if (val.isArray()) {
        for (const auto& item : val) {
            result.push_back(item.asString());
        }
        return result;
    }

    // Parse JSON string (Oracle CLOB or PostgreSQL text)
    if (val.isString()) {
        std::string str = val.asString();
        if (str.empty() || str == "[]") return result;

        Json::CharReaderBuilder reader;
        std::istringstream stream(str);
        Json::Value parsed;
        std::string errs;
        if (Json::parseFromStream(reader, stream, &parsed, &errs) && parsed.isArray()) {
            for (const auto& item : parsed) {
                result.push_back(item.asString());
            }
        }
    }

    return result;
}

bool ApiClientRepository::parseBool(const Json::Value& val) {
    if (val.isBool()) return val.asBool();
    if (val.isString()) {
        std::string s = val.asString();
        return (s == "t" || s == "true" || s == "1");
    }
    if (val.isInt()) return (val.asInt() != 0);
    return true;
}

} // namespace repositories

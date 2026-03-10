/** @file api_client_request_repository.cpp
 *  @brief ApiClientRequestRepository implementation
 */

#include "api_client_request_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

ApiClientRequestRepository::ApiClientRequestRepository(common::IQueryExecutor* executor)
    : executor_(executor)
{
    if (!executor_) {
        throw std::invalid_argument("ApiClientRequestRepository: executor cannot be nullptr");
    }
    spdlog::debug("[ApiClientRequestRepository] Initialized (DB type: {})", executor_->getDatabaseType());
}

ApiClientRequestRepository::~ApiClientRequestRepository() {}

std::string ApiClientRequestRepository::insert(const domain::models::ApiClientRequest& request) {
    try {
        std::string dbType = executor_->getDatabaseType();

        // Build JSON strings for array fields
        Json::Value permsJson(Json::arrayValue);
        for (const auto& p : request.permissions) permsJson.append(p);

        Json::Value ipsJson(Json::arrayValue);
        for (const auto& ip : request.allowedIps) ipsJson.append(ip);

        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string permsStr = Json::writeString(writerBuilder, permsJson);
        std::string ipsStr = Json::writeString(writerBuilder, ipsJson);

        std::string jsonCast = (dbType == "oracle") ? "" : "::jsonb";

        std::string query;
        if (dbType == "oracle") {
            query =
                "INSERT INTO api_client_requests ("
                "  requester_name, requester_org, requester_contact_phone, requester_contact_email, "
                "  request_reason, client_name, description, device_type, "
                "  permissions, allowed_ips"
                ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)";
        } else {
            query =
                "INSERT INTO api_client_requests ("
                "  requester_name, requester_org, requester_contact_phone, requester_contact_email, "
                "  request_reason, client_name, description, device_type, "
                "  permissions, allowed_ips"
                ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, "
                "  $9" + jsonCast + ", $10" + jsonCast + ") RETURNING id";
        }

        std::vector<std::string> params = {
            request.requesterName,                          // $1
            request.requesterOrg,                           // $2
            request.requesterContactPhone.value_or(""),     // $3
            request.requesterContactEmail,                  // $4
            request.requestReason,                          // $5
            request.clientName,                             // $6
            request.description.value_or(""),               // $7
            request.deviceType,                             // $8
            permsStr,                                       // $9
            ipsStr,                                         // $10
        };

        if (dbType == "oracle") {
            executor_->executeCommand(query, params);
            // Retrieve the generated ID by most recent insert
            Json::Value idResult = executor_->executeQuery(
                "SELECT id FROM api_client_requests "
                "WHERE requester_contact_email = $1 AND client_name = $2 "
                "ORDER BY created_at DESC FETCH FIRST 1 ROWS ONLY",
                { request.requesterContactEmail, request.clientName });
            if (idResult.isArray() && idResult.size() > 0) {
                return idResult[0].get("id", "").asString();
            }
            return "";
        } else {
            Json::Value result = executor_->executeQuery(query, params);
            if (result.isArray() && result.size() > 0) {
                std::string id = result[0].get("id", "").asString();
                spdlog::info("[ApiClientRequestRepository] Inserted request from: {} ({})",
                             request.requesterName, request.requesterOrg);
                return id;
            }
            return "";
        }

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestRepository] Insert failed: {}", e.what());
        return "";
    }
}

std::optional<domain::models::ApiClientRequest> ApiClientRequestRepository::findById(const std::string& id) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            query =
                "SELECT id, requester_name, requester_org, requester_contact_phone, requester_contact_email, "
                "  request_reason, client_name, description, device_type, "
                "  TO_CHAR(permissions) AS permissions, TO_CHAR(allowed_ips) AS allowed_ips, "
                "  status, reviewed_by, reviewed_at, TO_CHAR(review_comment) AS review_comment, "
                "  approved_client_id, created_at, updated_at "
                "FROM api_client_requests WHERE id = $1";
        } else {
            query =
                "SELECT id, requester_name, requester_org, requester_contact_phone, requester_contact_email, "
                "  request_reason, client_name, description, device_type, "
                "  permissions, allowed_ips, "
                "  status, reviewed_by, reviewed_at, review_comment, "
                "  approved_client_id, created_at, updated_at "
                "FROM api_client_requests WHERE id = $1";
        }

        std::vector<std::string> params = { id };
        Json::Value result = executor_->executeQuery(query, params);

        if (result.isArray() && result.size() > 0) {
            return jsonToModel(result[0]);
        }
        return std::nullopt;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestRepository] findById failed: {}", e.what());
        return std::nullopt;
    }
}

std::vector<domain::models::ApiClientRequest> ApiClientRequestRepository::findAll(
    const std::string& statusFilter, int limit, int offset) {

    try {
        std::string dbType = executor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            query =
                "SELECT id, requester_name, requester_org, requester_contact_phone, requester_contact_email, "
                "  request_reason, client_name, description, device_type, "
                "  TO_CHAR(permissions) AS permissions, TO_CHAR(allowed_ips) AS allowed_ips, "
                "  status, reviewed_by, reviewed_at, TO_CHAR(review_comment) AS review_comment, "
                "  approved_client_id, created_at, updated_at "
                "FROM api_client_requests WHERE 1=1";
        } else {
            query =
                "SELECT id, requester_name, requester_org, requester_contact_phone, requester_contact_email, "
                "  request_reason, client_name, description, device_type, "
                "  permissions, allowed_ips, "
                "  status, reviewed_by, reviewed_at, review_comment, "
                "  approved_client_id, created_at, updated_at "
                "FROM api_client_requests WHERE 1=1";
        }

        std::vector<std::string> params;
        if (!statusFilter.empty()) {
            query += " AND status = $1";
            params.push_back(statusFilter);
        }

        query += " ORDER BY created_at DESC ";
        query += common::db::paginationClause(dbType, limit, offset);

        Json::Value result;
        if (params.empty()) {
            result = executor_->executeQuery(query);
        } else {
            result = executor_->executeQuery(query, params);
        }

        std::vector<domain::models::ApiClientRequest> items;
        if (result.isArray()) {
            for (const auto& row : result) {
                items.push_back(jsonToModel(row));
            }
        }
        return items;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestRepository] findAll failed: {}", e.what());
        return {};
    }
}

int ApiClientRequestRepository::countAll(const std::string& statusFilter) {
    try {
        std::string query = "SELECT COUNT(*) FROM api_client_requests WHERE 1=1";
        std::vector<std::string> params;

        if (!statusFilter.empty()) {
            query += " AND status = $1";
            params.push_back(statusFilter);
        }

        Json::Value result;
        if (params.empty()) {
            result = executor_->executeScalar(query);
        } else {
            result = executor_->executeScalar(query, params);
        }
        return common::db::scalarToInt(result);

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestRepository] countAll failed: {}", e.what());
        return 0;
    }
}

bool ApiClientRequestRepository::updateStatus(
    const std::string& id, const std::string& status,
    const std::string& reviewedBy, const std::string& reviewComment,
    const std::string& approvedClientId) {

    try {
        std::string dbType = executor_->getDatabaseType();
        std::string tsFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

        std::string query;
        std::vector<std::string> params;

        if (!approvedClientId.empty()) {
            query =
                "UPDATE api_client_requests SET "
                "  status = $1, reviewed_by = $2, reviewed_at = " + tsFunc + ", "
                "  review_comment = $3, approved_client_id = $4, "
                "  updated_at = " + tsFunc + " "
                "WHERE id = $5";
            params = { status, reviewedBy, reviewComment, approvedClientId, id };
        } else {
            query =
                "UPDATE api_client_requests SET "
                "  status = $1, reviewed_by = $2, reviewed_at = " + tsFunc + ", "
                "  review_comment = $3, "
                "  updated_at = " + tsFunc + " "
                "WHERE id = $4";
            params = { status, reviewedBy, reviewComment, id };
        }

        int rowsAffected = executor_->executeCommand(query, params);
        if (rowsAffected > 0) {
            spdlog::info("[ApiClientRequestRepository] Updated request {} status to {}", id, status);
            return true;
        }
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[ApiClientRequestRepository] updateStatus failed: {}", e.what());
        return false;
    }
}

// --- Private Helpers ---

domain::models::ApiClientRequest ApiClientRequestRepository::jsonToModel(const Json::Value& row) {
    domain::models::ApiClientRequest req;

    req.id = row.get("id", "").asString();
    req.requesterName = row.get("requester_name", "").asString();
    req.requesterOrg = row.get("requester_org", "").asString();

    std::string phone = row.get("requester_contact_phone", "").asString();
    if (!phone.empty()) req.requesterContactPhone = phone;

    req.requesterContactEmail = row.get("requester_contact_email", "").asString();
    req.requestReason = row.get("request_reason", "").asString();
    req.clientName = row.get("client_name", "").asString();

    std::string desc = row.get("description", "").asString();
    if (!desc.empty()) req.description = desc;

    req.deviceType = row.get("device_type", "SERVER").asString();
    req.permissions = parseJsonArray(row.get("permissions", "[]"));
    req.allowedIps = parseJsonArray(row.get("allowed_ips", "[]"));

    req.status = row.get("status", "PENDING").asString();

    std::string reviewedBy = row.get("reviewed_by", "").asString();
    if (!reviewedBy.empty()) req.reviewedBy = reviewedBy;

    std::string reviewedAt = row.get("reviewed_at", "").asString();
    if (!reviewedAt.empty()) req.reviewedAt = reviewedAt;

    std::string reviewComment = row.get("review_comment", "").asString();
    if (!reviewComment.empty()) req.reviewComment = reviewComment;

    std::string approvedClientId = row.get("approved_client_id", "").asString();
    if (!approvedClientId.empty()) req.approvedClientId = approvedClientId;

    req.createdAt = row.get("created_at", "").asString();
    req.updatedAt = row.get("updated_at", "").asString();

    return req;
}

std::vector<std::string> ApiClientRequestRepository::parseJsonArray(const Json::Value& val) {
    std::vector<std::string> result;

    if (val.isArray()) {
        for (const auto& item : val) {
            result.push_back(item.asString());
        }
        return result;
    }

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

} // namespace repositories

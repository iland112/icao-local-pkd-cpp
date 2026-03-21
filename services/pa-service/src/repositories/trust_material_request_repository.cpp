#include "trust_material_request_repository.h"
#include "i_query_executor.h"
#include "query_helpers.h"
#include "../auth/personal_info_crypto.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

TrustMaterialRequestRepository::TrustMaterialRequestRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("TrustMaterialRequestRepository: queryExecutor cannot be nullptr");
    }
    spdlog::debug("[TrustMaterialRequestRepo] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

std::string TrustMaterialRequestRepository::insert(const RequestRecord& record) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string encClientIp = auth::pii::encrypt(record.clientIp);

        if (dbType == "postgres") {
            const char* query =
                "INSERT INTO trust_material_request ("
                "country_code, dsc_issuer_dn, "
                "csca_count, link_cert_count, crl_count, "
                "client_ip, user_agent, requested_by, api_client_id, "
                "processing_time_ms, status, error_message"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12"
                ") RETURNING id";

            std::vector<std::string> params = {
                record.countryCode, record.dscIssuerDn,
                std::to_string(record.cscaCount), std::to_string(record.linkCertCount),
                std::to_string(record.crlCount),
                encClientIp, record.userAgent, record.requestedBy, record.apiClientId,
                std::to_string(record.processingTimeMs), record.status, record.errorMessage
            };

            auto result = queryExecutor_->executeScalar(query, params);
            return result.asString();
        } else {
            const char* query =
                "INSERT INTO trust_material_request ("
                "country_code, dsc_issuer_dn, "
                "csca_count, link_cert_count, crl_count, "
                "client_ip, user_agent, requested_by, api_client_id, "
                "processing_time_ms, status, error_message"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12"
                ")";

            std::vector<std::string> params = {
                record.countryCode, record.dscIssuerDn,
                std::to_string(record.cscaCount), std::to_string(record.linkCertCount),
                std::to_string(record.crlCount),
                encClientIp, record.userAgent, record.requestedBy, record.apiClientId,
                std::to_string(record.processingTimeMs), record.status, record.errorMessage
            };

            queryExecutor_->executeCommand(query, params);

            auto idResult = queryExecutor_->executeScalar(
                "SELECT id FROM trust_material_request WHERE country_code = $1 "
                "ORDER BY request_timestamp DESC FETCH FIRST 1 ROWS ONLY",
                {record.countryCode});
            return idResult.asString();
        }
    } catch (const std::exception& e) {
        spdlog::error("[TrustMaterialRequestRepo] Insert failed: {}", e.what());
        return "";
    }
}

bool TrustMaterialRequestRepository::updateResult(const ResultRecord& result) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Encrypt MRZ PII fields
        std::string encNationality = auth::pii::encrypt(result.mrzNationality);
        std::string encDocType = auth::pii::encrypt(result.mrzDocumentType);
        std::string encDocNumber = auth::pii::encrypt(result.mrzDocumentNumber);

        std::string boolTrue = common::db::boolLiteral(dbType, true);
        std::string boolFalse = common::db::boolLiteral(dbType, false);
        std::string currentTs = common::db::currentTimestamp(dbType);

        std::string query =
            "UPDATE trust_material_request SET "
            "verification_status = $1, "
            "verification_message = $2, "
            "trust_chain_valid = $3, "
            "sod_signature_valid = $4, "
            "dg_hash_valid = $5, "
            "crl_check_passed = $6, "
            "client_processing_time_ms = $7, "
            "mrz_nationality = $8, "
            "mrz_document_type = $9, "
            "mrz_document_number = $10, "
            "result_reported_at = " + currentTs + ", "
            "status = $1 "
            "WHERE id = $11";

        std::vector<std::string> params = {
            result.verificationStatus,
            result.verificationMessage,
            result.trustChainValid ? boolTrue : boolFalse,
            result.sodSignatureValid ? boolTrue : boolFalse,
            result.dgHashValid ? boolTrue : boolFalse,
            result.crlCheckPassed ? boolTrue : boolFalse,
            std::to_string(result.clientProcessingTimeMs),
            encNationality, encDocType, encDocNumber,
            result.requestId
        };

        queryExecutor_->executeCommand(query, params);
        spdlog::info("[TrustMaterialRequestRepo] Result updated: id={}, status={}",
            result.requestId.substr(0, 8), result.verificationStatus);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[TrustMaterialRequestRepo] updateResult failed: {}", e.what());
        return false;
    }
}

Json::Value TrustMaterialRequestRepository::findAll(int limit, int offset, const std::string& countryFilter) {
    Json::Value result;
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Build WHERE clause
        std::string where;
        std::vector<std::string> params;
        int paramIdx = 1;
        if (!countryFilter.empty()) {
            where = " WHERE country_code = $" + std::to_string(paramIdx++);
            params.push_back(countryFilter);
        }

        // Count total
        auto countResult = queryExecutor_->executeScalar(
            "SELECT COUNT(*) FROM trust_material_request" + where, params);
        int total = common::db::scalarToInt(countResult);

        // Fetch page
        std::string pagination = common::db::paginationClause(dbType, limit, offset);
        std::string query =
            "SELECT id, country_code, dsc_issuer_dn, csca_count, link_cert_count, crl_count, "
            "client_ip, requested_by, request_timestamp, processing_time_ms, "
            "status, verification_status, verification_message, "
            "trust_chain_valid, sod_signature_valid, dg_hash_valid, crl_check_passed, "
            "result_reported_at, client_processing_time_ms, "
            "mrz_nationality, mrz_document_type "
            "FROM trust_material_request" + where +
            " ORDER BY request_timestamp DESC " + pagination;

        auto rows = queryExecutor_->executeQuery(query, params);

        Json::Value data(Json::arrayValue);
        for (const auto& row : rows) {
            Json::Value item;
            item["id"] = row["id"].asString();
            item["countryCode"] = row["country_code"].asString();
            item["cscaCount"] = common::db::scalarToInt(row["csca_count"]);
            item["linkCertCount"] = common::db::scalarToInt(row["link_cert_count"]);
            item["crlCount"] = common::db::scalarToInt(row["crl_count"]);
            item["requestedBy"] = row["requested_by"].asString();
            item["requestTimestamp"] = row["request_timestamp"].asString();
            item["processingTimeMs"] = common::db::scalarToInt(row["processing_time_ms"]);
            item["status"] = row["status"].asString();
            item["verificationStatus"] = row["verification_status"].asString();
            item["verificationMessage"] = row["verification_message"].asString();

            // Decrypt & mask PII
            std::string nationality = auth::pii::decrypt(row["mrz_nationality"].asString());
            std::string docType = auth::pii::decrypt(row["mrz_document_type"].asString());
            item["mrzNationality"] = nationality;
            item["mrzDocumentType"] = docType;

            // Mask client IP
            std::string clientIp = auth::pii::decrypt(row["client_ip"].asString());
            item["clientIp"] = auth::pii::mask(clientIp, "ip");

            data.append(item);
        }

        result["success"] = true;
        result["data"] = data;
        result["total"] = total;
        result["page"] = offset / std::max(limit, 1);
        result["size"] = limit;

    } catch (const std::exception& e) {
        spdlog::error("[TrustMaterialRequestRepo] findAll failed: {}", e.what());
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

Json::Value TrustMaterialRequestRepository::getStatistics() {
    Json::Value stats;
    try {
        auto totalResult = queryExecutor_->executeScalar(
            "SELECT COUNT(*) FROM trust_material_request", {});
        stats["totalRequests"] = common::db::scalarToInt(totalResult);

        // By status (REQUESTED, VALID, INVALID, ERROR, NOT_FOUND)
        auto statusResult = queryExecutor_->executeQuery(
            "SELECT status, COUNT(*) as cnt FROM trust_material_request "
            "GROUP BY status ORDER BY cnt DESC", {});
        Json::Value byStatus(Json::objectValue);
        for (const auto& row : statusResult) {
            byStatus[row["status"].asString()] = common::db::scalarToInt(row["cnt"]);
        }
        stats["byStatus"] = byStatus;

        // By country
        auto countryResult = queryExecutor_->executeQuery(
            "SELECT country_code, COUNT(*) as cnt FROM trust_material_request "
            "GROUP BY country_code ORDER BY cnt DESC", {});
        Json::Value byCountry(Json::arrayValue);
        for (const auto& row : countryResult) {
            Json::Value entry;
            entry["countryCode"] = row["country_code"].asString();
            entry["count"] = common::db::scalarToInt(row["cnt"]);
            byCountry.append(entry);
        }
        stats["byCountry"] = byCountry;

        // Verification result breakdown
        auto verResult = queryExecutor_->executeScalar(
            "SELECT COUNT(*) FROM trust_material_request WHERE verification_status IS NOT NULL", {});
        stats["resultReportedCount"] = common::db::scalarToInt(verResult);

        auto validResult = queryExecutor_->executeScalar(
            "SELECT COUNT(*) FROM trust_material_request WHERE verification_status = 'VALID'", {});
        stats["validCount"] = common::db::scalarToInt(validResult);

        auto invalidResult = queryExecutor_->executeScalar(
            "SELECT COUNT(*) FROM trust_material_request WHERE verification_status = 'INVALID'", {});
        stats["invalidCount"] = common::db::scalarToInt(invalidResult);

    } catch (const std::exception& e) {
        spdlog::error("[TrustMaterialRequestRepo] getStatistics failed: {}", e.what());
    }
    return stats;
}

} // namespace repositories

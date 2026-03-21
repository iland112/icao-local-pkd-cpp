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

        // Encrypt PII fields
        std::string encNationality = auth::pii::encrypt(record.mrzNationality);
        std::string encDocType = auth::pii::encrypt(record.mrzDocumentType);
        std::string encDocNumber = auth::pii::encrypt(record.mrzDocumentNumber);
        std::string encClientIp = auth::pii::encrypt(record.clientIp);

        if (dbType == "postgres") {
            const char* query =
                "INSERT INTO trust_material_request ("
                "country_code, dsc_issuer_dn, "
                "mrz_nationality, mrz_document_type, mrz_document_number, "
                "csca_count, link_cert_count, crl_count, "
                "client_ip, user_agent, requested_by, api_client_id, "
                "processing_time_ms, status, error_message"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15"
                ") RETURNING id";

            std::vector<std::string> params = {
                record.countryCode,
                record.dscIssuerDn,
                encNationality,
                encDocType,
                encDocNumber,
                std::to_string(record.cscaCount),
                std::to_string(record.linkCertCount),
                std::to_string(record.crlCount),
                encClientIp,
                record.userAgent,
                record.requestedBy,
                record.apiClientId,
                std::to_string(record.processingTimeMs),
                record.status,
                record.errorMessage
            };

            auto result = queryExecutor_->executeScalar(query, params);
            std::string id = result.asString();
            spdlog::debug("[TrustMaterialRequestRepo] Inserted: id={}, country={}", id.substr(0, 8), record.countryCode);
            return id;
        } else {
            // Oracle
            const char* query =
                "INSERT INTO trust_material_request ("
                "country_code, dsc_issuer_dn, "
                "mrz_nationality, mrz_document_type, mrz_document_number, "
                "csca_count, link_cert_count, crl_count, "
                "client_ip, user_agent, requested_by, api_client_id, "
                "processing_time_ms, status, error_message"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15"
                ")";

            std::vector<std::string> params = {
                record.countryCode,
                record.dscIssuerDn,
                encNationality,
                encDocType,
                encDocNumber,
                std::to_string(record.cscaCount),
                std::to_string(record.linkCertCount),
                std::to_string(record.crlCount),
                encClientIp,
                record.userAgent,
                record.requestedBy,
                record.apiClientId,
                std::to_string(record.processingTimeMs),
                record.status,
                record.errorMessage
            };

            queryExecutor_->executeCommand(query, params);

            // Re-query for generated ID
            auto idResult = queryExecutor_->executeScalar(
                "SELECT id FROM trust_material_request WHERE country_code = $1 AND status = $2 "
                "ORDER BY request_timestamp DESC FETCH FIRST 1 ROWS ONLY",
                {record.countryCode, record.status});
            std::string id = idResult.asString();
            spdlog::debug("[TrustMaterialRequestRepo] Inserted (Oracle): id={}, country={}", id.substr(0, 8), record.countryCode);
            return id;
        }
    } catch (const std::exception& e) {
        spdlog::error("[TrustMaterialRequestRepo] Insert failed: {}", e.what());
        return "";
    }
}

Json::Value TrustMaterialRequestRepository::getStatistics() {
    Json::Value stats;
    try {
        // Total requests
        auto totalResult = queryExecutor_->executeScalar(
            "SELECT COUNT(*) FROM trust_material_request", {});
        stats["totalRequests"] = common::db::scalarToInt(totalResult);

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

    } catch (const std::exception& e) {
        spdlog::error("[TrustMaterialRequestRepo] getStatistics failed: {}", e.what());
    }
    return stats;
}

} // namespace repositories

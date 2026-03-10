/**
 * @file pending_dsc_repository.cpp
 * @brief Repository for pending DSC registration approval workflow
 */

#include "pending_dsc_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

PendingDscRepository::PendingDscRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("PendingDscRepository: queryExecutor cannot be nullptr");
    }
}

Json::Value PendingDscRepository::findAll(int limit, int offset,
                                           const std::string& status,
                                           const std::string& countryCode)
{
    std::string dbType = queryExecutor_->getDatabaseType();

    std::string query = "SELECT id, fingerprint_sha256, country_code, ";

    if (dbType == "oracle") {
        query += "TO_CHAR(subject_dn) AS subject_dn, TO_CHAR(issuer_dn) AS issuer_dn, ";
    } else {
        query += "subject_dn, issuer_dn, ";
    }

    query += "serial_number, not_before, not_after, "
             "signature_algorithm, public_key_algorithm, public_key_size, "
             "is_self_signed, validation_status, "
             "pa_verification_id, verification_status, "
             "status, reviewed_by, reviewed_at, ";

    if (dbType == "oracle") {
        query += "TO_CHAR(review_comment) AS review_comment, ";
    } else {
        query += "review_comment, ";
    }

    query += "created_at FROM pending_dsc_registration WHERE 1=1";

    std::vector<std::string> params;
    int paramIdx = 1;

    if (!status.empty()) {
        query += " AND status = $" + std::to_string(paramIdx++);
        params.push_back(status);
    }

    if (!countryCode.empty()) {
        query += " AND country_code = $" + std::to_string(paramIdx++);
        params.push_back(countryCode);
    }

    query += " ORDER BY created_at DESC";
    query += " " + common::db::paginationClause(dbType, limit, offset);

    return queryExecutor_->executeQuery(query, params);
}

int PendingDscRepository::countAll(const std::string& status,
                                    const std::string& countryCode)
{
    std::string query = "SELECT COUNT(*) AS cnt FROM pending_dsc_registration WHERE 1=1";
    std::vector<std::string> params;
    int paramIdx = 1;

    if (!status.empty()) {
        query += " AND status = $" + std::to_string(paramIdx++);
        params.push_back(status);
    }

    if (!countryCode.empty()) {
        query += " AND country_code = $" + std::to_string(paramIdx++);
        params.push_back(countryCode);
    }

    Json::Value result = queryExecutor_->executeQuery(query, params);
    if (!result.empty()) {
        return common::db::scalarToInt(result[0]["cnt"]);
    }
    return 0;
}

Json::Value PendingDscRepository::findById(const std::string& id) {
    std::string dbType = queryExecutor_->getDatabaseType();

    std::string query = "SELECT id, fingerprint_sha256, country_code, ";

    if (dbType == "oracle") {
        query += "TO_CHAR(subject_dn) AS subject_dn, TO_CHAR(issuer_dn) AS issuer_dn, ";
        query += "RAWTOHEX(DBMS_LOB.SUBSTR(certificate_data, DBMS_LOB.GETLENGTH(certificate_data), 1)) AS certificate_data, ";
    } else {
        query += "subject_dn, issuer_dn, certificate_data, ";
    }

    query += "serial_number, not_before, not_after, "
             "signature_algorithm, public_key_algorithm, public_key_size, "
             "is_self_signed, validation_status, "
             "pa_verification_id, verification_status, "
             "status, reviewed_by, reviewed_at, ";

    if (dbType == "oracle") {
        query += "TO_CHAR(review_comment) AS review_comment, ";
    } else {
        query += "review_comment, ";
    }

    query += "created_at FROM pending_dsc_registration WHERE id = $1";

    std::vector<std::string> params = {id};
    Json::Value result = queryExecutor_->executeQuery(query, params);

    if (result.empty()) {
        return Json::Value::null;
    }
    return result[0];
}

bool PendingDscRepository::updateStatus(const std::string& id,
                                         const std::string& status,
                                         const std::string& reviewedBy,
                                         const std::string& reviewComment)
{
    std::string dbType = queryExecutor_->getDatabaseType();
    std::string timestamp = (dbType == "oracle") ? "SYSTIMESTAMP" : "CURRENT_TIMESTAMP";

    std::string query = "UPDATE pending_dsc_registration SET "
                        "status = $1, reviewed_by = $2, review_comment = $3, "
                        "reviewed_at = " + timestamp +
                        " WHERE id = $4";

    std::vector<std::string> params = {status, reviewedBy, reviewComment, id};

    try {
        queryExecutor_->executeCommand(query, params);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[PendingDscRepo] Failed to update status: {}", e.what());
        return false;
    }
}

bool PendingDscRepository::deleteById(const std::string& id) {
    try {
        queryExecutor_->executeCommand(
            "DELETE FROM pending_dsc_registration WHERE id = $1", {id});
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[PendingDscRepo] Failed to delete: {}", e.what());
        return false;
    }
}

Json::Value PendingDscRepository::getStatistics() {
    std::string query =
        "SELECT status, COUNT(*) AS cnt "
        "FROM pending_dsc_registration "
        "GROUP BY status";

    Json::Value rows = queryExecutor_->executeQuery(query, {});

    Json::Value stats;
    stats["pendingCount"] = 0;
    stats["approvedCount"] = 0;
    stats["rejectedCount"] = 0;
    stats["totalCount"] = 0;

    for (const auto& row : rows) {
        std::string s = row["status"].asString();
        int cnt = common::db::scalarToInt(row["cnt"]);
        if (s == "PENDING") stats["pendingCount"] = cnt;
        else if (s == "APPROVED") stats["approvedCount"] = cnt;
        else if (s == "REJECTED") stats["rejectedCount"] = cnt;
        stats["totalCount"] = stats["totalCount"].asInt() + cnt;
    }

    return stats;
}

} // namespace repositories

#include "validation_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

ValidationRepository::ValidationRepository(PGconn* dbConn)
    : dbConn_(dbConn)
{
    if (!dbConn_) {
        throw std::invalid_argument("ValidationRepository: dbConn cannot be nullptr");
    }
    spdlog::debug("[ValidationRepository] Initialized");
}

bool ValidationRepository::save(
    const std::string& fingerprint,
    const std::string& uploadId,
    const std::string& certificateType,
    const std::string& validationStatus,
    bool trustChainValid,
    const std::string& trustChainPath,
    bool signatureValid,
    bool crlChecked,
    bool revoked
)
{
    spdlog::debug("[ValidationRepository] Saving validation for: {}...", fingerprint.substr(0, 16));

    // TODO: Implement validation result save
    spdlog::warn("[ValidationRepository] save - TODO: Implement");

    return false;
}

Json::Value ValidationRepository::findByFingerprint(const std::string& fingerprint)
{
    spdlog::debug("[ValidationRepository] Finding by fingerprint: {}...", fingerprint.substr(0, 16));

    try {
        // Query validation_result with JOIN to certificate on certificate_id
        // Filter by certificate.fingerprint_sha256
        const char* query =
            "SELECT vr.id, vr.certificate_id, vr.upload_id, vr.certificate_type, "
            "       vr.country_code, vr.subject_dn, vr.issuer_dn, vr.serial_number, "
            "       vr.validation_status, vr.trust_chain_valid, vr.trust_chain_message, "
            "       vr.trust_chain_path, vr.csca_found, vr.csca_subject_dn, "
            "       vr.signature_valid, vr.signature_algorithm, "
            "       vr.validity_period_valid, vr.is_expired, vr.is_not_yet_valid, "
            "       vr.not_before, vr.not_after, "
            "       vr.revocation_status, vr.crl_checked, "
            "       vr.validation_timestamp, c.fingerprint_sha256 "
            "FROM validation_result vr "
            "LEFT JOIN certificate c ON vr.certificate_id = c.id "
            "WHERE c.fingerprint_sha256 = $1 "
            "LIMIT 1";

        std::vector<std::string> params = {fingerprint};
        PGresult* res = executeParamQuery(query, params);

        // Check if result found
        if (PQntuples(res) == 0) {
            spdlog::debug("[ValidationRepository] No validation result found for fingerprint: {}...",
                fingerprint.substr(0, 16));
            PQclear(res);
            return Json::nullValue;
        }

        // Build JSON response
        Json::Value result;
        result["id"] = PQgetisnull(res, 0, 0) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 0)));
        result["certificateId"] = PQgetisnull(res, 0, 1) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 1)));
        result["uploadId"] = PQgetisnull(res, 0, 2) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 2)));
        result["certificateType"] = PQgetisnull(res, 0, 3) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 3)));
        result["countryCode"] = PQgetisnull(res, 0, 4) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 4)));
        result["subjectDn"] = PQgetisnull(res, 0, 5) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 5)));
        result["issuerDn"] = PQgetisnull(res, 0, 6) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 6)));
        result["serialNumber"] = PQgetisnull(res, 0, 7) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 7)));
        result["validationStatus"] = PQgetisnull(res, 0, 8) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 8)));

        // Boolean fields
        std::string tcvStr = PQgetisnull(res, 0, 9) ? "f" : std::string(PQgetvalue(res, 0, 9));
        result["trustChainValid"] = (tcvStr == "t" || tcvStr == "true");

        result["trustChainMessage"] = PQgetisnull(res, 0, 10) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 10)));

        // Parse trust_chain_path JSONB (stored as array ["DSC → CSCA"])
        std::string tcpRaw = PQgetisnull(res, 0, 11) ? "[]" : std::string(PQgetvalue(res, 0, 11));
        try {
            Json::Reader reader;
            Json::Value pathArray;
            if (reader.parse(tcpRaw, pathArray) && pathArray.isArray() && pathArray.size() > 0) {
                result["trustChainPath"] = pathArray[0].asString();
            } else {
                result["trustChainPath"] = "";
            }
        } catch (...) {
            result["trustChainPath"] = "";
        }

        std::string cfStr = PQgetisnull(res, 0, 12) ? "f" : std::string(PQgetvalue(res, 0, 12));
        result["cscaFound"] = (cfStr == "t" || cfStr == "true");
        result["cscaSubjectDn"] = PQgetisnull(res, 0, 13) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 13)));

        std::string svStr = PQgetisnull(res, 0, 14) ? "f" : std::string(PQgetvalue(res, 0, 14));
        result["signatureVerified"] = (svStr == "t" || svStr == "true");

        result["signatureAlgorithm"] = PQgetisnull(res, 0, 15) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 15)));

        std::string vpStr = PQgetisnull(res, 0, 16) ? "f" : std::string(PQgetvalue(res, 0, 16));
        result["validityCheckPassed"] = (vpStr == "t" || vpStr == "true");

        std::string expStr = PQgetisnull(res, 0, 17) ? "f" : std::string(PQgetvalue(res, 0, 17));
        result["isExpired"] = (expStr == "t" || expStr == "true");

        std::string nysStr = PQgetisnull(res, 0, 18) ? "f" : std::string(PQgetvalue(res, 0, 18));
        result["isNotYetValid"] = (nysStr == "t" || nysStr == "true");

        result["notBefore"] = PQgetisnull(res, 0, 19) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 19)));
        result["notAfter"] = PQgetisnull(res, 0, 20) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 20)));
        result["crlCheckStatus"] = PQgetisnull(res, 0, 21) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 21)));

        std::string ccStr = PQgetisnull(res, 0, 22) ? "f" : std::string(PQgetvalue(res, 0, 22));
        result["crlChecked"] = (ccStr == "t" || ccStr == "true");

        result["validatedAt"] = PQgetisnull(res, 0, 23) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 23)));
        result["fingerprint"] = PQgetisnull(res, 0, 24) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, 0, 24)));

        PQclear(res);

        spdlog::debug("[ValidationRepository] Found validation result for fingerprint: {}...",
            fingerprint.substr(0, 16));

        return result;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] findByFingerprint failed: {}", e.what());
        return Json::nullValue;
    }
}

Json::Value ValidationRepository::findByUploadId(
    const std::string& uploadId,
    int limit,
    int offset,
    const std::string& statusFilter,
    const std::string& certTypeFilter
)
{
    spdlog::debug("[ValidationRepository] Finding by upload ID: {} (limit: {}, offset: {}, status: {}, certType: {})",
        uploadId, limit, offset, statusFilter, certTypeFilter);

    Json::Value response;

    try {
        // Build dynamic WHERE clause
        std::string whereClause = "WHERE vr.upload_id = $1";
        std::vector<std::string> paramValues;
        paramValues.push_back(uploadId);
        int paramIdx = 2;

        if (!statusFilter.empty()) {
            whereClause += " AND vr.validation_status = $" + std::to_string(paramIdx);
            paramValues.push_back(statusFilter);
            paramIdx++;
        }
        if (!certTypeFilter.empty()) {
            whereClause += " AND vr.certificate_type = $" + std::to_string(paramIdx);
            paramValues.push_back(certTypeFilter);
            paramIdx++;
        }

        // Get total count
        std::string countQuery = "SELECT COUNT(*) FROM validation_result vr " + whereClause;
        PGresult* countRes = executeParamQuery(countQuery, paramValues);

        int total = 0;
        if (PQresultStatus(countRes) == PGRES_TUPLES_OK && PQntuples(countRes) > 0) {
            total = std::atoi(PQgetvalue(countRes, 0, 0));
        }
        PQclear(countRes);

        // Fetch validation results with JOIN to certificate for fingerprint
        std::string dataQuery =
            "SELECT vr.id, vr.certificate_id, vr.upload_id, vr.certificate_type, "
            "       vr.country_code, vr.subject_dn, vr.issuer_dn, vr.serial_number, "
            "       vr.validation_status, vr.trust_chain_valid, vr.trust_chain_message, "
            "       vr.trust_chain_path, vr.csca_found, vr.csca_subject_dn, "
            "       vr.signature_valid, vr.signature_algorithm, "
            "       vr.validity_period_valid, vr.is_expired, vr.is_not_yet_valid, "
            "       vr.not_before, vr.not_after, "
            "       vr.revocation_status, vr.crl_checked, "
            "       vr.validation_timestamp, c.fingerprint_sha256 "
            "FROM validation_result vr "
            "LEFT JOIN certificate c ON vr.certificate_id = c.id "
            + whereClause +
            " ORDER BY vr.validation_status, vr.validation_timestamp DESC "
            " LIMIT $" + std::to_string(paramIdx) +
            " OFFSET $" + std::to_string(paramIdx + 1);

        // Add limit and offset to params
        std::vector<std::string> dataParams = paramValues;
        dataParams.push_back(std::to_string(limit));
        dataParams.push_back(std::to_string(offset));

        PGresult* res = executeParamQuery(dataQuery, dataParams);

        // Build validations array
        Json::Value validations = Json::arrayValue;
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            Json::Value v;
            v["id"] = PQgetisnull(res, i, 0) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 0)));
            v["certificateId"] = PQgetisnull(res, i, 1) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 1)));
            v["uploadId"] = PQgetisnull(res, i, 2) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 2)));
            v["certificateType"] = PQgetisnull(res, i, 3) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 3)));
            v["countryCode"] = PQgetisnull(res, i, 4) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 4)));
            v["subjectDn"] = PQgetisnull(res, i, 5) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 5)));
            v["issuerDn"] = PQgetisnull(res, i, 6) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 6)));
            v["serialNumber"] = PQgetisnull(res, i, 7) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 7)));
            v["validationStatus"] = PQgetisnull(res, i, 8) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 8)));

            // Boolean fields
            std::string tcvStr = PQgetisnull(res, i, 9) ? "f" : std::string(PQgetvalue(res, i, 9));
            v["trustChainValid"] = (tcvStr == "t" || tcvStr == "true");

            v["trustChainMessage"] = PQgetisnull(res, i, 10) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 10)));

            // Parse trust_chain_path JSONB
            std::string tcpRaw = PQgetisnull(res, i, 11) ? "[]" : std::string(PQgetvalue(res, i, 11));
            try {
                Json::Reader reader;
                Json::Value pathArray;
                if (reader.parse(tcpRaw, pathArray) && pathArray.isArray() && pathArray.size() > 0) {
                    v["trustChainPath"] = pathArray[0].asString();
                } else {
                    v["trustChainPath"] = "";
                }
            } catch (...) {
                v["trustChainPath"] = "";
            }

            std::string cfStr = PQgetisnull(res, i, 12) ? "f" : std::string(PQgetvalue(res, i, 12));
            v["cscaFound"] = (cfStr == "t" || cfStr == "true");
            v["cscaSubjectDn"] = PQgetisnull(res, i, 13) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 13)));

            std::string svStr = PQgetisnull(res, i, 14) ? "f" : std::string(PQgetvalue(res, i, 14));
            v["signatureVerified"] = (svStr == "t" || svStr == "true");

            v["signatureAlgorithm"] = PQgetisnull(res, i, 15) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 15)));

            std::string vpStr = PQgetisnull(res, i, 16) ? "f" : std::string(PQgetvalue(res, i, 16));
            v["validityCheckPassed"] = (vpStr == "t" || vpStr == "true");

            std::string expStr = PQgetisnull(res, i, 17) ? "f" : std::string(PQgetvalue(res, i, 17));
            v["isExpired"] = (expStr == "t" || expStr == "true");

            std::string nysStr = PQgetisnull(res, i, 18) ? "f" : std::string(PQgetvalue(res, i, 18));
            v["isNotYetValid"] = (nysStr == "t" || nysStr == "true");

            v["notBefore"] = PQgetisnull(res, i, 19) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 19)));
            v["notAfter"] = PQgetisnull(res, i, 20) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 20)));
            v["crlCheckStatus"] = PQgetisnull(res, i, 21) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 21)));

            std::string ccStr = PQgetisnull(res, i, 22) ? "f" : std::string(PQgetvalue(res, i, 22));
            v["crlChecked"] = (ccStr == "t" || ccStr == "true");

            v["validatedAt"] = PQgetisnull(res, i, 23) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 23)));
            v["fingerprint"] = PQgetisnull(res, i, 24) ? Json::nullValue : Json::Value(std::string(PQgetvalue(res, i, 24)));

            validations.append(v);
        }

        PQclear(res);

        // Build response with pagination metadata
        response["success"] = true;
        response["count"] = rows;
        response["total"] = total;
        response["limit"] = limit;
        response["offset"] = offset;
        response["validations"] = validations;

        spdlog::debug("[ValidationRepository] Found {} validations (total: {})", rows, total);

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] findByUploadId failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
        response["count"] = 0;
        response["total"] = 0;
        response["validations"] = Json::arrayValue;
        return response;
    }
}

int ValidationRepository::countByStatus(const std::string& status)
{
    spdlog::debug("[ValidationRepository] Counting by status: {}", status);

    try {
        const char* query = "SELECT COUNT(*) FROM validation_result WHERE validation_status = $1";
        std::vector<std::string> params = {status};

        PGresult* res = executeParamQuery(query, params);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Count by status failed: {}", e.what());
        return 0;
    }
}

PGresult* ValidationRepository::executeParamQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
    }

    PGresult* res = PQexecParams(
        dbConn_,
        query.c_str(),
        params.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)) {
        std::string error = res ? PQerrorMessage(dbConn_) : "null result";
        if (res) PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

PGresult* ValidationRepository::executeQuery(const std::string& query)
{
    PGresult* res = PQexec(dbConn_, query.c_str());

    if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)) {
        std::string error = res ? PQerrorMessage(dbConn_) : "null result";
        if (res) PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

Json::Value ValidationRepository::pgResultToJson(PGresult* res)
{
    Json::Value array = Json::arrayValue;
    int rows = PQntuples(res);
    int cols = PQnfields(res);

    for (int i = 0; i < rows; ++i) {
        Json::Value row;
        for (int j = 0; j < cols; ++j) {
            const char* fieldName = PQfname(res, j);
            if (PQgetisnull(res, i, j)) {
                row[fieldName] = Json::nullValue;
            } else {
                row[fieldName] = PQgetvalue(res, i, j);
            }
        }
        array.append(row);
    }

    return array;
}

} // namespace repositories

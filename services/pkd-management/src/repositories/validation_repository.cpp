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

bool ValidationRepository::save(const domain::models::ValidationResult& result)
{
    spdlog::debug("[ValidationRepository] Saving validation for cert: {}...",
                  result.certificateId.substr(0, 8));

    try {
        // Parameterized query matching actual validation_result table schema (22 columns)
        const char* query =
            "INSERT INTO validation_result ("
            "certificate_id, upload_id, certificate_type, country_code, "
            "subject_dn, issuer_dn, serial_number, "
            "validation_status, trust_chain_valid, trust_chain_message, "
            "csca_found, csca_subject_dn, csca_serial_number, csca_country, "
            "signature_valid, signature_algorithm, "
            "validity_period_valid, not_before, not_after, "
            "revocation_status, crl_checked, "
            "trust_chain_path"
            ") VALUES ("
            "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, "
            "$11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22"
            ")";

        // Prepare boolean strings
        const std::string trustChainValidStr = result.trustChainValid ? "true" : "false";
        const std::string cscaFoundStr = result.cscaFound ? "true" : "false";
        const std::string signatureValidStr = result.signatureVerified ? "true" : "false";
        const std::string validityPeriodValidStr = result.validityCheckPassed ? "true" : "false";
        const std::string crlCheckedStr = (result.crlCheckStatus != "NOT_CHECKED") ? "true" : "false";

        // Map crlCheckStatus to revocation_status (schema uses UNKNOWN/NOT_REVOKED/REVOKED)
        std::string revocationStatus = "UNKNOWN";
        if (result.crlCheckStatus == "REVOKED") {
            revocationStatus = "REVOKED";
        } else if (result.crlCheckStatus == "NOT_REVOKED" || result.crlCheckStatus == "VALID") {
            revocationStatus = "NOT_REVOKED";
        }

        // Prepare trust_chain_path as JSON array (schema expects jsonb)
        // trustChainPath is human-readable like "DSC → CN=CSCA → CN=Link"
        // Wrap as JSON array for JSONB column
        std::string trustChainPathJson;
        if (result.trustChainPath.empty()) {
            trustChainPathJson = "[]";
        } else {
            Json::Value pathArray(Json::arrayValue);
            pathArray.append(result.trustChainPath);
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            trustChainPathJson = Json::writeString(builder, pathArray);
        }

        // Prepare parameter values
        const char* paramValues[22];
        paramValues[0] = result.certificateId.c_str();
        paramValues[1] = result.uploadId.c_str();
        paramValues[2] = result.certificateType.c_str();
        paramValues[3] = result.countryCode.empty() ? nullptr : result.countryCode.c_str();
        paramValues[4] = result.subjectDn.c_str();
        paramValues[5] = result.issuerDn.c_str();
        paramValues[6] = result.serialNumber.empty() ? nullptr : result.serialNumber.c_str();
        paramValues[7] = result.validationStatus.c_str();
        paramValues[8] = trustChainValidStr.c_str();
        paramValues[9] = result.trustChainMessage.empty() ? nullptr : result.trustChainMessage.c_str();
        paramValues[10] = cscaFoundStr.c_str();
        paramValues[11] = result.cscaSubjectDn.empty() ? nullptr : result.cscaSubjectDn.c_str();
        paramValues[12] = nullptr;  // csca_serial_number - not tracked in ValidationResult
        paramValues[13] = nullptr;  // csca_country - not tracked in ValidationResult
        paramValues[14] = signatureValidStr.c_str();
        paramValues[15] = result.signatureAlgorithm.empty() ? nullptr : result.signatureAlgorithm.c_str();
        paramValues[16] = validityPeriodValidStr.c_str();
        paramValues[17] = result.notBefore.empty() ? nullptr : result.notBefore.c_str();
        paramValues[18] = result.notAfter.empty() ? nullptr : result.notAfter.c_str();
        paramValues[19] = revocationStatus.c_str();
        paramValues[20] = crlCheckedStr.c_str();
        paramValues[21] = trustChainPathJson.c_str();

        // Execute parameterized query
        PGresult* res = PQexecParams(dbConn_, query, 22, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);

        if (!success) {
            spdlog::error("[ValidationRepository] Failed to save validation result: {}",
                         PQerrorMessage(dbConn_));
        } else {
            spdlog::debug("[ValidationRepository] Validation result saved successfully");
        }

        PQclear(res);
        return success;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Exception in save: {}", e.what());
        return false;
    }
}

bool ValidationRepository::updateStatistics(const std::string& uploadId,
                                           const domain::models::ValidationStatistics& stats)
{
    spdlog::debug("[ValidationRepository] Updating statistics for upload: {}...",
                  uploadId.substr(0, 8));

    try {
        // Parameterized UPDATE query for uploaded_file table (10 parameters)
        const char* query =
            "UPDATE uploaded_file SET "
            "validation_valid_count = $1, "
            "validation_invalid_count = $2, "
            "validation_pending_count = $3, "
            "validation_error_count = $4, "
            "trust_chain_valid_count = $5, "
            "trust_chain_invalid_count = $6, "
            "csca_not_found_count = $7, "
            "expired_count = $8, "
            "revoked_count = $9 "
            "WHERE id = $10";

        // Prepare integer strings for parameterized query
        std::string validCountStr = std::to_string(stats.validCount);
        std::string invalidCountStr = std::to_string(stats.invalidCount);
        std::string pendingCountStr = std::to_string(stats.pendingCount);
        std::string errorCountStr = std::to_string(stats.errorCount);
        std::string trustChainValidCountStr = std::to_string(stats.trustChainValidCount);
        std::string trustChainInvalidCountStr = std::to_string(stats.trustChainInvalidCount);
        std::string cscaNotFoundCountStr = std::to_string(stats.cscaNotFoundCount);
        std::string expiredCountStr = std::to_string(stats.expiredCount);
        std::string revokedCountStr = std::to_string(stats.revokedCount);

        const char* paramValues[10] = {
            validCountStr.c_str(),
            invalidCountStr.c_str(),
            pendingCountStr.c_str(),
            errorCountStr.c_str(),
            trustChainValidCountStr.c_str(),
            trustChainInvalidCountStr.c_str(),
            cscaNotFoundCountStr.c_str(),
            expiredCountStr.c_str(),
            revokedCountStr.c_str(),
            uploadId.c_str()
        };

        // Execute parameterized query
        PGresult* res = PQexecParams(dbConn_, query, 10, nullptr, paramValues,
                                     nullptr, nullptr, 0);

        bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);

        if (!success) {
            spdlog::error("[ValidationRepository] Failed to update statistics: {}",
                         PQerrorMessage(dbConn_));
        } else {
            spdlog::debug("[ValidationRepository] Statistics updated successfully "
                         "(valid={}, invalid={}, pending={}, error={})",
                         stats.validCount, stats.invalidCount,
                         stats.pendingCount, stats.errorCount);
        }

        PQclear(res);
        return success;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Exception in updateStatistics: {}", e.what());
        return false;
    }
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

Json::Value ValidationRepository::getStatisticsByUploadId(const std::string& uploadId)
{
    spdlog::debug("[ValidationRepository] Getting statistics for upload ID: {}", uploadId);

    Json::Value stats;

    try {
        // Single query to get all statistics
        const char* query =
            "SELECT "
            "  COUNT(*) as total_count, "
            "  SUM(CASE WHEN validation_status = 'VALID' THEN 1 ELSE 0 END) as valid_count, "
            "  SUM(CASE WHEN validation_status = 'INVALID' THEN 1 ELSE 0 END) as invalid_count, "
            "  SUM(CASE WHEN validation_status = 'PENDING' THEN 1 ELSE 0 END) as pending_count, "
            "  SUM(CASE WHEN validation_status = 'ERROR' THEN 1 ELSE 0 END) as error_count, "
            "  SUM(CASE WHEN trust_chain_valid = TRUE THEN 1 ELSE 0 END) as trust_chain_valid_count, "
            "  SUM(CASE WHEN trust_chain_valid = FALSE THEN 1 ELSE 0 END) as trust_chain_invalid_count "
            "FROM validation_result "
            "WHERE upload_id = $1";

        std::vector<std::string> params = {uploadId};
        PGresult* res = executeParamQuery(query, params);

        if (PQntuples(res) > 0) {
            int totalCount = std::atoi(PQgetvalue(res, 0, 0));
            int validCount = std::atoi(PQgetvalue(res, 0, 1));
            int invalidCount = std::atoi(PQgetvalue(res, 0, 2));
            int pendingCount = std::atoi(PQgetvalue(res, 0, 3));
            int errorCount = std::atoi(PQgetvalue(res, 0, 4));
            int trustChainValidCount = std::atoi(PQgetvalue(res, 0, 5));
            int trustChainInvalidCount = std::atoi(PQgetvalue(res, 0, 6));

            // Calculate trust chain success rate
            double trustChainSuccessRate = 0.0;
            if (totalCount > 0) {
                trustChainSuccessRate = (static_cast<double>(trustChainValidCount) / totalCount) * 100.0;
            }

            stats["totalCount"] = totalCount;
            stats["validCount"] = validCount;
            stats["invalidCount"] = invalidCount;
            stats["pendingCount"] = pendingCount;
            stats["errorCount"] = errorCount;
            stats["trustChainValidCount"] = trustChainValidCount;
            stats["trustChainInvalidCount"] = trustChainInvalidCount;
            stats["trustChainSuccessRate"] = trustChainSuccessRate;

            spdlog::debug("[ValidationRepository] Statistics: total={}, valid={}, invalid={}, pending={}, error={}",
                totalCount, validCount, invalidCount, pendingCount, errorCount);
        }

        PQclear(res);

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Get statistics failed: {}", e.what());
        stats["error"] = e.what();
    }

    return stats;
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

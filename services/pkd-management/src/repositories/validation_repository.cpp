#include "validation_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

ValidationRepository::ValidationRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("ValidationRepository: queryExecutor cannot be nullptr");
    }
    spdlog::debug("[ValidationRepository] Initialized (DB type: {})", queryExecutor_->getDatabaseType());
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

        // Prepare parameter values as vector
        std::vector<std::string> params = {
            result.certificateId,
            result.uploadId,
            result.certificateType,
            result.countryCode.empty() ? "" : result.countryCode,
            result.subjectDn,
            result.issuerDn,
            result.serialNumber.empty() ? "" : result.serialNumber,
            result.validationStatus,
            trustChainValidStr,
            result.trustChainMessage.empty() ? "" : result.trustChainMessage,
            cscaFoundStr,
            result.cscaSubjectDn.empty() ? "" : result.cscaSubjectDn,
            "",  // csca_serial_number - not tracked
            "",  // csca_country - not tracked
            signatureValidStr,
            result.signatureAlgorithm.empty() ? "" : result.signatureAlgorithm,
            validityPeriodValidStr,
            result.notBefore.empty() ? "" : result.notBefore,
            result.notAfter.empty() ? "" : result.notAfter,
            revocationStatus,
            crlCheckedStr,
            trustChainPathJson
        };

        queryExecutor_->executeCommand(query, params);
        spdlog::debug("[ValidationRepository] Validation result saved successfully");
        return true;

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

        std::vector<std::string> params = {
            std::to_string(stats.validCount),
            std::to_string(stats.invalidCount),
            std::to_string(stats.pendingCount),
            std::to_string(stats.errorCount),
            std::to_string(stats.trustChainValidCount),
            std::to_string(stats.trustChainInvalidCount),
            std::to_string(stats.cscaNotFoundCount),
            std::to_string(stats.expiredCount),
            std::to_string(stats.revokedCount),
            uploadId
        };

        queryExecutor_->executeCommand(query, params);
        spdlog::debug("[ValidationRepository] Statistics updated successfully "
                     "(valid={}, invalid={}, pending={}, error={})",
                     stats.validCount, stats.invalidCount,
                     stats.pendingCount, stats.errorCount);
        return true;

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
            "       vr.validity_period_valid, vr.not_before, vr.not_after, "
            "       vr.revocation_status, vr.crl_checked, "
            "       vr.validation_timestamp, c.fingerprint_sha256 "
            "FROM validation_result vr "
            "LEFT JOIN certificate c ON vr.certificate_id = c.id "
            "WHERE c.fingerprint_sha256 = $1 "
            "LIMIT 1";

        std::vector<std::string> params = {fingerprint};
        Json::Value queryResult = queryExecutor_->executeQuery(query, params);

        // Check if result found
        if (queryResult.empty()) {
            spdlog::debug("[ValidationRepository] No validation result found for fingerprint: {}...",
                fingerprint.substr(0, 16));
            return Json::nullValue;
        }

        // Extract first row
        Json::Value row = queryResult[0];

        // Build JSON response with field name mapping
        Json::Value result;
        result["id"] = row.get("id", Json::nullValue);
        result["certificateId"] = row.get("certificate_id", Json::nullValue);
        result["uploadId"] = row.get("upload_id", Json::nullValue);
        result["certificateType"] = row.get("certificate_type", Json::nullValue);
        result["countryCode"] = row.get("country_code", Json::nullValue);
        result["subjectDn"] = row.get("subject_dn", Json::nullValue);
        result["issuerDn"] = row.get("issuer_dn", Json::nullValue);
        result["serialNumber"] = row.get("serial_number", Json::nullValue);
        result["validationStatus"] = row.get("validation_status", Json::nullValue);

        // Boolean fields - handle both boolean and string types from Query Executor
        Json::Value tcvVal = row.get("trust_chain_valid", false);
        if (tcvVal.isBool()) {
            result["trustChainValid"] = tcvVal.asBool();
        } else if (tcvVal.isString()) {
            std::string tcvStr = tcvVal.asString();
            result["trustChainValid"] = (tcvStr == "t" || tcvStr == "true");
        } else {
            result["trustChainValid"] = false;
        }

        result["trustChainMessage"] = row.get("trust_chain_message", Json::nullValue);

        // Parse trust_chain_path JSONB (stored as array ["DSC → CSCA"])
        Json::Value tcpVal = row.get("trust_chain_path", Json::nullValue);
        if (tcpVal.isArray() && tcpVal.size() > 0) {
            result["trustChainPath"] = tcpVal[0].asString();
        } else if (tcpVal.isString()) {
            // If returned as string, try parsing
            std::string tcpRaw = tcpVal.asString();
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
        } else {
            result["trustChainPath"] = "";
        }

        // Boolean field: csca_found
        Json::Value cfVal = row.get("csca_found", false);
        if (cfVal.isBool()) {
            result["cscaFound"] = cfVal.asBool();
        } else if (cfVal.isString()) {
            std::string cfStr = cfVal.asString();
            result["cscaFound"] = (cfStr == "t" || cfStr == "true");
        } else {
            result["cscaFound"] = false;
        }

        result["cscaSubjectDn"] = row.get("csca_subject_dn", Json::nullValue);

        // Boolean field: signature_valid
        Json::Value svVal = row.get("signature_valid", false);
        if (svVal.isBool()) {
            result["signatureVerified"] = svVal.asBool();
        } else if (svVal.isString()) {
            std::string svStr = svVal.asString();
            result["signatureVerified"] = (svStr == "t" || svStr == "true");
        } else {
            result["signatureVerified"] = false;
        }

        result["signatureAlgorithm"] = row.get("signature_algorithm", Json::nullValue);

        // Boolean field: validity_period_valid
        Json::Value vpVal = row.get("validity_period_valid", false);
        if (vpVal.isBool()) {
            result["validityCheckPassed"] = vpVal.asBool();
        } else if (vpVal.isString()) {
            std::string vpStr = vpVal.asString();
            result["validityCheckPassed"] = (vpStr == "t" || vpStr == "true");
        } else {
            result["validityCheckPassed"] = false;
        }

        result["notBefore"] = row.get("not_before", Json::nullValue);
        result["notAfter"] = row.get("not_after", Json::nullValue);

        // Compute isExpired and isNotYetValid from timestamps
        result["isExpired"] = false;  // TODO: compute from notAfter
        result["isNotYetValid"] = false;  // TODO: compute from notBefore

        result["crlCheckStatus"] = row.get("revocation_status", Json::nullValue);

        // Boolean field: crl_checked
        Json::Value ccVal = row.get("crl_checked", false);
        if (ccVal.isBool()) {
            result["crlChecked"] = ccVal.asBool();
        } else if (ccVal.isString()) {
            std::string ccStr = ccVal.asString();
            result["crlChecked"] = (ccStr == "t" || ccStr == "true");
        } else {
            result["crlChecked"] = false;
        }

        result["validatedAt"] = row.get("validation_timestamp", Json::nullValue);
        result["fingerprint"] = row.get("fingerprint_sha256", Json::nullValue);

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
        Json::Value countResult = queryExecutor_->executeScalar(countQuery, paramValues);
        int total = countResult.asInt();

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

        Json::Value queryResult = queryExecutor_->executeQuery(dataQuery, dataParams);

        // Build validations array
        Json::Value validations = Json::arrayValue;
        for (Json::ArrayIndex i = 0; i < queryResult.size(); i++) {
            Json::Value row = queryResult[i];
            Json::Value v;

            v["id"] = row.get("id", Json::nullValue);
            v["certificateId"] = row.get("certificate_id", Json::nullValue);
            v["uploadId"] = row.get("upload_id", Json::nullValue);
            v["certificateType"] = row.get("certificate_type", Json::nullValue);
            v["countryCode"] = row.get("country_code", Json::nullValue);
            v["subjectDn"] = row.get("subject_dn", Json::nullValue);
            v["issuerDn"] = row.get("issuer_dn", Json::nullValue);
            v["serialNumber"] = row.get("serial_number", Json::nullValue);
            v["validationStatus"] = row.get("validation_status", Json::nullValue);

            // Boolean field: trust_chain_valid
            Json::Value tcvVal = row.get("trust_chain_valid", false);
            if (tcvVal.isBool()) {
                v["trustChainValid"] = tcvVal.asBool();
            } else if (tcvVal.isString()) {
                std::string tcvStr = tcvVal.asString();
                v["trustChainValid"] = (tcvStr == "t" || tcvStr == "true");
            } else {
                v["trustChainValid"] = false;
            }

            v["trustChainMessage"] = row.get("trust_chain_message", Json::nullValue);

            // Parse trust_chain_path JSONB
            Json::Value tcpVal = row.get("trust_chain_path", Json::nullValue);
            if (tcpVal.isArray() && tcpVal.size() > 0) {
                v["trustChainPath"] = tcpVal[0].asString();
            } else if (tcpVal.isString()) {
                std::string tcpRaw = tcpVal.asString();
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
            } else {
                v["trustChainPath"] = "";
            }

            // Boolean field: csca_found
            Json::Value cfVal = row.get("csca_found", false);
            if (cfVal.isBool()) {
                v["cscaFound"] = cfVal.asBool();
            } else if (cfVal.isString()) {
                std::string cfStr = cfVal.asString();
                v["cscaFound"] = (cfStr == "t" || cfStr == "true");
            } else {
                v["cscaFound"] = false;
            }

            v["cscaSubjectDn"] = row.get("csca_subject_dn", Json::nullValue);

            // Boolean field: signature_valid
            Json::Value svVal = row.get("signature_valid", false);
            if (svVal.isBool()) {
                v["signatureVerified"] = svVal.asBool();
            } else if (svVal.isString()) {
                std::string svStr = svVal.asString();
                v["signatureVerified"] = (svStr == "t" || svStr == "true");
            } else {
                v["signatureVerified"] = false;
            }

            v["signatureAlgorithm"] = row.get("signature_algorithm", Json::nullValue);

            // Boolean field: validity_period_valid
            Json::Value vpVal = row.get("validity_period_valid", false);
            if (vpVal.isBool()) {
                v["validityCheckPassed"] = vpVal.asBool();
            } else if (vpVal.isString()) {
                std::string vpStr = vpVal.asString();
                v["validityCheckPassed"] = (vpStr == "t" || vpStr == "true");
            } else {
                v["validityCheckPassed"] = false;
            }

            // Boolean field: is_expired
            Json::Value expVal = row.get("is_expired", false);
            if (expVal.isBool()) {
                v["isExpired"] = expVal.asBool();
            } else if (expVal.isString()) {
                std::string expStr = expVal.asString();
                v["isExpired"] = (expStr == "t" || expStr == "true");
            } else {
                v["isExpired"] = false;
            }

            // Boolean field: is_not_yet_valid
            Json::Value nysVal = row.get("is_not_yet_valid", false);
            if (nysVal.isBool()) {
                v["isNotYetValid"] = nysVal.asBool();
            } else if (nysVal.isString()) {
                std::string nysStr = nysVal.asString();
                v["isNotYetValid"] = (nysStr == "t" || nysStr == "true");
            } else {
                v["isNotYetValid"] = false;
            }

            v["notBefore"] = row.get("not_before", Json::nullValue);
            v["notAfter"] = row.get("not_after", Json::nullValue);
            v["crlCheckStatus"] = row.get("revocation_status", Json::nullValue);

            // Boolean field: crl_checked
            Json::Value ccVal = row.get("crl_checked", false);
            if (ccVal.isBool()) {
                v["crlChecked"] = ccVal.asBool();
            } else if (ccVal.isString()) {
                std::string ccStr = ccVal.asString();
                v["crlChecked"] = (ccStr == "t" || ccStr == "true");
            } else {
                v["crlChecked"] = false;
            }

            v["validatedAt"] = row.get("validation_timestamp", Json::nullValue);
            v["fingerprint"] = row.get("fingerprint_sha256", Json::nullValue);

            validations.append(v);
        }

        // Build response with pagination metadata
        response["success"] = true;
        response["count"] = static_cast<int>(queryResult.size());
        response["total"] = total;
        response["limit"] = limit;
        response["offset"] = offset;
        response["validations"] = validations;

        spdlog::debug("[ValidationRepository] Found {} validations (total: {})", queryResult.size(), total);

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

        Json::Value result = queryExecutor_->executeScalar(query, params);
        return result.asInt();

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
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (!result.empty()) {
            int totalCount = result[0].get("total_count", 0).asInt();
            int validCount = result[0].get("valid_count", 0).asInt();
            int invalidCount = result[0].get("invalid_count", 0).asInt();
            int pendingCount = result[0].get("pending_count", 0).asInt();
            int errorCount = result[0].get("error_count", 0).asInt();
            int trustChainValidCount = result[0].get("trust_chain_valid_count", 0).asInt();
            int trustChainInvalidCount = result[0].get("trust_chain_invalid_count", 0).asInt();

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

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Get statistics failed: {}", e.what());
        stats["error"] = e.what();
    }

    return stats;
}

} // namespace repositories

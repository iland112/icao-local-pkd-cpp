#include "validation_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <random>
#include <sstream>
#include <iomanip>

namespace repositories {

// Thread-local UUID generator
static std::string generateUuid() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    uint64_t ab = dist(gen);
    uint64_t cd = dist(gen);

    // Set version 4 and variant bits
    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (ab >> 32) << "-";
    ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << "-";
    ss << std::setw(4) << (ab & 0xFFFF) << "-";
    ss << std::setw(4) << (cd >> 48) << "-";
    ss << std::setw(12) << (cd & 0x0000FFFFFFFFFFFFULL);
    return ss.str();
}

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
    spdlog::debug("[ValidationRepository] Saving validation for upload: {}...",
                  result.uploadId.substr(0, 8));

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Database-aware boolean formatting
        auto boolStr = [&dbType](bool val) -> std::string {
            if (dbType == "oracle") return val ? "1" : "0";
            return val ? "true" : "false";
        };

        // Prepare boolean strings
        std::string trustChainValidStr = boolStr(result.trustChainValid);
        std::string signatureVerifiedStr = boolStr(result.signatureVerified);
        std::string isExpiredStr = boolStr(result.isExpired);
        std::string cscaFoundStr = boolStr(result.cscaFound);
        std::string crlCheckedStr = boolStr(result.crlCheckStatus != "NOT_CHECKED");
        std::string crlRevokedStr = boolStr(result.crlCheckStatus == "REVOKED");

        // Prepare trust_chain_path as JSON
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

        // Use fingerprint as the identifier
        std::string fingerprintValue = result.fingerprint;
        if (fingerprintValue.empty()) {
            fingerprintValue = result.certificateId;
        }

        // Database-specific INSERT query
        std::string query;
        std::vector<std::string> params;

        if (dbType == "oracle") {
            // Oracle schema: NOT NULL columns: id, certificate_type, subject_dn, issuer_dn, validation_status
            std::string validityPeriodValidStr = boolStr(!result.isExpired);
            std::string revocationStatus = result.crlCheckStatus;
            std::string id = generateUuid();

            query =
                "INSERT INTO validation_result ("
                "id, upload_id, certificate_id, certificate_type, country_code, "
                "subject_dn, issuer_dn, serial_number, "
                "trust_chain_valid, trust_chain_message, csca_subject_dn, csca_found, "
                "signature_valid, signature_algorithm, "
                "validity_period_valid, not_before, not_after, "
                "crl_checked, revocation_status, "
                "validation_status"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20"
                ")";

            params = {
                id,
                result.uploadId,
                fingerprintValue,
                result.certificateType,
                result.countryCode,
                result.subjectDn.empty() ? "N/A" : result.subjectDn,
                result.issuerDn.empty() ? "N/A" : result.issuerDn,
                result.serialNumber,
                trustChainValidStr,
                result.trustChainPath.empty() ? "" : result.trustChainPath,
                result.cscaSubjectDn.empty() ? "" : result.cscaSubjectDn,
                cscaFoundStr,
                signatureVerifiedStr,
                result.signatureAlgorithm,
                validityPeriodValidStr,
                result.notBefore,
                result.notAfter,
                crlCheckedStr,
                revocationStatus,
                result.validationStatus
            };
        } else {
            // PostgreSQL schema: column names must match actual table definition
            // certificate_id is UUID FK → use result.certificateId (not fingerprint)
            std::string validityPeriodValidStr = boolStr(!result.isExpired);
            std::string revocationStatus = result.crlCheckStatus;

            query =
                "INSERT INTO validation_result ("
                "upload_id, certificate_id, certificate_type, country_code, "
                "subject_dn, issuer_dn, serial_number, "
                "trust_chain_valid, trust_chain_message, csca_subject_dn, csca_found, "
                "signature_valid, signature_algorithm, "
                "validity_period_valid, not_before, not_after, "
                "crl_checked, revocation_status, "
                "validation_status"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19"
                ")";

            params = {
                result.uploadId,                                          // $1
                result.certificateId,                                     // $2 (UUID FK)
                result.certificateType,                                   // $3
                result.countryCode,                                       // $4
                result.subjectDn.empty() ? "N/A" : result.subjectDn,     // $5 (NOT NULL)
                result.issuerDn.empty() ? "N/A" : result.issuerDn,       // $6 (NOT NULL)
                result.serialNumber,                                      // $7
                trustChainValidStr,                                       // $8
                result.trustChainMessage.empty() ? "" : result.trustChainMessage, // $9
                result.cscaSubjectDn.empty() ? "" : result.cscaSubjectDn, // $10
                cscaFoundStr,                                             // $11
                signatureVerifiedStr,                                     // $12
                result.signatureAlgorithm,                                // $13
                validityPeriodValidStr,                                   // $14
                result.notBefore,                                         // $15
                result.notAfter,                                          // $16
                crlCheckedStr,                                            // $17
                revocationStatus,                                         // $18
                result.validationStatus                                   // $19
            };
        }

        queryExecutor_->executeCommand(query, params);
        spdlog::debug("[ValidationRepository] Validation result saved successfully");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Exception in save: {}", e.what());
        return false;
    }
}

bool ValidationRepository::updateRevalidation(const std::string& certificateId,
                                              const std::string& validationStatus,
                                              bool trustChainValid,
                                              bool cscaFound,
                                              bool signatureValid,
                                              const std::string& trustChainMessage,
                                              const std::string& cscaSubjectDn)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        auto boolStr = [&](bool v) -> std::string {
            return (dbType == "oracle") ? (v ? "1" : "0") : (v ? "TRUE" : "FALSE");
        };

        std::string query =
            "UPDATE validation_result SET "
            "validation_status = $1, "
            "trust_chain_valid = " + boolStr(trustChainValid) + ", "
            "csca_found = " + boolStr(cscaFound) + ", "
            "signature_valid = " + boolStr(signatureValid) + ", "
            "trust_chain_message = $2, "
            "csca_subject_dn = $3 "
            "WHERE certificate_id = $4";

        std::vector<std::string> params = {
            validationStatus,
            trustChainMessage,
            cscaSubjectDn,
            certificateId
        };

        queryExecutor_->executeCommand(query, params);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] updateRevalidation failed: {}", e.what());
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
        std::string dbType = queryExecutor_->getDatabaseType();

        std::string query;
        if (dbType == "oracle") {
            // Oracle: certificate_id stores fingerprint directly, no JOIN needed
            query =
                "SELECT vr.id, vr.certificate_id, vr.upload_id, vr.certificate_type, "
                "       vr.country_code, vr.subject_dn, vr.issuer_dn, vr.serial_number, "
                "       vr.validation_status, vr.trust_chain_valid, vr.trust_chain_message, "
                "       vr.csca_found, vr.csca_subject_dn, "
                "       vr.signature_valid, vr.signature_algorithm, "
                "       vr.validity_period_valid, vr.not_before, vr.not_after, "
                "       vr.revocation_status, vr.crl_checked, "
                "       vr.validation_timestamp, "
                "       vr.certificate_id AS fingerprint_sha256 "
                "FROM validation_result vr "
                "WHERE vr.certificate_id = $1 "
                "OFFSET 0 ROWS FETCH NEXT 1 ROWS ONLY";
        } else {
            // PostgreSQL: JOIN with certificate table
            query =
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
        }

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

Json::Value ValidationRepository::findBySubjectDn(const std::string& subjectDn)
{
    spdlog::debug("[ValidationRepository] Finding by subject DN: {}...", subjectDn.substr(0, 60));

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Step 1: Find certificate by subject_dn (case-insensitive)
        std::string certQuery;
        if (dbType == "oracle") {
            certQuery =
                "SELECT id, fingerprint_sha256 FROM certificate "
                "WHERE certificate_type IN ('DSC', 'DSC_NC') "
                "AND LOWER(subject_dn) = LOWER($1) "
                "ORDER BY created_at DESC "
                "OFFSET 0 ROWS FETCH NEXT 1 ROWS ONLY";
        } else {
            certQuery =
                "SELECT id, fingerprint_sha256 FROM certificate "
                "WHERE certificate_type IN ('DSC', 'DSC_NC') "
                "AND LOWER(subject_dn) = LOWER($1) "
                "ORDER BY created_at DESC "
                "LIMIT 1";
        }

        std::vector<std::string> certParams = {subjectDn};
        Json::Value certResult = queryExecutor_->executeQuery(certQuery, certParams);

        if (certResult.empty()) {
            spdlog::debug("[ValidationRepository] No certificate found for subject DN: {}...",
                subjectDn.substr(0, 60));
            return Json::nullValue;
        }

        std::string certificateId = certResult[0].get("id", "").asString();
        std::string fingerprint = certResult[0].get("fingerprint_sha256", "").asString();

        if (certificateId.empty()) {
            spdlog::warn("[ValidationRepository] Certificate found but ID is empty");
            return Json::nullValue;
        }

        // Step 2: Find validation result for this certificate
        std::string query;
        std::vector<std::string> params;

        if (dbType == "oracle") {
            // Oracle: certificate_id stores fingerprint directly
            query =
                "SELECT vr.id, vr.certificate_id, vr.upload_id, vr.certificate_type, "
                "       vr.country_code, vr.subject_dn, vr.issuer_dn, vr.serial_number, "
                "       vr.validation_status, vr.trust_chain_valid, vr.trust_chain_message, "
                "       vr.csca_found, vr.csca_subject_dn, "
                "       vr.signature_valid, vr.signature_algorithm, "
                "       vr.validity_period_valid, vr.not_before, vr.not_after, "
                "       vr.revocation_status, vr.crl_checked, "
                "       vr.validation_timestamp, "
                "       vr.certificate_id AS fingerprint_sha256 "
                "FROM validation_result vr "
                "WHERE vr.certificate_id = $1 "
                "ORDER BY vr.validation_timestamp DESC "
                "OFFSET 0 ROWS FETCH NEXT 1 ROWS ONLY";
            params = {fingerprint.empty() ? certificateId : fingerprint};
        } else {
            // PostgreSQL: JOIN with certificate table
            query =
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
                "WHERE vr.certificate_id = $1 "
                "ORDER BY vr.validation_timestamp DESC "
                "LIMIT 1";
            params = {certificateId};
        }

        Json::Value queryResult = queryExecutor_->executeQuery(query, params);

        if (queryResult.empty()) {
            spdlog::debug("[ValidationRepository] Certificate found but no validation result for DN: {}...",
                subjectDn.substr(0, 60));
            return Json::nullValue;
        }

        // Extract first row and build response (same format as findByFingerprint)
        Json::Value row = queryResult[0];

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

        // Boolean fields
        Json::Value tcvVal = row.get("trust_chain_valid", false);
        if (tcvVal.isBool()) result["trustChainValid"] = tcvVal.asBool();
        else if (tcvVal.isString()) {
            std::string s = tcvVal.asString();
            result["trustChainValid"] = (s == "t" || s == "true");
        } else result["trustChainValid"] = false;

        result["trustChainMessage"] = row.get("trust_chain_message", Json::nullValue);

        // trust_chain_path JSONB
        Json::Value tcpVal = row.get("trust_chain_path", Json::nullValue);
        if (tcpVal.isArray() && tcpVal.size() > 0) {
            result["trustChainPath"] = tcpVal[0].asString();
        } else if (tcpVal.isString()) {
            std::string tcpRaw = tcpVal.asString();
            try {
                Json::Reader reader;
                Json::Value pathArray;
                if (reader.parse(tcpRaw, pathArray) && pathArray.isArray() && pathArray.size() > 0)
                    result["trustChainPath"] = pathArray[0].asString();
                else result["trustChainPath"] = "";
            } catch (...) { result["trustChainPath"] = ""; }
        } else result["trustChainPath"] = "";

        Json::Value cfVal = row.get("csca_found", false);
        if (cfVal.isBool()) result["cscaFound"] = cfVal.asBool();
        else if (cfVal.isString()) {
            std::string s = cfVal.asString();
            result["cscaFound"] = (s == "t" || s == "true");
        } else result["cscaFound"] = false;

        result["cscaSubjectDn"] = row.get("csca_subject_dn", Json::nullValue);

        Json::Value svVal = row.get("signature_valid", false);
        if (svVal.isBool()) result["signatureVerified"] = svVal.asBool();
        else if (svVal.isString()) {
            std::string s = svVal.asString();
            result["signatureVerified"] = (s == "t" || s == "true");
        } else result["signatureVerified"] = false;

        result["signatureAlgorithm"] = row.get("signature_algorithm", Json::nullValue);

        Json::Value vpVal = row.get("validity_period_valid", false);
        if (vpVal.isBool()) result["validityCheckPassed"] = vpVal.asBool();
        else if (vpVal.isString()) {
            std::string s = vpVal.asString();
            result["validityCheckPassed"] = (s == "t" || s == "true");
        } else result["validityCheckPassed"] = false;

        result["notBefore"] = row.get("not_before", Json::nullValue);
        result["notAfter"] = row.get("not_after", Json::nullValue);
        result["isExpired"] = false;
        result["isNotYetValid"] = false;
        result["crlCheckStatus"] = row.get("revocation_status", Json::nullValue);

        Json::Value ccVal = row.get("crl_checked", false);
        if (ccVal.isBool()) result["crlChecked"] = ccVal.asBool();
        else if (ccVal.isString()) {
            std::string s = ccVal.asString();
            result["crlChecked"] = (s == "t" || s == "true");
        } else result["crlChecked"] = false;

        result["validatedAt"] = row.get("validation_timestamp", Json::nullValue);
        result["fingerprint"] = row.get("fingerprint_sha256", Json::nullValue);

        spdlog::debug("[ValidationRepository] Found validation result for subject DN: {}...",
            subjectDn.substr(0, 60));

        return result;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] findBySubjectDn failed: {}", e.what());
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
        int total = 0;
        if (countResult.isInt()) total = countResult.asInt();
        else if (countResult.isString()) { try { total = std::stoi(countResult.asString()); } catch(...) {} }

        std::string dbType = queryExecutor_->getDatabaseType();

        // Fetch validation results
        std::string dataQuery;
        if (dbType == "oracle") {
            // Oracle: no trust_chain_path/is_expired/is_not_yet_valid columns
            // certificate_id stores fingerprint directly, use OFFSET FETCH
            dataQuery =
                "SELECT vr.id, vr.certificate_id, vr.upload_id, vr.certificate_type, "
                "       vr.country_code, vr.subject_dn, vr.issuer_dn, vr.serial_number, "
                "       vr.validation_status, vr.trust_chain_valid, vr.trust_chain_message, "
                "       vr.csca_found, vr.csca_subject_dn, "
                "       vr.signature_valid, vr.signature_algorithm, "
                "       vr.validity_period_valid, "
                "       vr.not_before, vr.not_after, "
                "       vr.revocation_status, vr.crl_checked, "
                "       vr.validation_timestamp, "
                "       vr.certificate_id AS fingerprint_sha256 "
                "FROM validation_result vr "
                + whereClause +
                " ORDER BY vr.validation_status, vr.validation_timestamp DESC "
                " OFFSET $" + std::to_string(paramIdx) +
                " ROWS FETCH NEXT $" + std::to_string(paramIdx + 1) + " ROWS ONLY";
        } else {
            // PostgreSQL: actual table columns, LIMIT/OFFSET
            dataQuery =
                "SELECT vr.id, vr.certificate_id, vr.upload_id, vr.certificate_type, "
                "       vr.country_code, vr.subject_dn, vr.issuer_dn, vr.serial_number, "
                "       vr.validation_status, vr.trust_chain_valid, vr.trust_chain_message, "
                "       vr.trust_chain_path, vr.csca_found, vr.csca_subject_dn, "
                "       vr.signature_valid, vr.signature_algorithm, "
                "       vr.validity_period_valid, "
                "       vr.not_before, vr.not_after, "
                "       vr.revocation_status, vr.crl_checked, "
                "       vr.validation_timestamp, c.fingerprint_sha256 "
                "FROM validation_result vr "
                "LEFT JOIN certificate c ON vr.certificate_id = c.id "
                + whereClause +
                " ORDER BY vr.validation_status, vr.validation_timestamp DESC "
                " LIMIT $" + std::to_string(paramIdx) +
                " OFFSET $" + std::to_string(paramIdx + 1);
        }

        // Add limit and offset to params
        // Oracle: OFFSET $n ROWS FETCH NEXT $n+1 ROWS ONLY (offset first, then limit)
        // PostgreSQL: LIMIT $n OFFSET $n+1 (limit first, then offset)
        std::vector<std::string> dataParams = paramValues;
        if (dbType == "oracle") {
            dataParams.push_back(std::to_string(offset));
            dataParams.push_back(std::to_string(limit));
        } else {
            dataParams.push_back(std::to_string(limit));
            dataParams.push_back(std::to_string(offset));
        }

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
        if (result.isInt()) return result.asInt();
        if (result.isString()) { try { return std::stoi(result.asString()); } catch(...) { return 0; } }
        return 0;

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
        std::string dbType = queryExecutor_->getDatabaseType();

        // Helper to safely get int from Oracle (returns strings) or PostgreSQL
        auto safeInt = [](const Json::Value& val) -> int {
            if (val.isInt()) return val.asInt();
            if (val.isString()) { try { return std::stoi(val.asString()); } catch(...) { return 0; } }
            return 0;
        };

        // Oracle uses NUMBER(1) for booleans (1/0), PostgreSQL uses TRUE/FALSE
        std::string trueCheck = (dbType == "oracle") ? "= 1" : "= TRUE";
        std::string falseCheck = (dbType == "oracle") ? "= 0" : "= FALSE";

        std::string query =
            "SELECT "
            "  COUNT(*) as total_count, "
            "  SUM(CASE WHEN validation_status IN ('VALID', 'EXPIRED_VALID') THEN 1 ELSE 0 END) as valid_count, "
            "  SUM(CASE WHEN validation_status = 'EXPIRED_VALID' THEN 1 ELSE 0 END) as expired_valid_count, "
            "  SUM(CASE WHEN validation_status = 'INVALID' THEN 1 ELSE 0 END) as invalid_count, "
            "  SUM(CASE WHEN validation_status = 'PENDING' THEN 1 ELSE 0 END) as pending_count, "
            "  SUM(CASE WHEN validation_status = 'ERROR' THEN 1 ELSE 0 END) as error_count, "
            "  SUM(CASE WHEN trust_chain_valid " + trueCheck + " THEN 1 ELSE 0 END) as trust_chain_valid_count, "
            "  SUM(CASE WHEN trust_chain_valid " + falseCheck + " THEN 1 ELSE 0 END) as trust_chain_invalid_count "
            "FROM validation_result "
            "WHERE upload_id = $1";

        std::vector<std::string> params = {uploadId};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (!result.empty()) {
            int totalCount = safeInt(result[0].get("total_count", 0));
            int validCount = safeInt(result[0].get("valid_count", 0));
            int expiredValidCount = safeInt(result[0].get("expired_valid_count", 0));
            int invalidCount = safeInt(result[0].get("invalid_count", 0));
            int pendingCount = safeInt(result[0].get("pending_count", 0));
            int errorCount = safeInt(result[0].get("error_count", 0));
            int trustChainValidCount = safeInt(result[0].get("trust_chain_valid_count", 0));
            int trustChainInvalidCount = safeInt(result[0].get("trust_chain_invalid_count", 0));

            // Calculate trust chain success rate
            double trustChainSuccessRate = 0.0;
            if (totalCount > 0) {
                trustChainSuccessRate = (static_cast<double>(trustChainValidCount) / totalCount) * 100.0;
            }

            stats["totalCount"] = totalCount;
            stats["validCount"] = validCount;
            stats["expiredValidCount"] = expiredValidCount;
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

Json::Value ValidationRepository::getReasonBreakdown()
{
    spdlog::debug("[ValidationRepository] Getting validation reason breakdown");

    Json::Value response;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        auto safeInt = [](const Json::Value& val) -> int {
            if (val.isInt()) return val.asInt();
            if (val.isString()) { try { return std::stoi(val.asString()); } catch(...) { return 0; } }
            return 0;
        };

        // GROUP BY validation_status, trust_chain_message, country_code
        // Only INVALID and PENDING are interesting for reason breakdown
        std::string query;
        if (dbType == "oracle") {
            query =
                "SELECT validation_status, trust_chain_message, country_code, COUNT(*) AS cnt "
                "FROM validation_result "
                "WHERE validation_status IN ('INVALID', 'PENDING') "
                "AND trust_chain_message IS NOT NULL "
                "GROUP BY validation_status, trust_chain_message, country_code "
                "ORDER BY validation_status, cnt DESC";
        } else {
            query =
                "SELECT validation_status, trust_chain_message, country_code, COUNT(*) AS cnt "
                "FROM validation_result "
                "WHERE validation_status IN ('INVALID', 'PENDING') "
                "AND trust_chain_message IS NOT NULL AND trust_chain_message != '' "
                "GROUP BY validation_status, trust_chain_message, country_code "
                "ORDER BY validation_status, cnt DESC";
        }

        Json::Value result = queryExecutor_->executeQuery(query, {});

        Json::Value reasons(Json::arrayValue);
        for (Json::ArrayIndex i = 0; i < result.size(); i++) {
            Json::Value row = result[i];
            Json::Value reason;
            reason["status"] = row.get("validation_status", "").asString();
            reason["reason"] = row.get("trust_chain_message", "").asString();
            reason["countryCode"] = row.get("country_code", "").asString();
            reason["count"] = safeInt(row.get("cnt", 0));
            reasons.append(reason);
        }

        response["success"] = true;
        response["reasons"] = reasons;

        // Expired certificates breakdown: GROUP BY country_code, year of not_after
        std::string expiredQuery;
        if (dbType == "oracle") {
            expiredQuery =
                "SELECT country_code, "
                "  EXTRACT(YEAR FROM TO_TIMESTAMP(not_after, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')) AS expire_year, "
                "  COUNT(*) AS cnt "
                "FROM validation_result "
                "WHERE validity_period_valid = 0 "
                "AND not_after IS NOT NULL "
                "GROUP BY country_code, EXTRACT(YEAR FROM TO_TIMESTAMP(not_after, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')) "
                "ORDER BY cnt DESC";
        } else {
            expiredQuery =
                "SELECT country_code, "
                "  EXTRACT(YEAR FROM not_after::timestamp) AS expire_year, "
                "  COUNT(*) AS cnt "
                "FROM validation_result "
                "WHERE validity_period_valid = FALSE "
                "AND not_after IS NOT NULL AND not_after != '' "
                "GROUP BY country_code, EXTRACT(YEAR FROM not_after::timestamp) "
                "ORDER BY cnt DESC";
        }

        Json::Value expiredResult = queryExecutor_->executeQuery(expiredQuery, {});
        Json::Value expired(Json::arrayValue);
        for (Json::ArrayIndex i = 0; i < expiredResult.size(); i++) {
            Json::Value row = expiredResult[i];
            Json::Value entry;
            entry["countryCode"] = row.get("country_code", "").asString();

            // expire_year may come as double (EXTRACT returns numeric)
            Json::Value yearVal = row.get("expire_year", 0);
            if (yearVal.isDouble()) entry["expireYear"] = static_cast<int>(yearVal.asDouble());
            else if (yearVal.isInt()) entry["expireYear"] = yearVal.asInt();
            else if (yearVal.isString()) { try { entry["expireYear"] = std::stoi(yearVal.asString()); } catch(...) { entry["expireYear"] = 0; } }
            else entry["expireYear"] = 0;

            entry["count"] = safeInt(row.get("cnt", 0));
            expired.append(entry);
        }
        response["expired"] = expired;

        // Revoked certificates breakdown: GROUP BY country_code
        std::string revokedQuery =
            "SELECT country_code, COUNT(*) AS cnt "
            "FROM validation_result "
            "WHERE revocation_status = 'REVOKED' "
            "GROUP BY country_code "
            "ORDER BY cnt DESC";

        Json::Value revokedResult = queryExecutor_->executeQuery(revokedQuery, {});
        Json::Value revoked(Json::arrayValue);
        for (Json::ArrayIndex i = 0; i < revokedResult.size(); i++) {
            Json::Value row = revokedResult[i];
            Json::Value entry;
            entry["countryCode"] = row.get("country_code", "").asString();
            entry["count"] = safeInt(row.get("cnt", 0));
            revoked.append(entry);
        }
        response["revoked"] = revoked;

        spdlog::info("[ValidationRepository] Reason breakdown: {} reasons, {} expired, {} revoked",
                     reasons.size(), expired.size(), revoked.size());

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] getReasonBreakdown failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
        response["reasons"] = Json::arrayValue;
        response["expired"] = Json::arrayValue;
        response["revoked"] = Json::arrayValue;
    }

    return response;
}

} // namespace repositories

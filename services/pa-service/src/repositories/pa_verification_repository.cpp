/**
 * @file pa_verification_repository.cpp
 * @brief Implementation of PaVerificationRepository (Query Executor Pattern)
 * @updated 2026-02-05 (Phase 5.1: Database-agnostic implementation)
 */

#include "pa_verification_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <map>

namespace repositories {

// ============================================================================
// Helper: Safe integer extraction from Json::Value (Oracle returns strings)
// ============================================================================

static int safeInt(const Json::Value& val, int defaultValue = 0) {
    if (val.isNull()) return defaultValue;
    if (val.isInt()) return val.asInt();
    if (val.isUInt()) return static_cast<int>(val.asUInt());
    if (val.isString()) {
        try { return std::stoi(val.asString()); }
        catch (...) { return defaultValue; }
    }
    if (val.isDouble()) return static_cast<int>(val.asDouble());
    return defaultValue;
}

// ============================================================================
// Constructor
// ============================================================================

PaVerificationRepository::PaVerificationRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("PaVerificationRepository: queryExecutor cannot be nullptr");
    }

    spdlog::debug("[PaVerificationRepository] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

// ============================================================================
// CRUD Operations
// ============================================================================

std::string PaVerificationRepository::insert(const domain::models::PaVerification& verification) {
    spdlog::debug("[PaVerificationRepository] Inserting PA verification record");

    try {
        // Step 1: Generate UUID using database-specific function
        // PostgreSQL: uuid_generate_v4()
        // Oracle: LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), '([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', '\1-\2-\3-\4-\5'))
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string uuidQuery;

        if (dbType == "postgres") {
            uuidQuery = "SELECT uuid_generate_v4()::text as id";
        } else {
            // Oracle: Convert SYS_GUID() to UUID format (lowercase with hyphens)
            uuidQuery = "SELECT LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), "
                       "'([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', "
                       "'\\1-\\2-\\3-\\4-\\5')) as id FROM DUAL";
        }

        Json::Value uuidResult = queryExecutor_->executeQuery(uuidQuery, {});
        if (uuidResult.empty()) {
            throw std::runtime_error("Failed to generate UUID");
        }
        std::string generatedId = uuidResult[0]["id"].asString();

        // Step 2: Insert with generated UUID (no RETURNING clause needed)
        const char* insertQuery =
            "INSERT INTO pa_verification ("
            "id, "  // Add id column
            "issuing_country, document_number, verification_status, sod_hash, sod_binary, "
            "dsc_subject_dn, dsc_serial_number, dsc_issuer_dn, dsc_fingerprint, "
            "csca_subject_dn, csca_fingerprint, "
            "trust_chain_valid, trust_chain_message, "
            "sod_signature_valid, sod_signature_message, "
            "dg_hashes_valid, dg_hashes_message, "
            "crl_status, crl_message, "
            "verification_message, "
            "client_ip, user_agent"
            ") VALUES ("
            "$1, "  // id value
            "$2, $3, $4, $5, $6, "
            "$7, $8, $9, $10, "
            "$11, $12, "
            "$13, $14, "
            "$15, $16, "
            "$17, $18, "
            "$19, $20, "
            "$21, "
            "$22, $23"
            ")";

        // Database-aware boolean formatting (reuse dbType from line 41)
        auto boolStr = [&dbType](bool val) -> std::string {
            return (dbType == "oracle") ? (val ? "1" : "0") : (val ? "true" : "false");
        };

        // Convert SOD binary to hex string for BYTEA storage
        std::string sodBinaryHex;
        if (!verification.sodBinary.empty()) {
            std::ostringstream hexStream;
            hexStream << "\\x";
            for (auto b : verification.sodBinary) {
                hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            }
            sodBinaryHex = hexStream.str();
        }

        std::vector<std::string> params = {
            generatedId,                                                     // $1: id
            verification.countryCode,                                        // $2
            verification.documentNumber,                                     // $3
            verification.verificationStatus,                                 // $4
            verification.sodHash,                                            // $5
            sodBinaryHex,                                                    // $6: sod_binary
            verification.dscSubject,                                         // $7
            verification.dscSerialNumber,                                    // $8
            verification.dscIssuer,                                          // $9
            "",                                                              // $10: dsc_fingerprint (TODO)
            verification.cscaSubject,                                        // $11
            "",                                                              // $12: csca_fingerprint (TODO)
            boolStr(verification.certificateChainValid),                     // $13
            "",                                                              // $14: trust_chain_message (TODO)
            boolStr(verification.sodSignatureValid),                         // $15
            "",                                                              // $16: sod_signature_message (TODO)
            boolStr(verification.dataGroupsValid),                           // $17
            "",                                                              // $18: dg_hashes_message (TODO)
            verification.crlStatus,                                          // $19
            verification.crlMessage.value_or(""),                            // $20
            verification.validationErrors.value_or(""),                      // $21
            verification.ipAddress.value_or(""),                             // $22
            verification.userAgent.value_or("")                              // $23
        };

        int rowsAffected = queryExecutor_->executeCommand(insertQuery, params);

        if (rowsAffected == 0) {
            throw std::runtime_error("Insert failed: no rows affected");
        }

        spdlog::info("[PaVerificationRepository] PA verification inserted with ID: {}", generatedId);
        return generatedId;

    } catch (const std::exception& e) {
        spdlog::error("[PaVerificationRepository] Insert failed: {}", e.what());
        throw;
    }
}

Json::Value PaVerificationRepository::findById(const std::string& id) {
    spdlog::debug("[PaVerificationRepository] Finding PA verification by ID: {}", id);

    try {
        const char* query =
            "SELECT id, document_number, issuing_country, verification_status, sod_hash, "
            "dsc_subject_dn, dsc_serial_number, dsc_issuer_dn, dsc_fingerprint, "
            "csca_subject_dn, csca_fingerprint, "
            "trust_chain_valid, trust_chain_message, "
            "sod_signature_valid, sod_signature_message, "
            "dg_hashes_valid, dg_hashes_message, "
            "crl_status, crl_message, "
            "verification_message, "
            "request_timestamp, completed_timestamp, client_ip, user_agent "
            "FROM pa_verification WHERE id = $1";

        std::vector<std::string> params = {id};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::debug("[PaVerificationRepository] PA verification not found: {}", id);
            return Json::Value::null;
        }

        // Apply field name mapping (snake_case -> camelCase)
        return toCamelCase(result[0]);

    } catch (const std::exception& e) {
        spdlog::error("[PaVerificationRepository] Find by ID failed: {}", e.what());
        return Json::Value::null;
    }
}

Json::Value PaVerificationRepository::findAll(
    int limit,
    int offset,
    const std::string& status,
    const std::string& countryCode)
{
    spdlog::debug("[PaVerificationRepository] Finding all PA verifications (limit: {}, offset: {}, status: {}, country: {})",
        limit, offset, status, countryCode);

    try {
        std::vector<std::string> params;
        std::string whereClause = buildWhereClause(status, countryCode, params);

        // Count query
        std::ostringstream countQuery;
        countQuery << "SELECT COUNT(*) FROM pa_verification";
        if (!whereClause.empty()) {
            countQuery << " WHERE " << whereClause;
        }

        Json::Value countResult = queryExecutor_->executeScalar(countQuery.str(), params);
        int total = safeInt(countResult);

        // Data query
        std::ostringstream dataQuery;
        dataQuery << "SELECT id, document_number, issuing_country, verification_status, sod_hash, "
                  << "dsc_subject_dn, dsc_serial_number, dsc_issuer_dn, dsc_fingerprint, "
                  << "csca_subject_dn, csca_fingerprint, "
                  << "trust_chain_valid, trust_chain_message, "
                  << "sod_signature_valid, sod_signature_message, "
                  << "dg_hashes_valid, dg_hashes_message, "
                  << "crl_status, crl_message, "
                  << "verification_message, "
                  << "request_timestamp, completed_timestamp, client_ip, user_agent "
                  << "FROM pa_verification";

        if (!whereClause.empty()) {
            dataQuery << " WHERE " << whereClause;
        }

        std::string dbType = queryExecutor_->getDatabaseType();
        dataQuery << " ORDER BY request_timestamp DESC";
        if (dbType == "oracle") {
            dataQuery << " OFFSET " << offset << " ROWS FETCH NEXT " << limit << " ROWS ONLY";
        } else {
            dataQuery << " LIMIT " << limit << " OFFSET " << offset;
        }

        Json::Value dataResult = queryExecutor_->executeQuery(dataQuery.str(), params);

        // Build response
        Json::Value response;
        response["success"] = true;
        response["total"] = total;
        response["page"] = (offset / limit) + 1;
        response["size"] = limit;

        Json::Value dataArray = Json::arrayValue;
        for (const auto& row : dataResult) {
            dataArray.append(toCamelCase(row));
        }
        response["data"] = dataArray;

        spdlog::debug("[PaVerificationRepository] Found {} verifications (total: {})",
            dataResult.size(), total);

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[PaVerificationRepository] Find all failed: {}", e.what());

        Json::Value errorResponse;
        errorResponse["success"] = false;
        errorResponse["error"] = e.what();
        return errorResponse;
    }
}

Json::Value PaVerificationRepository::getStatistics() {
    spdlog::debug("[PaVerificationRepository] Getting PA verification statistics");

    try {
        Json::Value stats;

        // Total count
        const char* totalQuery = "SELECT COUNT(*) FROM pa_verification";
        stats["totalVerifications"] = safeInt(queryExecutor_->executeScalar(totalQuery));

        // Count by status
        const char* statusQuery =
            "SELECT verification_status, COUNT(*) as count "
            "FROM pa_verification "
            "GROUP BY verification_status";
        Json::Value statusResult = queryExecutor_->executeQuery(statusQuery);

        Json::Value statusCounts;
        for (const auto& row : statusResult) {
            std::string status = row["verification_status"].asString();
            statusCounts[status] = safeInt(row["count"]);
        }
        stats["byStatus"] = statusCounts;

        // Count by country
        std::string countryQuery =
            "SELECT issuing_country, COUNT(*) as count "
            "FROM pa_verification "
            "GROUP BY issuing_country "
            "ORDER BY count DESC ";
        if (queryExecutor_->getDatabaseType() == "oracle") {
            countryQuery += "FETCH FIRST 10 ROWS ONLY";
        } else {
            countryQuery += "LIMIT 10";
        }
        Json::Value countryResult = queryExecutor_->executeQuery(countryQuery);

        Json::Value countryCounts = Json::arrayValue;
        for (const auto& row : countryResult) {
            Json::Value country;
            country["country"] = row["issuing_country"].asString();
            country["count"] = safeInt(row["count"]);
            countryCounts.append(country);
        }
        stats["byCountry"] = countryCounts;

        // Success rate
        const char* successQuery =
            "SELECT "
            "COUNT(CASE WHEN verification_status = 'VALID' THEN 1 END) as valid_count, "
            "COUNT(*) as total_count "
            "FROM pa_verification";
        Json::Value successResult = queryExecutor_->executeQuery(successQuery);

        if (!successResult.empty()) {
            int validCount = safeInt(successResult[0]["valid_count"]);
            int totalCount = safeInt(successResult[0]["total_count"]);
            double successRate = totalCount > 0 ? (validCount * 100.0 / totalCount) : 0.0;
            stats["successRate"] = successRate;
        }

        spdlog::debug("[PaVerificationRepository] Statistics retrieved successfully");
        return stats;

    } catch (const std::exception& e) {
        spdlog::error("[PaVerificationRepository] Get statistics failed: {}", e.what());

        Json::Value errorResponse;
        errorResponse["error"] = e.what();
        return errorResponse;
    }
}

bool PaVerificationRepository::deleteById(const std::string& id) {
    spdlog::debug("[PaVerificationRepository] Deleting PA verification: {}", id);

    try {
        const char* query = "DELETE FROM pa_verification WHERE id = $1";
        std::vector<std::string> params = {id};

        int affectedRows = queryExecutor_->executeCommand(query, params);

        if (affectedRows > 0) {
            spdlog::info("[PaVerificationRepository] Deleted PA verification: {}", id);
            return true;
        }

        spdlog::warn("[PaVerificationRepository] PA verification not found for deletion: {}", id);
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[PaVerificationRepository] Delete failed: {}", e.what());
        return false;
    }
}

bool PaVerificationRepository::updateStatus(const std::string& id, const std::string& status) {
    spdlog::debug("[PaVerificationRepository] Updating PA verification status: ID={}, status={}", id, status);

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query =
            "UPDATE pa_verification "
            "SET verification_status = $1, completed_timestamp = " +
            std::string(dbType == "oracle" ? "SYSTIMESTAMP" : "NOW()") +
            " WHERE id = $2";

        std::vector<std::string> params = {status, id};
        int affectedRows = queryExecutor_->executeCommand(query, params);

        if (affectedRows > 0) {
            spdlog::info("[PaVerificationRepository] Status updated: {} -> {}", id, status);
            return true;
        }

        spdlog::warn("[PaVerificationRepository] PA verification not found for update: {}", id);
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[PaVerificationRepository] Update status failed: {}", e.what());
        return false;
    }
}

// ============================================================================
// Helper Methods
// ============================================================================

std::string PaVerificationRepository::buildWhereClause(
    const std::string& status,
    const std::string& countryCode,
    std::vector<std::string>& params)
{
    std::ostringstream where;
    int paramCount = 1;

    if (!status.empty()) {
        if (paramCount > 1) where << " AND ";
        where << "verification_status = $" << paramCount++;
        params.push_back(status);
    }

    if (!countryCode.empty()) {
        if (paramCount > 1) where << " AND ";
        where << "issuing_country = $" << paramCount++;
        params.push_back(countryCode);
    }

    return where.str();
}

Json::Value PaVerificationRepository::toCamelCase(const Json::Value& dbRow) {
    // Field name mapping: snake_case (DB) -> camelCase (Frontend)
    static const std::map<std::string, std::string> fieldMapping = {
        {"id", "verificationId"},
        {"verification_status", "status"},
        {"request_timestamp", "verificationTimestamp"},
        {"completed_timestamp", "completedTimestamp"},
        {"issuing_country", "issuingCountry"},
        {"document_number", "documentNumber"},
        {"sod_hash", "sodHash"},
        {"sod_binary", "sodBinary"},
        {"sod_signature_valid", "sodSignatureValid"},
        {"sod_signature_message", "sodSignatureMessage"},
        {"trust_chain_valid", "trustChainValid"},
        {"trust_chain_message", "trustChainMessage"},
        {"dg_hashes_valid", "dgHashesValid"},
        {"dg_hashes_message", "dgHashesMessage"},
        {"crl_status", "crlStatus"},
        {"crl_message", "crlMessage"},
        {"dsc_subject_dn", "dscSubjectDn"},
        {"dsc_serial_number", "dscSerialNumber"},
        {"dsc_issuer_dn", "dscIssuerDn"},
        {"dsc_fingerprint", "dscFingerprint"},
        {"csca_subject_dn", "cscaSubjectDn"},
        {"csca_fingerprint", "cscaFingerprint"},
        {"verification_message", "verificationMessage"},
        {"client_ip", "clientIp"},
        {"user_agent", "userAgent"}
    };

    Json::Value camelCaseRow;

    for (const auto& key : dbRow.getMemberNames()) {
        std::string camelKey = key;

        // Apply mapping if exists
        auto it = fieldMapping.find(key);
        if (it != fieldMapping.end()) {
            camelKey = it->second;
        }

        const Json::Value& value = dbRow[key];

        // Handle NULL values
        if (value.isNull()) {
            camelCaseRow[camelKey] = Json::Value::null;
            continue;
        }

        // Handle boolean fields (PostgreSQL returns 't'/'f' strings)
        if (key.find("_valid") != std::string::npos ||
            key.find("_checked") != std::string::npos ||
            key.find("_expired") != std::string::npos ||
            key == "revoked") {
            if (value.isString()) {
                std::string strVal = value.asString();
                camelCaseRow[camelKey] = (strVal == "t" || strVal == "true" || strVal == "1");
            } else {
                camelCaseRow[camelKey] = value.asBool();
            }
        }
        // All other fields
        else {
            camelCaseRow[camelKey] = value;
        }
    }

    return camelCaseRow;
}

} // namespace repositories

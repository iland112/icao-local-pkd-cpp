/**
 * @file validation_repository.cpp
 * @brief Validation repository implementation
 */
#include "validation_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <json/json.h>
#include <stdexcept>

namespace icao::relay::repositories {

ValidationRepository::ValidationRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor) {
    if (!queryExecutor_) {
        throw std::invalid_argument("QueryExecutor cannot be null");
    }
    spdlog::debug("[ValidationRepository] Initialized");
}

std::vector<domain::ValidationResult> ValidationRepository::findAllWithExpirationInfo() {
    const char* query = R"(
        SELECT id, certificate_id, certificate_type, country_code,
               validity_period_valid, validation_status, not_after
        FROM validation_result
        WHERE not_after IS NOT NULL
        ORDER BY not_after ASC
    )";

    try {
        std::vector<std::string> params;  // Empty params vector
        Json::Value result = queryExecutor_->executeQuery(query, params);

        std::vector<domain::ValidationResult> validations;
        if (result.isArray()) {
            for (const auto& row : result) {
                bool vpValid = common::db::getBool(row, "validity_period_valid", false);
                validations.emplace_back(
                    row["id"].asString(),
                    row["certificate_id"].asString(),
                    row["certificate_type"].asString(),
                    row["country_code"].asString(),
                    vpValid,
                    row["validation_status"].asString(),
                    row["not_after"].asString()
                );
            }
        }

        spdlog::debug("[ValidationRepository] Found {} validation results with expiration info", validations.size());
        return validations;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Failed to find validation results: {}", e.what());
        throw;
    }
}

bool ValidationRepository::updateValidityStatus(
    const std::string& id,
    bool validityPeriodValid,
    const std::string& newStatus
) {
    const char* query = R"(
        UPDATE validation_result
        SET validity_period_valid = $1,
            validation_status = $2
        WHERE id = $3
    )";

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::vector<std::string> params;
        params.push_back(common::db::boolLiteral(dbType, validityPeriodValid));
        params.push_back(newStatus);
        params.push_back(id);

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::debug("[ValidationRepository] Updated validation {} to status: {}, valid: {}",
                         id, newStatus, validityPeriodValid);
            return true;
        }

        spdlog::warn("[ValidationRepository] No rows updated for validation id: {}", id);
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Failed to update validation status for {}: {}", id, e.what());
        throw;
    }
}

int ValidationRepository::countExpiredByUploadId(const std::string& uploadId) {
    std::string dbType = queryExecutor_->getDatabaseType();
    std::string falseVal = common::db::boolLiteral(dbType, false);

    std::string query =
        "SELECT COUNT(*) as count "
        "FROM validation_result vr "
        "JOIN certificate c ON vr.certificate_id = c.id "
        "WHERE c.upload_id = $1 "
        "AND vr.validity_period_valid = " + falseVal;

    try {
        std::vector<std::string> params;
        params.push_back(uploadId);

        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.isArray() && !result.empty()) {
            int count = 0;
            const auto& v = result[0]["count"];
            if (v.isInt()) count = v.asInt();
            else if (v.isString()) { try { count = std::stoi(v.asString()); } catch (...) { /* non-critical: use default */ } }
            spdlog::debug("[ValidationRepository] Found {} expired certificates for upload {}", count, uploadId);
            return count;
        }

        return 0;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Failed to count expired certificates for upload {}: {}", uploadId, e.what());
        throw;
    }
}

int ValidationRepository::updateAllUploadExpiredCounts() {
    std::string dbType = queryExecutor_->getDatabaseType();
    std::string falseVal = common::db::boolLiteral(dbType, false);

    std::string query =
        "UPDATE uploaded_file uf "
        "SET expired_count = ("
        "    SELECT COUNT(*) "
        "    FROM validation_result vr "
        "    JOIN certificate c ON vr.certificate_id = c.id "
        "    WHERE c.upload_id = uf.id "
        "    AND vr.validity_period_valid = " + falseVal +
        ")";

    try {
        std::vector<std::string> params;  // Empty params vector

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        spdlog::info("[ValidationRepository] Updated expired counts for {} upload files", rowsAffected);
        return rowsAffected;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Failed to update upload expired counts: {}", e.what());
        throw;
    }
}

bool ValidationRepository::saveRevalidationHistory(
    int totalProcessed,
    int newlyExpired,
    int newlyValid,
    int unchanged,
    int errors,
    int durationMs
) {
    const char* query = R"(
        INSERT INTO revalidation_history
        (total_processed, newly_expired, newly_valid, unchanged, errors, duration_ms)
        VALUES ($1, $2, $3, $4, $5, $6)
    )";

    try {
        std::vector<std::string> params;
        params.push_back(std::to_string(totalProcessed));
        params.push_back(std::to_string(newlyExpired));
        params.push_back(std::to_string(newlyValid));
        params.push_back(std::to_string(unchanged));
        params.push_back(std::to_string(errors));
        params.push_back(std::to_string(durationMs));

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[ValidationRepository] Saved revalidation history: {} processed, {} expired, {} valid, {} unchanged, {} errors ({}ms)",
                        totalProcessed, newlyExpired, newlyValid, unchanged, errors, durationMs);
            return true;
        }

        spdlog::warn("[ValidationRepository] Failed to save revalidation history");
        return false;

    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Failed to save revalidation history: {}", e.what());
        return false;  // Don't throw - history save failure shouldn't break revalidation
    }
}

bool ValidationRepository::saveRevalidationHistoryExtended(
    int totalProcessed, int newlyExpired, int newlyValid,
    int unchanged, int errors, int durationMs,
    int tcProcessed, int tcNewlyValid, int tcStillPending, int tcErrors,
    int crlChecked, int crlRevoked, int crlUnavailable, int crlExpired, int crlErrors
) {
    const char* query = R"(
        INSERT INTO revalidation_history
        (total_processed, newly_expired, newly_valid, unchanged, errors, duration_ms,
         tc_processed, tc_newly_valid, tc_still_pending, tc_errors,
         crl_checked, crl_revoked, crl_unavailable, crl_expired, crl_errors)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15)
    )";

    try {
        std::vector<std::string> params = {
            std::to_string(totalProcessed), std::to_string(newlyExpired),
            std::to_string(newlyValid), std::to_string(unchanged),
            std::to_string(errors), std::to_string(durationMs),
            std::to_string(tcProcessed), std::to_string(tcNewlyValid),
            std::to_string(tcStillPending), std::to_string(tcErrors),
            std::to_string(crlChecked), std::to_string(crlRevoked),
            std::to_string(crlUnavailable), std::to_string(crlExpired),
            std::to_string(crlErrors)
        };

        int rowsAffected = queryExecutor_->executeCommand(query, params);
        if (rowsAffected > 0) {
            spdlog::info("[ValidationRepository] Saved extended revalidation history: "
                        "{} processed, TC({}/{}/{}), CRL({}/{}/{}/{})",
                        totalProcessed, tcProcessed, tcNewlyValid, tcStillPending,
                        crlChecked, crlRevoked, crlUnavailable, crlExpired);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] Failed to save extended revalidation history: {}", e.what());
        return false;
    }
}

Json::Value ValidationRepository::findDscsForTrustChainRevalidation() {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string falseVal = common::db::boolLiteral(dbType, false);

        // Oracle: RAWTOHEX+DBMS_LOB for BLOB, TO_CHAR for issuer_dn CLOB
        std::string certDataExpr = (dbType == "oracle")
            ? "RAWTOHEX(DBMS_LOB.SUBSTR(c.certificate_data, DBMS_LOB.GETLENGTH(c.certificate_data), 1)) as certificate_data"
            : "c.certificate_data";
        std::string issuerDnExpr = (dbType == "oracle")
            ? "TO_CHAR(c.issuer_dn) as issuer_dn"
            : "c.issuer_dn";

        std::string query =
            "SELECT vr.id, vr.certificate_id, vr.country_code, " + certDataExpr + ", " + issuerDnExpr + " "
            "FROM validation_result vr "
            "JOIN certificate c ON vr.certificate_id = c.id "
            "WHERE c.certificate_type = 'DSC' "
            "AND vr.csca_found = " + falseVal + " "
            "AND vr.validation_status IN ('PENDING', 'INVALID')";

        return queryExecutor_->executeQuery(query, {});
    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] findDscsForTrustChainRevalidation failed: {}", e.what());
        return Json::Value(Json::arrayValue);
    }
}

Json::Value ValidationRepository::findDscsForCrlRecheck() {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        std::string certDataExpr = (dbType == "oracle")
            ? "RAWTOHEX(DBMS_LOB.SUBSTR(c.certificate_data, DBMS_LOB.GETLENGTH(c.certificate_data), 1)) as certificate_data"
            : "c.certificate_data";

        std::string query =
            "SELECT vr.id, vr.certificate_id, vr.country_code, " + certDataExpr + " "
            "FROM validation_result vr "
            "JOIN certificate c ON vr.certificate_id = c.id "
            "WHERE c.certificate_type = 'DSC' "
            "AND vr.validation_status IN ('VALID', 'EXPIRED_VALID')";

        return queryExecutor_->executeQuery(query, {});
    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] findDscsForCrlRecheck failed: {}", e.what());
        return Json::Value(Json::arrayValue);
    }
}

bool ValidationRepository::updateTrustChainStatus(
    const std::string& id, const std::string& newStatus,
    bool cscaFound, const std::string& trustChainMessage
) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query = R"(
            UPDATE validation_result
            SET validation_status = $1, csca_found = $2, trust_chain_message = $3
            WHERE id = $4
        )";
        std::vector<std::string> params = {
            newStatus,
            common::db::boolLiteral(dbType, cscaFound),
            trustChainMessage,
            id
        };
        return queryExecutor_->executeCommand(query, params) > 0;
    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] updateTrustChainStatus failed for {}: {}", id, e.what());
        return false;
    }
}

bool ValidationRepository::updateCrlStatus(
    const std::string& id, const std::string& newStatus,
    const std::string& crlMessage
) {
    try {
        std::string query = R"(
            UPDATE validation_result
            SET validation_status = $1, trust_chain_message = COALESCE(trust_chain_message, '') || ' | CRL: ' || $2
            WHERE id = $3
        )";
        std::vector<std::string> params = { newStatus, crlMessage, id };
        return queryExecutor_->executeCommand(query, params) > 0;
    } catch (const std::exception& e) {
        spdlog::error("[ValidationRepository] updateCrlStatus failed for {}: {}", id, e.what());
        return false;
    }
}

} // namespace icao::relay::repositories

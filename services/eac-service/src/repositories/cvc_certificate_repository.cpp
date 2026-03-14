/**
 * @file cvc_certificate_repository.cpp
 * @brief CVC certificate repository implementation (PostgreSQL + Oracle)
 */

#include "repositories/cvc_certificate_repository.h"

#include "i_query_executor.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>

namespace eac::repositories {

CvcCertificateRepository::CvcCertificateRepository(common::IQueryExecutor* qe)
    : queryExecutor_(qe) {}

bool CvcCertificateRepository::save(const domain::CvcCertificateRecord& r) {
    try {
        std::string sql = R"(
            INSERT INTO cvc_certificate (
                cvc_type, country_code, car, chr,
                chat_oid, chat_role, chat_permissions,
                public_key_oid, public_key_algorithm,
                effective_date, expiration_date,
                fingerprint_sha256,
                signature_valid, validation_status, validation_message,
                source_type
            ) VALUES (
                $1, $2, $3, $4,
                $5, $6, $7,
                $8, $9,
                $10::date, $11::date,
                $12,
                $13, $14, $15,
                $16
            )
        )";

        queryExecutor_->executeCommand(sql, {
            r.cvcType, r.countryCode, r.car, r.chr,
            r.chatOid, r.chatRole, r.chatPermissions,
            r.publicKeyOid, r.publicKeyAlgorithm,
            r.effectiveDate, r.expirationDate,
            r.fingerprintSha256,
            common::db::boolLiteral(queryExecutor_->getDatabaseType(), r.signatureValid),
            r.validationStatus, r.validationMessage,
            r.sourceType.empty() ? "FILE_UPLOAD" : r.sourceType
        });
        return true;
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::save failed: {}", e.what());
        return false;
    }
}

bool CvcCertificateRepository::deleteById(const std::string& id) {
    try {
        queryExecutor_->executeCommand(
            "DELETE FROM cvc_certificate WHERE id = $1", {id});
        return true;
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::deleteById failed: {}", e.what());
        return false;
    }
}

std::optional<domain::CvcCertificateRecord> CvcCertificateRepository::findById(const std::string& id) {
    try {
        auto result = queryExecutor_->executeQuery(
            "SELECT * FROM cvc_certificate WHERE id = $1", {id});
        if (result.empty()) return std::nullopt;
        return rowToModel(result[0]);
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::findById failed: {}", e.what());
        return std::nullopt;
    }
}

std::optional<domain::CvcCertificateRecord> CvcCertificateRepository::findByFingerprint(const std::string& fp) {
    try {
        auto result = queryExecutor_->executeQuery(
            "SELECT * FROM cvc_certificate WHERE fingerprint_sha256 = $1", {fp});
        if (result.empty()) return std::nullopt;
        return rowToModel(result[0]);
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::findByFingerprint failed: {}", e.what());
        return std::nullopt;
    }
}

std::optional<domain::CvcCertificateRecord> CvcCertificateRepository::findByChr(const std::string& chr) {
    try {
        auto result = queryExecutor_->executeQuery(
            "SELECT * FROM cvc_certificate WHERE chr = $1 ORDER BY created_at DESC " +
            common::db::limitClause(queryExecutor_->getDatabaseType(), 1), {chr});
        if (result.empty()) return std::nullopt;
        return rowToModel(result[0]);
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::findByChr failed: {}", e.what());
        return std::nullopt;
    }
}

bool CvcCertificateRepository::existsByFingerprint(const std::string& fp) {
    try {
        auto result = queryExecutor_->executeScalar(
            "SELECT COUNT(*) FROM cvc_certificate WHERE fingerprint_sha256 = $1", {fp});
        return common::db::scalarToInt(result) > 0;
    } catch (...) {
        return false;
    }
}

Json::Value CvcCertificateRepository::findAll(const std::string& country, const std::string& type,
                                               const std::string& status, int page, int pageSize) {
    try {
        std::string sql = "SELECT * FROM cvc_certificate WHERE 1=1";
        std::vector<std::string> params;
        int paramIdx = 1;

        if (!country.empty()) {
            sql += " AND country_code = $" + std::to_string(paramIdx++);
            params.push_back(country);
        }
        if (!type.empty()) {
            sql += " AND cvc_type = $" + std::to_string(paramIdx++);
            params.push_back(type);
        }
        if (!status.empty()) {
            sql += " AND validation_status = $" + std::to_string(paramIdx++);
            params.push_back(status);
        }

        sql += " ORDER BY created_at DESC";
        sql += " " + common::db::paginationClause(queryExecutor_->getDatabaseType(), pageSize, (page - 1) * pageSize);

        return queryExecutor_->executeQuery(sql, params);
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::findAll failed: {}", e.what());
        return Json::Value(Json::arrayValue);
    }
}

int CvcCertificateRepository::countAll(const std::string& country, const std::string& type,
                                        const std::string& status) {
    try {
        std::string sql = "SELECT COUNT(*) FROM cvc_certificate WHERE 1=1";
        std::vector<std::string> params;
        int paramIdx = 1;

        if (!country.empty()) {
            sql += " AND country_code = $" + std::to_string(paramIdx++);
            params.push_back(country);
        }
        if (!type.empty()) {
            sql += " AND cvc_type = $" + std::to_string(paramIdx++);
            params.push_back(type);
        }
        if (!status.empty()) {
            sql += " AND validation_status = $" + std::to_string(paramIdx++);
            params.push_back(status);
        }

        return common::db::scalarToInt(queryExecutor_->executeScalar(sql, params));
    } catch (...) {
        return 0;
    }
}

Json::Value CvcCertificateRepository::getStatistics() {
    try {
        Json::Value stats;

        // Total count
        stats["total"] = common::db::scalarToInt(
            queryExecutor_->executeScalar("SELECT COUNT(*) FROM cvc_certificate"));

        // By type
        auto byType = queryExecutor_->executeQuery(
            "SELECT cvc_type, COUNT(*) as cnt FROM cvc_certificate GROUP BY cvc_type");
        Json::Value typeMap(Json::objectValue);
        for (const auto& row : byType) {
            typeMap[row["cvc_type"].asString()] = common::db::scalarToInt(row["cnt"]);
        }
        stats["byType"] = typeMap;

        // By country
        stats["byCountry"] = queryExecutor_->executeQuery(
            "SELECT country_code, COUNT(*) as cnt FROM cvc_certificate "
            "GROUP BY country_code ORDER BY cnt DESC");

        // Valid/Expired counts
        stats["validCount"] = common::db::scalarToInt(
            queryExecutor_->executeScalar(
                "SELECT COUNT(*) FROM cvc_certificate WHERE validation_status = 'VALID'"));
        stats["expiredCount"] = common::db::scalarToInt(
            queryExecutor_->executeScalar(
                "SELECT COUNT(*) FROM cvc_certificate WHERE validation_status = 'EXPIRED'"));

        return stats;
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::getStatistics failed: {}", e.what());
        return Json::Value();
    }
}

Json::Value CvcCertificateRepository::getCountryList() {
    try {
        return queryExecutor_->executeQuery(
            "SELECT DISTINCT country_code FROM cvc_certificate ORDER BY country_code");
    } catch (...) {
        return Json::Value(Json::arrayValue);
    }
}

std::vector<domain::CvcCertificateRecord> CvcCertificateRepository::findByCar(const std::string& car) {
    std::vector<domain::CvcCertificateRecord> results;
    try {
        auto rows = queryExecutor_->executeQuery(
            "SELECT * FROM cvc_certificate WHERE chr = $1 ORDER BY created_at DESC", {car});
        for (const auto& row : rows) {
            results.push_back(rowToModel(row));
        }
    } catch (const std::exception& e) {
        spdlog::error("CvcCertificateRepository::findByCar failed: {}", e.what());
    }
    return results;
}

domain::CvcCertificateRecord CvcCertificateRepository::rowToModel(const Json::Value& row) const {
    domain::CvcCertificateRecord r;
    r.id = row["id"].asString();
    r.uploadId = row.get("upload_id", "").asString();
    r.cvcType = row["cvc_type"].asString();
    r.countryCode = row["country_code"].asString();
    r.car = row["car"].asString();
    r.chr = row["chr"].asString();
    r.chatOid = row.get("chat_oid", "").asString();
    r.chatRole = row.get("chat_role", "").asString();
    r.chatPermissions = row.get("chat_permissions", "").asString();
    r.publicKeyOid = row.get("public_key_oid", "").asString();
    r.publicKeyAlgorithm = row.get("public_key_algorithm", "").asString();
    r.effectiveDate = row.get("effective_date", "").asString();
    r.expirationDate = row.get("expiration_date", "").asString();
    r.fingerprintSha256 = row["fingerprint_sha256"].asString();
    r.signatureValid = common::db::getBool(row, "signature_valid", false);
    r.validationStatus = row.get("validation_status", "PENDING").asString();
    r.validationMessage = row.get("validation_message", "").asString();
    r.issuerCvcId = row.get("issuer_cvc_id", "").asString();
    r.sourceType = row.get("source_type", "").asString();
    r.createdAt = row.get("created_at", "").asString();
    r.updatedAt = row.get("updated_at", "").asString();
    return r;
}

} // namespace eac::repositories

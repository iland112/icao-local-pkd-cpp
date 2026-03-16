#include "csr_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>

namespace repositories {

CsrRepository::CsrRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
}

std::string CsrRepository::save(
    const std::string& subjectDn,
    const std::string& countryCode,
    const std::string& organization,
    const std::string& commonName,
    const std::string& keyAlgorithm,
    const std::string& signatureAlgorithm,
    const std::string& csrPem,
    const std::vector<uint8_t>& csrDer,
    const std::string& publicKeyFingerprint,
    const std::string& privateKeyEncrypted,
    const std::string& memo,
    const std::string& createdBy)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Convert DER to hex string for DB storage
        std::string derHex;
        derHex.reserve(csrDer.size() * 2);
        static const char hexChars[] = "0123456789abcdef";
        for (uint8_t byte : csrDer) {
            derHex += hexChars[byte >> 4];
            derHex += hexChars[byte & 0x0F];
        }

        if (dbType == "oracle") {
            std::string query =
                "INSERT INTO csr_request "
                "(id, subject_dn, country_code, organization, common_name, "
                " key_algorithm, signature_algorithm, csr_pem, csr_der, "
                " public_key_fingerprint, private_key_encrypted, memo, created_by) "
                "VALUES (SYS_GUID(), $1, $2, $3, $4, $5, $6, $7, HEXTORAW($8), $9, $10, $11, $12)";

            std::vector<std::string> params = {
                subjectDn, countryCode, organization, commonName,
                keyAlgorithm, signatureAlgorithm, csrPem, derHex,
                publicKeyFingerprint, privateKeyEncrypted, memo, createdBy
            };

            queryExecutor_->executeCommand(query, params);

            // Retrieve generated ID
            std::string selectQuery =
                "SELECT id FROM csr_request WHERE public_key_fingerprint = $1 "
                "ORDER BY created_at DESC FETCH FIRST 1 ROWS ONLY";
            Json::Value result = queryExecutor_->executeQuery(selectQuery, {publicKeyFingerprint});
            if (!result.empty()) {
                return result[0].get("id", "").asString();
            }
            return "";
        } else {
            std::string query =
                "INSERT INTO csr_request "
                "(subject_dn, country_code, organization, common_name, "
                " key_algorithm, signature_algorithm, csr_pem, csr_der, "
                " public_key_fingerprint, private_key_encrypted, memo, created_by) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, decode($8, 'hex'), $9, $10, $11, $12) "
                "RETURNING id";

            std::vector<std::string> params = {
                subjectDn, countryCode, organization, commonName,
                keyAlgorithm, signatureAlgorithm, csrPem, derHex,
                publicKeyFingerprint, privateKeyEncrypted, memo, createdBy
            };

            Json::Value result = queryExecutor_->executeQuery(query, params);
            if (!result.empty()) {
                return result[0].get("id", "").asString();
            }
            return "";
        }
    } catch (const std::exception& e) {
        spdlog::error("[CsrRepository] save failed: {}", e.what());
        return "";
    }
}

Json::Value CsrRepository::findById(const std::string& id)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            query =
                "SELECT id, subject_dn, country_code, organization, common_name, "
                "  key_algorithm, signature_algorithm, TO_CHAR(csr_pem) as csr_pem, "
                "  RAWTOHEX(DBMS_LOB.SUBSTR(csr_der, DBMS_LOB.GETLENGTH(csr_der), 1)) as csr_der_hex, "
                "  public_key_fingerprint, status, memo, "
                "  TO_CHAR(issued_certificate_pem) as issued_certificate_pem, "
                "  certificate_serial, certificate_subject_dn, certificate_issuer_dn, "
                "  TO_CHAR(certificate_not_before, 'YYYY-MM-DD HH24:MI:SS') as certificate_not_before, "
                "  TO_CHAR(certificate_not_after, 'YYYY-MM-DD HH24:MI:SS') as certificate_not_after, "
                "  certificate_fingerprint, "
                "  TO_CHAR(issued_at, 'YYYY-MM-DD HH24:MI:SS') as issued_at, registered_by, "
                "  created_by, TO_CHAR(created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at, "
                "  TO_CHAR(updated_at, 'YYYY-MM-DD HH24:MI:SS') as updated_at "
                "FROM csr_request WHERE id = $1";
        } else {
            query =
                "SELECT id, subject_dn, country_code, organization, common_name, "
                "  key_algorithm, signature_algorithm, csr_pem, "
                "  encode(csr_der, 'hex') as csr_der_hex, "
                "  public_key_fingerprint, status, memo, "
                "  issued_certificate_pem, "
                "  certificate_serial, certificate_subject_dn, certificate_issuer_dn, "
                "  certificate_not_before, certificate_not_after, "
                "  certificate_fingerprint, issued_at, registered_by, "
                "  created_by, created_at, updated_at "
                "FROM csr_request WHERE id = $1";
        }

        Json::Value result = queryExecutor_->executeQuery(query, {id});
        if (result.empty()) {
            return Json::nullValue;
        }

        return result[0];
    } catch (const std::exception& e) {
        spdlog::error("[CsrRepository] findById failed: {}", e.what());
        return Json::nullValue;
    }
}

Json::Value CsrRepository::findAll(int limit, int offset, const std::string& statusFilter)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string whereClause;
        std::vector<std::string> params;

        if (!statusFilter.empty()) {
            whereClause = "WHERE status = $1";
            params.push_back(statusFilter);
        }

        std::string query;
        if (dbType == "oracle") {
            query =
                "SELECT id, subject_dn, country_code, organization, common_name, "
                "  key_algorithm, signature_algorithm, public_key_fingerprint, status, memo, "
                "  created_by, TO_CHAR(created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at, "
                "  TO_CHAR(updated_at, 'YYYY-MM-DD HH24:MI:SS') as updated_at "
                "FROM csr_request " + whereClause +
                " ORDER BY created_at DESC "
                "OFFSET " + std::to_string(offset) + " ROWS FETCH NEXT " + std::to_string(limit) + " ROWS ONLY";
        } else {
            query =
                "SELECT id, subject_dn, country_code, organization, common_name, "
                "  key_algorithm, signature_algorithm, public_key_fingerprint, status, memo, "
                "  created_by, created_at, updated_at "
                "FROM csr_request " + whereClause +
                " ORDER BY created_at DESC "
                "LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
        }

        return queryExecutor_->executeQuery(query, params);
    } catch (const std::exception& e) {
        spdlog::error("[CsrRepository] findAll failed: {}", e.what());
        return Json::arrayValue;
    }
}

int CsrRepository::countAll(const std::string& statusFilter)
{
    try {
        std::string whereClause;
        std::vector<std::string> params;

        if (!statusFilter.empty()) {
            whereClause = "WHERE status = $1";
            params.push_back(statusFilter);
        }

        std::string query = "SELECT COUNT(*) FROM csr_request " + whereClause;
        Json::Value result = queryExecutor_->executeScalar(query, params);
        return common::db::scalarToInt(result);
    } catch (const std::exception& e) {
        spdlog::error("[CsrRepository] countAll failed: {}", e.what());
        return 0;
    }
}

bool CsrRepository::updateStatus(const std::string& id, const std::string& status)
{
    try {
        std::string query =
            "UPDATE csr_request SET status = $1 WHERE id = $2";
        int affected = queryExecutor_->executeCommand(query, {status, id});
        return affected > 0;
    } catch (const std::exception& e) {
        spdlog::error("[CsrRepository] updateStatus failed: {}", e.what());
        return false;
    }
}

bool CsrRepository::registerCertificate(
    const std::string& id,
    const std::string& certPemEncrypted,
    const std::vector<uint8_t>& certDer,
    const std::string& serial,
    const std::string& subjectDn,
    const std::string& issuerDn,
    const std::string& notBefore,
    const std::string& notAfter,
    const std::string& fingerprint,
    const std::string& registeredBy)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Convert DER to hex
        std::string derHex;
        derHex.reserve(certDer.size() * 2);
        static const char hexChars[] = "0123456789abcdef";
        for (uint8_t byte : certDer) {
            derHex += hexChars[byte >> 4];
            derHex += hexChars[byte & 0x0F];
        }

        std::string query;
        if (dbType == "oracle") {
            query =
                "UPDATE csr_request SET "
                "  issued_certificate_pem = $1, "
                "  issued_certificate_der = HEXTORAW($2), "
                "  certificate_serial = $3, "
                "  certificate_subject_dn = $4, "
                "  certificate_issuer_dn = $5, "
                "  certificate_not_before = TO_TIMESTAMP($6, 'YYYY-MM-DD HH24:MI:SS'), "
                "  certificate_not_after = TO_TIMESTAMP($7, 'YYYY-MM-DD HH24:MI:SS'), "
                "  certificate_fingerprint = $8, "
                "  issued_at = SYSTIMESTAMP, "
                "  registered_by = $9, "
                "  status = 'ISSUED' "
                "WHERE id = $10";
        } else {
            query =
                "UPDATE csr_request SET "
                "  issued_certificate_pem = $1, "
                "  issued_certificate_der = decode($2, 'hex'), "
                "  certificate_serial = $3, "
                "  certificate_subject_dn = $4, "
                "  certificate_issuer_dn = $5, "
                "  certificate_not_before = $6::timestamptz, "
                "  certificate_not_after = $7::timestamptz, "
                "  certificate_fingerprint = $8, "
                "  issued_at = CURRENT_TIMESTAMP, "
                "  registered_by = $9, "
                "  status = 'ISSUED' "
                "WHERE id = $10";
        }

        std::vector<std::string> params = {
            certPemEncrypted, derHex, serial, subjectDn, issuerDn,
            notBefore, notAfter, fingerprint, registeredBy, id
        };

        int affected = queryExecutor_->executeCommand(query, params);
        return affected > 0;
    } catch (const std::exception& e) {
        spdlog::error("[CsrRepository] registerCertificate failed: {}", e.what());
        return false;
    }
}

bool CsrRepository::deleteById(const std::string& id)
{
    try {
        std::string query = "DELETE FROM csr_request WHERE id = $1";
        int affected = queryExecutor_->executeCommand(query, {id});
        return affected > 0;
    } catch (const std::exception& e) {
        spdlog::error("[CsrRepository] deleteById failed: {}", e.what());
        return false;
    }
}

} // namespace repositories

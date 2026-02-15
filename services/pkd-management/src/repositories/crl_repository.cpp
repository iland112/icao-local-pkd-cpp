/** @file crl_repository.cpp
 *  @brief CrlRepository implementation
 */

#include "crl_repository.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <stdexcept>

namespace repositories {

CrlRepository::CrlRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("CrlRepository: queryExecutor cannot be nullptr");
    }
    spdlog::debug("[CrlRepository] Initialized (DB type: {})", queryExecutor_->getDatabaseType());
}

std::string CrlRepository::save(const std::string& uploadId,
                                 const std::string& countryCode,
                                 const std::string& issuerDn,
                                 const std::string& thisUpdate,
                                 const std::string& nextUpdate,
                                 const std::string& crlNumber,
                                 const std::string& fingerprint,
                                 const std::vector<uint8_t>& crlBinary) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Generate UUID (C++ for all DB types)
        std::string crlId = generateUuid();

        // Convert binary CRL to hex string
        // PostgreSQL: \x for hex bytea; Oracle: \\x as BLOB marker for OracleQueryExecutor
        std::ostringstream hexStream;
        hexStream << (dbType == "oracle" ? "\\\\x" : "\\x");
        for (size_t i = 0; i < crlBinary.size(); i++) {
            hexStream << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(crlBinary[i]);
        }
        std::string crlDataHex = hexStream.str();

        // Database-specific INSERT query
        std::string query;
        std::vector<std::string> params;

        if (dbType == "oracle") {
            // Oracle schema columns: country_code, crl_binary, fingerprint_sha256
            query =
                "INSERT INTO crl (id, upload_id, country_code, issuer_dn, "
                "this_update, next_update, crl_number, fingerprint_sha256, "
                "crl_binary) VALUES ("
                "$1, $2, $3, $4, "
                "TO_TIMESTAMP($5, 'YYYY-MM-DD HH24:MI:SS'), "
                "CASE WHEN $6 IS NULL OR $6 = '' THEN NULL ELSE TO_TIMESTAMP($6, 'YYYY-MM-DD HH24:MI:SS') END, "
                "$7, $8, $9)";

            // Strip timezone suffix (+00) from date strings for Oracle TO_TIMESTAMP
            auto stripTz = [](const std::string& ts) -> std::string {
                if (ts.empty()) return "";
                return ts.length() > 19 ? ts.substr(0, 19) : ts;
            };

            params = {
                crlId, uploadId, countryCode, issuerDn,
                stripTz(thisUpdate),
                nextUpdate.empty() ? "" : stripTz(nextUpdate),
                crlNumber, fingerprint, crlDataHex
            };
        } else {
            // PostgreSQL schema: country_code, fingerprint_sha256, crl_binary, validation_status
            query =
                "INSERT INTO crl (id, upload_id, country_code, issuer_dn, "
                "this_update, next_update, crl_number, fingerprint_sha256, "
                "crl_binary, validation_status, created_at) VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, 'PENDING', NOW()) "
                "ON CONFLICT DO NOTHING";

            params = {
                crlId, uploadId, countryCode, issuerDn,
                thisUpdate,
                nextUpdate.empty() ? "" : nextUpdate,
                crlNumber, fingerprint, crlDataHex
            };
        }

        queryExecutor_->executeCommand(query, params);
        return crlId;
    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] save failed: {}", e.what());
        return "";
    }
}

void CrlRepository::saveRevokedCertificate(const std::string& crlId,
                                            const std::string& serialNumber,
                                            const std::string& revocationDate,
                                            const std::string& reason) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // revoked_certificate table only exists in PostgreSQL schema
        if (dbType == "oracle") {
            spdlog::debug("[CrlRepository] saveRevokedCertificate skipped - table not available in Oracle schema");
            return;
        }

        std::string id = generateUuid();
        std::string query =
            "INSERT INTO revoked_certificate (id, crl_id, serial_number, "
            "revocation_date, revocation_reason, created_at) VALUES ("
            "$1, $2, $3, $4, $5, NOW())";

        std::vector<std::string> params = {
            id, crlId, serialNumber, revocationDate, reason
        };

        queryExecutor_->executeCommand(query, params);
    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] saveRevokedCertificate failed: {}", e.what());
    }
}

void CrlRepository::updateLdapStatus(const std::string& crlId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string storedFlag = (dbType == "oracle") ? "1" : "TRUE";

        std::string query;
        if (dbType == "oracle") {
            // Oracle CRL table has no stored_at column
            query = "UPDATE crl SET "
                    "ldap_dn = $1, stored_in_ldap = " + storedFlag + " "
                    "WHERE id = $2";
        } else {
            query = "UPDATE crl SET "
                    "ldap_dn = $1, stored_in_ldap = " + storedFlag + ", stored_at = NOW() "
                    "WHERE id = $2";
        }

        queryExecutor_->executeCommand(query, {ldapDn, crlId});
    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] updateLdapStatus failed: {}", e.what());
    }
}

std::string CrlRepository::generateUuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);

    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (ab >> 32) << '-';
    ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << '-';
    ss << std::setw(4) << (ab & 0xFFFF) << '-';
    ss << std::setw(4) << (cd >> 48) << '-';
    ss << std::setw(12) << (cd & 0x0000FFFFFFFFFFFFULL);

    return ss.str();
}

// --- CRL Lookup by Country ---

Json::Value CrlRepository::findByCountryCode(const std::string& countryCode) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string storedFlag = (dbType == "oracle") ? "1" : "TRUE";

        // Get the most recent CRL for the country (by this_update descending)
        std::string query;
        if (dbType == "oracle") {
            query =
                "SELECT crl_binary, this_update, next_update "
                "FROM crl WHERE country_code = $1 AND stored_in_ldap = " + storedFlag + " "
                "ORDER BY this_update DESC FETCH FIRST 1 ROWS ONLY";
        } else {
            query =
                "SELECT crl_binary, this_update, next_update "
                "FROM crl WHERE country_code = $1 AND stored_in_ldap = " + storedFlag + " "
                "ORDER BY this_update DESC LIMIT 1";
        }

        Json::Value results = queryExecutor_->executeQuery(query, {countryCode});
        if (results.isArray() && results.size() > 0) {
            return results[0];
        }
        return Json::nullValue;
    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] findByCountryCode failed for {}: {}", countryCode, e.what());
        return Json::nullValue;
    }
}

// --- Bulk Export (All LDAP-stored CRLs) ---

Json::Value CrlRepository::findAllForExport() {
    std::string dbType = queryExecutor_->getDatabaseType();
    std::string storedFlag = (dbType == "oracle") ? "1" : "TRUE";

    std::string query =
        "SELECT country_code, issuer_dn, crl_binary, fingerprint_sha256 "
        "FROM crl WHERE stored_in_ldap = " + storedFlag + " "
        "ORDER BY country_code";

    return queryExecutor_->executeQuery(query);
}

} // namespace repositories

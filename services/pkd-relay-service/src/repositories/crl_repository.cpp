#include "crl_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <cstring>

namespace icao::relay::repositories {

CrlRepository::CrlRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("CrlRepository: queryExecutor cannot be nullptr");
    }

    spdlog::debug("[CrlRepository] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

int CrlRepository::countAll() {
    try {
        const char* query = "SELECT COUNT(*) FROM crl";

        Json::Value result = queryExecutor_->executeScalar(query);
        if (result.isInt()) return result.asInt();
        if (result.isString()) {
            try { return std::stoi(result.asString()); }
            catch (...) { return 0; }
        }
        return result.asInt();

    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] Exception in countAll(): {}", e.what());
        return 0;
    }
}

std::vector<domain::Crl> CrlRepository::findNotInLdap(int limit) {
    std::vector<domain::Crl> results;

    try {
        const char* query =
            "SELECT id, fingerprint_sha256, issuer_dn, country_code, "
            "this_update, next_update, stored_in_ldap, crl_data "
            "FROM crl "
            "WHERE stored_in_ldap = FALSE "
            "ORDER BY this_update ASC "
            "LIMIT $1";

        std::vector<std::string> params = {std::to_string(limit)};

        Json::Value result = queryExecutor_->executeQuery(query, params);

        for (const auto& row : result) {
            results.push_back(jsonToCrl(row));
        }

        spdlog::debug("[CrlRepository] Found {} CRLs not in LDAP", results.size());
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] Exception in findNotInLdap(): {}", e.what());
        return results;
    }
}

int CrlRepository::markStoredInLdap(const std::vector<std::string>& fingerprints) {
    if (fingerprints.empty()) {
        return 0;
    }

    try {
        // Build parameterized query with IN clause
        // UPDATE crl SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN ($1, $2, ...)
        std::ostringstream queryBuilder;
        queryBuilder << "UPDATE crl SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN (";

        for (size_t i = 0; i < fingerprints.size(); i++) {
            if (i > 0) {
                queryBuilder << ", ";
            }
            queryBuilder << "$" << (i + 1);
        }
        queryBuilder << ")";

        std::string query = queryBuilder.str();

        // Convert fingerprints to std::vector<std::string> for Query Executor
        std::vector<std::string> params(fingerprints.begin(), fingerprints.end());

        int count = queryExecutor_->executeCommand(query, params);

        spdlog::debug("[CrlRepository] Marked {} CRLs as stored in LDAP", count);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] Exception in markStoredInLdap(batch): {}", e.what());
        return 0;
    }
}

bool CrlRepository::markStoredInLdap(const std::string& fingerprint) {
    try {
        const char* query =
            "UPDATE crl SET stored_in_ldap = TRUE WHERE fingerprint_sha256 = $1";

        std::vector<std::string> params = {fingerprint};

        int rowsAffected = queryExecutor_->executeCommand(query, params);
        return (rowsAffected > 0);

    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] Exception in markStoredInLdap(single): {}", e.what());
        return false;
    }
}

domain::Crl CrlRepository::jsonToCrl(const Json::Value& row) {
    // Parse string fields
    std::string id = row["id"].asString();
    std::string fingerprintSha256 = row["fingerprint_sha256"].asString();
    std::string issuerDn = row["issuer_dn"].asString();
    std::string countryCode = row["country_code"].asString();

    // Parse timestamps
    std::string thisUpdateStr = row["this_update"].asString();
    std::string nextUpdateStr = row["next_update"].asString();

    std::tm tm = {};
    auto thisUpdate = std::chrono::system_clock::now();
    if (strptime(thisUpdateStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        thisUpdate = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    tm = {};
    auto nextUpdate = std::chrono::system_clock::now();
    if (strptime(nextUpdateStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        nextUpdate = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    // Parse boolean field
    bool storedInLdap = false;
    if (row["stored_in_ldap"].isBool()) {
        storedInLdap = row["stored_in_ldap"].asBool();
    } else if (row["stored_in_ldap"].isString()) {
        std::string val = row["stored_in_ldap"].asString();
        storedInLdap = (val == "t" || val == "true" || val == "1");
    }

    // Parse binary CRL data (bytea format: \x followed by hex)
    std::vector<unsigned char> crlData;
    if (!row["crl_data"].isNull() && row["crl_data"].isString()) {
        std::string crlDataHex = row["crl_data"].asString();
        if (crlDataHex.length() > 2 && crlDataHex[0] == '\\' && crlDataHex[1] == 'x') {
            // Skip '\x' prefix and parse hex
            const char* hexData = crlDataHex.c_str() + 2;
            size_t hexLen = crlDataHex.length() - 2;
            crlData.reserve(hexLen / 2);

            for (size_t i = 0; i < hexLen; i += 2) {
                char byteStr[3] = {hexData[i], hexData[i + 1], '\0'};
                unsigned char byte = static_cast<unsigned char>(std::strtol(byteStr, nullptr, 16));
                crlData.push_back(byte);
            }
        }
    }

    // Construct and return domain object
    return domain::Crl(
        id,
        fingerprintSha256,
        issuerDn,
        countryCode,
        thisUpdate,
        nextUpdate,
        storedInLdap,
        crlData
    );
}

} // namespace icao::relay::repositories

#include "crl_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <cstring>

namespace icao::relay::repositories {

CrlRepository::CrlRepository(std::shared_ptr<common::IDbConnectionPool> dbPool)
    : dbPool_(dbPool) {
}

int CrlRepository::countAll() {
    try {
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CrlRepository] Failed to acquire database connection");
            return 0;
        }

        const char* query = "SELECT COUNT(*) FROM crl";

        PGresult* res = PQexec(conn.get(), query);

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn.get());
            PQclear(res);
            spdlog::error("[CrlRepository] Failed to count CRLs: {}", error);
            return 0;
        }

        int count = 0;
        if (PQntuples(res) > 0) {
            count = std::atoi(PQgetvalue(res, 0, 0));
        }

        PQclear(res);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] Exception in countAll(): {}", e.what());
        return 0;
    }
}

std::vector<domain::Crl> CrlRepository::findNotInLdap(int limit) {
    std::vector<domain::Crl> results;

    try {
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CrlRepository] Failed to acquire database connection");
            return results;
        }

        const char* query =
            "SELECT id, fingerprint_sha256, issuer_dn, country_code, "
            "this_update, next_update, stored_in_ldap, crl_data "
            "FROM crl "
            "WHERE stored_in_ldap = FALSE "
            "ORDER BY this_update ASC "
            "LIMIT $1";

        std::string limitStr = std::to_string(limit);

        const char* paramValues[1] = {
            limitStr.c_str()
        };

        PGresult* res = PQexecParams(
            conn.get(), query, 1, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn.get());
            PQclear(res);
            spdlog::error("[CrlRepository] Failed to find CRLs not in LDAP: {}", error);
            return results;
        }

        int rowCount = PQntuples(res);
        for (int i = 0; i < rowCount; i++) {
            results.push_back(resultToCrl(res, i));
        }

        PQclear(res);
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
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CrlRepository] Failed to acquire database connection");
            return 0;
        }

        // Build parameterized query with IN clause
        // UPDATE crl SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN ($1, $2, ...)
        std::ostringstream queryBuilder;
        queryBuilder << "UPDATE crl SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN (";

        std::vector<const char*> paramValues;
        for (size_t i = 0; i < fingerprints.size(); i++) {
            if (i > 0) {
                queryBuilder << ", ";
            }
            queryBuilder << "$" << (i + 1);
            paramValues.push_back(fingerprints[i].c_str());
        }
        queryBuilder << ")";

        std::string query = queryBuilder.str();

        PGresult* res = PQexecParams(
            conn.get(), query.c_str(),
            paramValues.size(), nullptr,
            paramValues.data(),
            nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string error = PQerrorMessage(conn.get());
            PQclear(res);
            spdlog::error("[CrlRepository] Failed to mark CRLs stored in LDAP: {}", error);
            return 0;
        }

        char* rowsAffected = PQcmdTuples(res);
        int count = std::atoi(rowsAffected);

        PQclear(res);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] Exception in markStoredInLdap(batch): {}", e.what());
        return 0;
    }
}

bool CrlRepository::markStoredInLdap(const std::string& fingerprint) {
    try {
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CrlRepository] Failed to acquire database connection");
            return false;
        }

        const char* query =
            "UPDATE crl SET stored_in_ldap = TRUE WHERE fingerprint_sha256 = $1";

        const char* paramValues[1] = {
            fingerprint.c_str()
        };

        PGresult* res = PQexecParams(
            conn.get(), query, 1, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string error = PQerrorMessage(conn.get());
            PQclear(res);
            spdlog::error("[CrlRepository] Failed to mark CRL stored in LDAP: {}", error);
            return false;
        }

        char* rowsAffected = PQcmdTuples(res);
        bool success = (std::atoi(rowsAffected) > 0);

        PQclear(res);
        return success;

    } catch (const std::exception& e) {
        spdlog::error("[CrlRepository] Exception in markStoredInLdap(single): {}", e.what());
        return false;
    }
}

domain::Crl CrlRepository::resultToCrl(PGresult* res, int row) {
    // Parse all fields from result set
    std::string id = PQgetvalue(res, row, 0);
    std::string fingerprintSha256 = PQgetvalue(res, row, 1);
    std::string issuerDn = PQgetvalue(res, row, 2);
    std::string countryCode = PQgetvalue(res, row, 3);

    // Parse timestamps
    const char* thisUpdateStr = PQgetvalue(res, row, 4);
    const char* nextUpdateStr = PQgetvalue(res, row, 5);

    std::tm tm = {};
    auto thisUpdate = std::chrono::system_clock::now();
    if (strptime(thisUpdateStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        thisUpdate = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    tm = {};
    auto nextUpdate = std::chrono::system_clock::now();
    if (strptime(nextUpdateStr, "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        nextUpdate = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    bool storedInLdap = (strcmp(PQgetvalue(res, row, 6), "t") == 0);

    // Parse binary CRL data (bytea format: \x followed by hex)
    std::vector<unsigned char> crlData;
    const char* crlDataHex = PQgetvalue(res, row, 7);
    if (crlDataHex && strlen(crlDataHex) > 2 && crlDataHex[0] == '\\' && crlDataHex[1] == 'x') {
        // Skip '\x' prefix and parse hex
        const char* hexData = crlDataHex + 2;
        size_t hexLen = strlen(hexData);
        crlData.reserve(hexLen / 2);

        for (size_t i = 0; i < hexLen; i += 2) {
            char byteStr[3] = {hexData[i], hexData[i + 1], '\0'};
            unsigned char byte = static_cast<unsigned char>(std::strtol(byteStr, nullptr, 16));
            crlData.push_back(byte);
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

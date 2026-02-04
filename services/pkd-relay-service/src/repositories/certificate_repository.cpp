#include "certificate_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace icao::relay::repositories {

CertificateRepository::CertificateRepository(std::shared_ptr<common::DbConnectionPool> dbPool)
    : dbPool_(dbPool) {
}

int CertificateRepository::countByType(const std::string& certificateType) {
    try {
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CertificateRepository] Failed to acquire database connection");
            return 0;
        }

        const char* query = "SELECT COUNT(*) FROM certificate WHERE certificate_type = $1";

        const char* paramValues[1] = {
            certificateType.c_str()
        };

        PGresult* res = PQexecParams(
            conn.get(), query, 1, nullptr,
            paramValues, nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn.get());
            PQclear(res);
            spdlog::error("[CertificateRepository] Failed to count by type '{}': {}",
                          certificateType, error);
            return 0;
        }

        int count = 0;
        if (PQntuples(res) > 0) {
            count = std::atoi(PQgetvalue(res, 0, 0));
        }

        PQclear(res);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in countByType(): {}", e.what());
        return 0;
    }
}

std::vector<domain::Certificate> CertificateRepository::findNotInLdap(
    const std::string& certificateType,
    int limit
) {
    std::vector<domain::Certificate> results;

    try {
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CertificateRepository] Failed to acquire database connection");
            return results;
        }

        std::string query =
            "SELECT id, fingerprint_sha256, certificate_type, country_code, "
            "subject_dn, issuer_dn, stored_in_ldap "
            "FROM certificate "
            "WHERE stored_in_ldap = FALSE";

        std::vector<const char*> paramValues;
        int paramIndex = 1;

        // Add certificate type filter if provided
        if (!certificateType.empty()) {
            query += " AND certificate_type = $" + std::to_string(paramIndex++);
            paramValues.push_back(certificateType.c_str());
        }

        // Add ORDER BY and LIMIT
        query += " ORDER BY created_at ASC LIMIT $" + std::to_string(paramIndex);

        std::string limitStr = std::to_string(limit);
        paramValues.push_back(limitStr.c_str());

        PGresult* res = PQexecParams(
            conn.get(), query.c_str(),
            paramValues.size(), nullptr,
            paramValues.data(),
            nullptr, nullptr, 0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string error = PQerrorMessage(conn.get());
            PQclear(res);
            spdlog::error("[CertificateRepository] Failed to find not in LDAP: {}", error);
            return results;
        }

        int rowCount = PQntuples(res);
        for (int i = 0; i < rowCount; i++) {
            results.push_back(resultToCertificate(res, i));
        }

        PQclear(res);
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in findNotInLdap(): {}", e.what());
        return results;
    }
}

int CertificateRepository::markStoredInLdap(const std::vector<std::string>& fingerprints) {
    if (fingerprints.empty()) {
        return 0;
    }

    try {
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CertificateRepository] Failed to acquire database connection");
            return 0;
        }

        // Build parameterized query with IN clause
        // UPDATE certificate SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN ($1, $2, ...)
        std::ostringstream queryBuilder;
        queryBuilder << "UPDATE certificate SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN (";

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
            spdlog::error("[CertificateRepository] Failed to mark stored in LDAP: {}", error);
            return 0;
        }

        char* rowsAffected = PQcmdTuples(res);
        int count = std::atoi(rowsAffected);

        PQclear(res);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in markStoredInLdap(batch): {}", e.what());
        return 0;
    }
}

bool CertificateRepository::markStoredInLdap(const std::string& fingerprint) {
    try {
        auto conn = dbPool_->acquire();
        if (!conn.isValid()) {
            spdlog::error("[CertificateRepository] Failed to acquire database connection");
            return false;
        }

        const char* query =
            "UPDATE certificate SET stored_in_ldap = TRUE WHERE fingerprint_sha256 = $1";

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
            spdlog::error("[CertificateRepository] Failed to mark stored in LDAP: {}", error);
            return false;
        }

        char* rowsAffected = PQcmdTuples(res);
        bool success = (std::atoi(rowsAffected) > 0);

        PQclear(res);
        return success;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in markStoredInLdap(single): {}", e.what());
        return false;
    }
}

domain::Certificate CertificateRepository::resultToCertificate(PGresult* res, int row) {
    // Parse all fields from result set
    std::string id = PQgetvalue(res, row, 0);
    std::string fingerprintSha256 = PQgetvalue(res, row, 1);
    std::string certificateType = PQgetvalue(res, row, 2);
    std::string countryCode = PQgetvalue(res, row, 3);
    std::string subjectDn = PQgetvalue(res, row, 4);
    std::string issuerDn = PQgetvalue(res, row, 5);
    bool storedInLdap = (strcmp(PQgetvalue(res, row, 6), "t") == 0);

    // Construct and return domain object
    return domain::Certificate(
        id,
        fingerprintSha256,
        certificateType,
        countryCode,
        subjectDn,
        issuerDn,
        storedInLdap
    );
}

} // namespace icao::relay::repositories

#include "certificate_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace repositories {

CertificateRepository::CertificateRepository(PGconn* dbConn)
    : dbConn_(dbConn)
{
    if (!dbConn_) {
        throw std::invalid_argument("CertificateRepository: dbConn cannot be nullptr");
    }
    spdlog::debug("[CertificateRepository] Initialized");
}

// ========================================================================
// Search Operations
// ========================================================================

Json::Value CertificateRepository::search(const CertificateSearchFilter& filter)
{
    spdlog::debug("[CertificateRepository] Searching certificates");

    // TODO: Implement certificate search with dynamic WHERE clause
    spdlog::warn("[CertificateRepository] search - TODO: Implement");

    Json::Value response = Json::arrayValue;
    return response;
}

Json::Value CertificateRepository::findByFingerprint(const std::string& fingerprint)
{
    spdlog::debug("[CertificateRepository] Finding by fingerprint: {}...",
        fingerprint.substr(0, 16));

    try {
        const char* query =
            "SELECT id, certificate_type, country_code, subject_dn, issuer_dn, "
            "fingerprint_sha256, serial_number, valid_from, valid_to, "
            "stored_in_ldap, created_at "
            "FROM certificate WHERE fingerprint_sha256 = $1";

        std::vector<std::string> params = {fingerprint};
        PGresult* res = executeParamQuery(query, params);

        if (PQntuples(res) == 0) {
            PQclear(res);
            return Json::nullValue;
        }

        Json::Value certs = pgResultToJson(res);
        PQclear(res);

        return certs[0];

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Find by fingerprint failed: {}", e.what());
        return Json::nullValue;
    }
}

Json::Value CertificateRepository::findByCountry(
    const std::string& countryCode,
    int limit,
    int offset
)
{
    spdlog::debug("[CertificateRepository] Finding by country: {}", countryCode);

    // TODO: Implement find by country
    spdlog::warn("[CertificateRepository] findByCountry - TODO: Implement");

    Json::Value response = Json::arrayValue;
    return response;
}

Json::Value CertificateRepository::findBySubjectDn(
    const std::string& subjectDn,
    int limit
)
{
    spdlog::debug("[CertificateRepository] Finding by subject DN: {}",
        subjectDn.substr(0, 50));

    // TODO: Implement find by subject DN
    spdlog::warn("[CertificateRepository] findBySubjectDn - TODO: Implement");

    Json::Value response = Json::arrayValue;
    return response;
}

// ========================================================================
// Certificate Counts
// ========================================================================

int CertificateRepository::countByType(const std::string& certType)
{
    spdlog::debug("[CertificateRepository] Counting by type: {}", certType);

    try {
        const char* query = "SELECT COUNT(*) FROM certificate WHERE certificate_type = $1";
        std::vector<std::string> params = {certType};

        PGresult* res = executeParamQuery(query, params);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Count by type failed: {}", e.what());
        return 0;
    }
}

int CertificateRepository::countAll()
{
    spdlog::debug("[CertificateRepository] Counting all certificates");

    try {
        const char* query = "SELECT COUNT(*) FROM certificate";
        PGresult* res = executeQuery(query);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Count all failed: {}", e.what());
        return 0;
    }
}

int CertificateRepository::countByCountry(const std::string& countryCode)
{
    spdlog::debug("[CertificateRepository] Counting by country: {}", countryCode);

    try {
        const char* query = "SELECT COUNT(*) FROM certificate WHERE country_code = $1";
        std::vector<std::string> params = {countryCode};

        PGresult* res = executeParamQuery(query, params);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Count by country failed: {}", e.what());
        return 0;
    }
}

// ========================================================================
// LDAP Storage Tracking
// ========================================================================

Json::Value CertificateRepository::findNotStoredInLdap(int limit)
{
    spdlog::debug("[CertificateRepository] Finding not stored in LDAP (limit: {})", limit);

    // TODO: Implement find not stored in LDAP
    spdlog::warn("[CertificateRepository] findNotStoredInLdap - TODO: Implement");

    Json::Value response = Json::arrayValue;
    return response;
}

bool CertificateRepository::markStoredInLdap(const std::string& fingerprint)
{
    spdlog::debug("[CertificateRepository] Marking stored in LDAP: {}...",
        fingerprint.substr(0, 16));

    try {
        const char* query =
            "UPDATE certificate SET stored_in_ldap = TRUE WHERE fingerprint_sha256 = $1";
        std::vector<std::string> params = {fingerprint};

        PGresult* res = executeParamQuery(query, params);
        PQclear(res);

        return true;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Mark stored in LDAP failed: {}", e.what());
        return false;
    }
}

// ========================================================================
// Private Helper Methods
// ========================================================================

PGresult* CertificateRepository::executeParamQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
    }

    PGresult* res = PQexecParams(
        dbConn_,
        query.c_str(),
        params.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw std::runtime_error("Query execution failed: null result");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(dbConn_);
        PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

PGresult* CertificateRepository::executeQuery(const std::string& query)
{
    PGresult* res = PQexec(dbConn_, query.c_str());

    if (!res) {
        throw std::runtime_error("Query execution failed: null result");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(dbConn_);
        PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

Json::Value CertificateRepository::pgResultToJson(PGresult* res)
{
    Json::Value array = Json::arrayValue;

    int rows = PQntuples(res);
    int cols = PQnfields(res);

    for (int i = 0; i < rows; ++i) {
        Json::Value row;
        for (int j = 0; j < cols; ++j) {
            const char* fieldName = PQfname(res, j);
            const char* value = PQgetvalue(res, i, j);

            if (PQgetisnull(res, i, j)) {
                row[fieldName] = Json::nullValue;
            } else {
                Oid type = PQftype(res, j);
                if (type == 23 || type == 20) {  // INT4 or INT8
                    row[fieldName] = std::atoi(value);
                } else if (type == 700 || type == 701) {  // FLOAT4 or FLOAT8
                    row[fieldName] = std::atof(value);
                } else if (type == 16) {  // BOOL
                    row[fieldName] = (value[0] == 't');
                } else {
                    row[fieldName] = value;
                }
            }
        }
        array.append(row);
    }

    return array;
}

} // namespace repositories

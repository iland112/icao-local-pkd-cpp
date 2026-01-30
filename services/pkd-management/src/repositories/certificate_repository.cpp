#include "certificate_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <openssl/x509.h>
#include <openssl/err.h>

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
// X509 Certificate Retrieval (for Validation)
// ========================================================================

X509* CertificateRepository::findCscaByIssuerDn(const std::string& issuerDn)
{
    if (issuerDn.empty()) {
        spdlog::warn("[CertificateRepository] findCscaByIssuerDn: empty issuer DN");
        return nullptr;
    }

    spdlog::debug("[CertificateRepository] Finding CSCA by issuer DN: {}...",
        issuerDn.substr(0, 80));

    try {
        // Extract key DN components for robust matching across formats
        std::string cn = extractDnAttribute(issuerDn, "CN");
        std::string country = extractDnAttribute(issuerDn, "C");
        std::string org = extractDnAttribute(issuerDn, "O");

        // Build query using component-based matching
        std::string query = "SELECT certificate_data, subject_dn FROM certificate "
                           "WHERE certificate_type = 'CSCA'";

        if (!cn.empty()) {
            std::string escaped = escapeSingleQuotes(cn);
            query += " AND LOWER(subject_dn) LIKE '%cn=" + escaped + "%'";
        }
        if (!country.empty()) {
            query += " AND LOWER(subject_dn) LIKE '%c=" + country + "%'";
        }
        if (!org.empty()) {
            std::string escaped = escapeSingleQuotes(org);
            query += " AND LOWER(subject_dn) LIKE '%o=" + escaped + "%'";
        }
        query += " LIMIT 20";  // Fetch candidates for post-filtering

        PGresult* res = executeQuery(query);

        // Post-filter: find exact DN match using normalized comparison
        std::string targetNormalized = normalizeDnForComparison(issuerDn);
        int matchedRow = -1;

        for (int i = 0; i < PQntuples(res); i++) {
            char* dbSubjectDn = PQgetvalue(res, i, 1);
            if (dbSubjectDn) {
                std::string dbNormalized = normalizeDnForComparison(std::string(dbSubjectDn));
                if (dbNormalized == targetNormalized) {
                    matchedRow = i;
                    spdlog::debug("[CertificateRepository] Found matching CSCA at row {}", i);
                    break;
                }
            }
        }

        if (matchedRow < 0) {
            spdlog::warn("[CertificateRepository] CSCA not found for issuer DN: {}",
                issuerDn.substr(0, 80));
            PQclear(res);
            return nullptr;
        }

        // Parse binary certificate data
        X509* cert = parseCertificateData(res, matchedRow, 0);
        PQclear(res);

        if (cert) {
            spdlog::debug("[CertificateRepository] Successfully parsed CSCA X509 certificate");
        }

        return cert;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] findCscaByIssuerDn failed: {}", e.what());
        return nullptr;
    }
}

std::vector<X509*> CertificateRepository::findAllCscasBySubjectDn(const std::string& subjectDn)
{
    std::vector<X509*> result;

    if (subjectDn.empty()) {
        spdlog::warn("[CertificateRepository] findAllCscasBySubjectDn: empty subject DN");
        return result;
    }

    spdlog::debug("[CertificateRepository] Finding all CSCAs by subject DN: {}...",
        subjectDn.substr(0, 80));

    try {
        // Extract key DN components for robust matching
        std::string cn = extractDnAttribute(subjectDn, "CN");
        std::string country = extractDnAttribute(subjectDn, "C");
        std::string org = extractDnAttribute(subjectDn, "O");

        // Build query using component-based matching
        std::string query = "SELECT certificate_data, subject_dn FROM certificate "
                           "WHERE certificate_type = 'CSCA'";

        if (!cn.empty()) {
            std::string escaped = escapeSingleQuotes(cn);
            query += " AND LOWER(subject_dn) LIKE '%cn=" + escaped + "%'";
        }
        if (!country.empty()) {
            query += " AND LOWER(subject_dn) LIKE '%c=" + country + "%'";
        }
        if (!org.empty()) {
            std::string escaped = escapeSingleQuotes(org);
            query += " AND LOWER(subject_dn) LIKE '%o=" + escaped + "%'";
        }

        PGresult* res = executeQuery(query);
        int rows = PQntuples(res);

        // Post-filter: match using normalized DN comparison
        std::string targetNormalized = normalizeDnForComparison(subjectDn);

        for (int i = 0; i < rows; i++) {
            char* dbSubjectDn = PQgetvalue(res, i, 1);
            if (dbSubjectDn) {
                std::string dbNormalized = normalizeDnForComparison(std::string(dbSubjectDn));
                if (dbNormalized == targetNormalized) {
                    X509* cert = parseCertificateData(res, i, 0);
                    if (cert) {
                        result.push_back(cert);
                        spdlog::debug("[CertificateRepository] Added CSCA {} to result", i);
                    }
                }
            }
        }

        PQclear(res);

        spdlog::info("[CertificateRepository] Found {} CSCA(s) matching subject DN", result.size());
        return result;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] findAllCscasBySubjectDn failed: {}", e.what());
        // Clean up any certificates already allocated
        for (X509* cert : result) {
            X509_free(cert);
        }
        return std::vector<X509*>();
    }
}

Json::Value CertificateRepository::findDscForRevalidation(int limit)
{
    spdlog::debug("[CertificateRepository] Finding DSC certificates for re-validation (limit: {})", limit);

    Json::Value result = Json::arrayValue;

    try {
        // Query DSC/DSC_NC certificates where CSCA was not found (failed validation)
        const char* query =
            "SELECT c.id, c.issuer_dn, c.certificate_data, c.fingerprint_sha256 "
            "FROM certificate c "
            "JOIN validation_result vr ON c.id = vr.certificate_id "
            "WHERE c.certificate_type IN ('DSC', 'DSC_NC') "
            "AND vr.csca_found = FALSE "
            "AND vr.validation_status IN ('INVALID', 'PENDING') "
            "LIMIT $1";

        std::vector<std::string> params = {std::to_string(limit)};
        PGresult* res = executeParamQuery(query, params);

        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            Json::Value cert;
            cert["id"] = PQgetvalue(res, i, 0);
            cert["issuerDn"] = PQgetvalue(res, i, 1);

            // Certificate data (bytea hex format)
            char* certData = PQgetvalue(res, i, 2);
            int certLen = PQgetlength(res, i, 2);
            if (certData && certLen > 0) {
                cert["certificateData"] = std::string(certData, certLen);
            } else {
                cert["certificateData"] = "";
            }

            cert["fingerprint"] = PQgetvalue(res, i, 3);
            result.append(cert);
        }

        PQclear(res);

        spdlog::info("[CertificateRepository] Found {} DSC(s) for re-validation", rows);
        return result;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] findDscForRevalidation failed: {}", e.what());
        return Json::arrayValue;
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

// ========================================================================
// DN Normalization Helpers
// ========================================================================

std::string CertificateRepository::extractDnAttribute(const std::string& dn, const std::string& attr)
{
    std::string searchKey = attr + "=";
    std::string dnLower = dn;
    for (char& c : dnLower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string keyLower = searchKey;
    for (char& c : keyLower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    size_t pos = 0;
    while ((pos = dnLower.find(keyLower, pos)) != std::string::npos) {
        // Verify it's at a boundary (start of string, after / or ,)
        if (pos == 0 || dnLower[pos - 1] == '/' || dnLower[pos - 1] == ',') {
            size_t valStart = pos + keyLower.size();
            size_t valEnd = dn.find_first_of("/,", valStart);
            if (valEnd == std::string::npos) {
                valEnd = dn.size();
            }
            std::string val = dn.substr(valStart, valEnd - valStart);

            // Trim and lowercase
            size_t s = val.find_first_not_of(" \t");
            size_t e = val.find_last_not_of(" \t");
            if (s != std::string::npos) {
                val = val.substr(s, e - s + 1);
                for (char& c : val) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return val;
            }
        }
        pos++;
    }
    return "";
}

std::string CertificateRepository::normalizeDnForComparison(const std::string& dn)
{
    if (dn.empty()) return dn;

    std::vector<std::string> parts;

    if (dn[0] == '/') {
        // OpenSSL slash-separated format: /C=Z/O=Y/CN=X
        std::istringstream stream(dn);
        std::string segment;
        while (std::getline(stream, segment, '/')) {
            if (!segment.empty()) {
                std::string lower;
                for (char c : segment) {
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) {
                    parts.push_back(lower.substr(s));
                }
            }
        }
    } else {
        // RFC 2253 comma-separated format: CN=X,O=Y,C=Z
        std::string current;
        bool inQuotes = false;
        for (size_t i = 0; i < dn.size(); i++) {
            char c = dn[i];
            if (c == '"') {
                inQuotes = !inQuotes;
                current += c;
            } else if (c == ',' && !inQuotes) {
                std::string lower;
                for (char ch : current) {
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) {
                    parts.push_back(lower.substr(s));
                }
                current.clear();
            } else if (c == '\\' && i + 1 < dn.size()) {
                current += c;
                current += dn[++i];
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            std::string lower;
            for (char ch : current) {
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            size_t s = lower.find_first_not_of(" \t");
            if (s != std::string::npos) {
                parts.push_back(lower.substr(s));
            }
        }
    }

    // Sort components for order-independent comparison
    std::sort(parts.begin(), parts.end());

    // Join with pipe separator
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += "|";
        result += parts[i];
    }
    return result;
}

std::string CertificateRepository::escapeSingleQuotes(const std::string& str)
{
    std::string escaped = str;
    size_t pos = 0;
    while ((pos = escaped.find("'", pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }
    return escaped;
}

X509* CertificateRepository::parseCertificateData(PGresult* res, int row, int col)
{
    char* certData = PQgetvalue(res, row, col);
    int certLen = PQgetlength(res, row, col);

    if (!certData || certLen == 0) {
        spdlog::warn("[CertificateRepository] Empty certificate data");
        return nullptr;
    }

    // Parse bytea hex format (PostgreSQL escape format: \x...)
    std::vector<uint8_t> derBytes;
    if (certLen > 2 && certData[0] == '\\' && certData[1] == 'x') {
        // Hex encoded
        for (int i = 2; i < certLen; i += 2) {
            if (i + 1 < certLen) {
                char hex[3] = {certData[i], certData[i + 1], 0};
                derBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
            }
        }
    } else {
        // Might be raw binary (starts with 0x30 for DER SEQUENCE)
        if (certLen > 0 && static_cast<unsigned char>(certData[0]) == 0x30) {
            derBytes.assign(certData, certData + certLen);
        }
    }

    if (derBytes.empty()) {
        spdlog::warn("[CertificateRepository] Failed to parse certificate binary data");
        return nullptr;
    }

    const uint8_t* data = derBytes.data();
    X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!cert) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        spdlog::error("[CertificateRepository] d2i_X509 failed: {}", errBuf);
        return nullptr;
    }

    return cert;
}

} // namespace repositories

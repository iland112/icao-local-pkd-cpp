#include "certificate_repository.h"
#include "../common/x509_metadata_extractor.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <random>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <unordered_map>

namespace repositories {

// Thread-local UUID generator (same pattern as CrlRepository)
static std::string generateUuid() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dis;

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

// Convert certificate date to Oracle-safe ISO 8601 format (no timezone suffix)
// Handles two input formats:
//   1. ASN1_TIME_print: "Jan 15 10:30:00 2024 GMT" → "2024-01-15 10:30:00"
//   2. ISO with TZ:     "2024-04-15 15:00:00+00"   → "2024-04-15 15:00:00"
// Oracle TO_TIMESTAMP expects exactly 'YYYY-MM-DD HH24:MI:SS' (19 chars)
static std::string convertDateToIso(const std::string& opensslDate) {
    if (opensslDate.empty()) return "";

    // Check if already in ISO-like format (starts with digit: "2024-...")
    if (!opensslDate.empty() && std::isdigit(opensslDate[0])) {
        // Already ISO format, just strip timezone suffix (+00, +00:00, Z, etc.)
        std::string result = opensslDate;
        // Find the position after "YYYY-MM-DD HH:MI:SS" (19 chars)
        if (result.length() > 19) {
            result = result.substr(0, 19);
        }
        return result;
    }

    // ASN1_TIME_print format: "Jan 15 10:30:00 2024 GMT"
    static const std::unordered_map<std::string, std::string> months = {
        {"Jan", "01"}, {"Feb", "02"}, {"Mar", "03"}, {"Apr", "04"},
        {"May", "05"}, {"Jun", "06"}, {"Jul", "07"}, {"Aug", "08"},
        {"Sep", "09"}, {"Oct", "10"}, {"Nov", "11"}, {"Dec", "12"}
    };

    std::istringstream iss(opensslDate);
    std::string month, day, time, year;
    iss >> month >> day >> time >> year;

    auto it = months.find(month);
    if (it == months.end()) {
        // Unknown format — truncate to 19 chars as safety measure
        return opensslDate.length() > 19 ? opensslDate.substr(0, 19) : opensslDate;
    }

    if (day.length() == 1) day = "0" + day;

    // Truncate time to HH:MI:SS (strip any fractional seconds)
    if (time.length() > 8) {
        time = time.substr(0, 8);
    }

    return year + "-" + it->second + "-" + day + " " + time;
}

CertificateRepository::CertificateRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("CertificateRepository: queryExecutor cannot be nullptr");
    }
    spdlog::debug("[CertificateRepository] Initialized with database type: {}",
        queryExecutor_->getDatabaseType());
}

// ========================================================================
// Search Operations
// ========================================================================

Json::Value CertificateRepository::search(const CertificateSearchFilter& filter)
{
    spdlog::debug("[CertificateRepository] Searching certificates (DB-based)");

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::vector<std::string> params;
        int paramIdx = 1;

        // Build dynamic WHERE clause
        std::ostringstream where;
        if (filter.countryCode.has_value() && !filter.countryCode->empty()) {
            where << " AND country_code = $" << paramIdx++;
            params.push_back(*filter.countryCode);
        }
        if (filter.certificateType.has_value() && !filter.certificateType->empty()) {
            where << " AND certificate_type = $" << paramIdx++;
            params.push_back(*filter.certificateType);
        }
        if (filter.sourceType.has_value() && !filter.sourceType->empty()) {
            where << " AND source_type = $" << paramIdx++;
            params.push_back(*filter.sourceType);
        }
        if (filter.searchTerm.has_value() && !filter.searchTerm->empty()) {
            std::string term = "%" + *filter.searchTerm + "%";
            if (dbType == "oracle") {
                where << " AND (UPPER(subject_dn) LIKE UPPER($" << paramIdx << ")"
                       << " OR serial_number LIKE $" << paramIdx << ")";
            } else {
                where << " AND (subject_dn ILIKE $" << paramIdx
                       << " OR serial_number ILIKE $" << paramIdx << ")";
            }
            paramIdx++;
            params.push_back(term);
        }

        std::string whereStr = where.str();

        // Count query
        std::string countSql = "SELECT COUNT(*) FROM certificate WHERE 1=1" + whereStr;
        Json::Value countResult = queryExecutor_->executeScalar(countSql, params);
        int total = 0;
        if (countResult.isInt()) total = countResult.asInt();
        else if (countResult.isString()) { try { total = std::stoi(countResult.asString()); } catch (...) {} }

        // Data query
        std::ostringstream dataSql;
        dataSql << "SELECT id, certificate_type, country_code, subject_dn, issuer_dn, "
                << "serial_number, fingerprint_sha256, not_before, not_after, "
                << "validation_status, source_type, stored_in_ldap, "
                << "is_self_signed, version, signature_algorithm, "
                << "public_key_algorithm, public_key_size "
                << "FROM certificate WHERE 1=1" << whereStr
                << " ORDER BY created_at DESC";

        if (dbType == "oracle") {
            dataSql << " OFFSET " << filter.offset << " ROWS FETCH NEXT " << filter.limit << " ROWS ONLY";
        } else {
            dataSql << " LIMIT " << filter.limit << " OFFSET " << filter.offset;
        }

        Json::Value rows = queryExecutor_->executeQuery(dataSql.str(), params);

        // Build response
        Json::Value response;
        response["success"] = true;
        response["total"] = total;
        response["limit"] = filter.limit;
        response["offset"] = filter.offset;

        Json::Value certificates(Json::arrayValue);
        for (const auto& row : rows) {
            Json::Value cert;
            cert["dn"] = "";
            cert["cn"] = row.get("subject_dn", "").asString();
            cert["sn"] = row.get("serial_number", "").asString();
            cert["country"] = row.get("country_code", "").asString();
            cert["type"] = row.get("certificate_type", "").asString();
            cert["subjectDn"] = row.get("subject_dn", "").asString();
            cert["issuerDn"] = row.get("issuer_dn", "").asString();
            cert["fingerprint"] = row.get("fingerprint_sha256", "").asString();
            cert["validFrom"] = row.get("not_before", "").asString();
            cert["validTo"] = row.get("not_after", "").asString();
            cert["sourceType"] = row.get("source_type", "").asString();

            // Parse DN components from subject_dn and issuer_dn
            // Supports both formats: /C=KR/O=Gov/CN=Name and CN=Name,O=Gov,C=KR
            auto parseDnComponents = [](const std::string& dn) -> Json::Value {
                Json::Value components;
                if (dn.empty()) return components;

                char delim = '/';
                std::string input = dn;
                if (!dn.empty() && dn[0] == '/') {
                    // Slash-separated: /C=KR/O=Government/OU=MOFA/CN=Name
                    delim = '/';
                    input = dn.substr(1); // skip leading /
                } else {
                    // Comma-separated: CN=Name,OU=bsi,O=bund,C=DE
                    delim = ',';
                }

                std::istringstream ss(input);
                std::string token;
                while (std::getline(ss, token, delim)) {
                    // Trim whitespace
                    size_t start = token.find_first_not_of(" ");
                    if (start == std::string::npos) continue;
                    token = token.substr(start);

                    auto eqPos = token.find('=');
                    if (eqPos == std::string::npos) continue;
                    std::string key = token.substr(0, eqPos);
                    std::string val = token.substr(eqPos + 1);
                    if (key == "CN") components["commonName"] = val;
                    else if (key == "O") components["organization"] = val;
                    else if (key == "OU") components["organizationalUnit"] = val;
                    else if (key == "C") components["country"] = val;
                    else if (key == "SERIALNUMBER" || key == "serialNumber") components["serialNumber"] = val;
                }
                return components;
            };
            cert["subjectDnComponents"] = parseDnComponents(row.get("subject_dn", "").asString());
            cert["issuerDnComponents"] = parseDnComponents(row.get("issuer_dn", "").asString());

            // Validation status → validity
            std::string valStatus = row.get("validation_status", "UNKNOWN").asString();
            if (valStatus == "VALID") cert["validity"] = "VALID";
            else if (valStatus == "EXPIRED") cert["validity"] = "EXPIRED";
            else cert["validity"] = "UNKNOWN";

            // Boolean fields
            std::string selfSigned = row.get("is_self_signed", "").asString();
            cert["isSelfSigned"] = (selfSigned == "t" || selfSigned == "true" || selfSigned == "1");

            // Metadata
            if (!row.get("version", Json::nullValue).isNull()) {
                std::string ver = row.get("version", "").asString();
                try { cert["version"] = std::stoi(ver); } catch (...) { cert["version"] = 0; }
            }
            if (!row.get("signature_algorithm", Json::nullValue).isNull())
                cert["signatureAlgorithm"] = row["signature_algorithm"].asString();
            if (!row.get("public_key_algorithm", Json::nullValue).isNull())
                cert["publicKeyAlgorithm"] = row["public_key_algorithm"].asString();
            if (!row.get("public_key_size", Json::nullValue).isNull()) {
                std::string pks = row.get("public_key_size", "").asString();
                try { cert["publicKeySize"] = std::stoi(pks); } catch (...) {}
            }

            certificates.append(cert);
        }
        response["certificates"] = certificates;

        // Validity statistics query (same WHERE clause)
        std::string statsSql = "SELECT validation_status, COUNT(*) as cnt FROM certificate WHERE 1=1" + whereStr + " GROUP BY validation_status";
        Json::Value statsRows = queryExecutor_->executeQuery(statsSql, params);

        int valid = 0, expired = 0, notYetValid = 0, unknown = 0;
        for (const auto& srow : statsRows) {
            std::string vs = srow.get("validation_status", "").asString();
            int cnt = 0;
            auto cntVal = srow.get("cnt", 0);
            if (cntVal.isInt()) cnt = cntVal.asInt();
            else if (cntVal.isString()) { try { cnt = std::stoi(cntVal.asString()); } catch (...) {} }

            if (vs == "VALID") valid = cnt;
            else if (vs == "EXPIRED") expired = cnt;
            else if (vs == "NOT_YET_VALID") notYetValid = cnt;
            else unknown += cnt;
        }

        Json::Value stats;
        stats["total"] = total;
        stats["valid"] = valid;
        stats["expired"] = expired;
        stats["notYetValid"] = notYetValid;
        stats["unknown"] = unknown;
        response["stats"] = stats;

        spdlog::info("[CertificateRepository] DB search returned {} / {} results",
            rows.size(), total);
        return response;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] search failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        return error;
    }
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
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            return Json::nullValue;
        }

        return result[0];

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

        Json::Value result = queryExecutor_->executeScalar(query, params);
        return result.asInt();

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
        Json::Value result = queryExecutor_->executeScalar(query);
        return result.asInt();

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

        Json::Value result = queryExecutor_->executeScalar(query, params);
        return result.asInt();

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

        queryExecutor_->executeCommand(query, params);
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

        // Build parameterized query using component-based matching
        std::string query = "SELECT certificate_data, subject_dn FROM certificate "
                           "WHERE certificate_type = 'CSCA'";
        std::vector<std::string> params;
        int paramIdx = 1;

        if (!cn.empty()) {
            query += " AND LOWER(subject_dn) LIKE LOWER($" + std::to_string(paramIdx++) + ")";
            params.push_back("%cn=" + cn + "%");
        }
        if (!country.empty()) {
            query += " AND LOWER(subject_dn) LIKE LOWER($" + std::to_string(paramIdx++) + ")";
            params.push_back("%c=" + country + "%");
        }
        if (!org.empty()) {
            query += " AND LOWER(subject_dn) LIKE LOWER($" + std::to_string(paramIdx++) + ")";
            params.push_back("%o=" + org + "%");
        }
        query += " LIMIT 20";  // Fetch candidates for post-filtering

        Json::Value result = queryExecutor_->executeQuery(query, params);

        // Post-filter: find exact DN match using normalized comparison
        std::string targetNormalized = normalizeDnForComparison(issuerDn);
        int matchedRow = -1;

        for (Json::ArrayIndex i = 0; i < result.size(); i++) {
            std::string dbSubjectDn = result[i].get("subject_dn", "").asString();
            if (!dbSubjectDn.empty()) {
                std::string dbNormalized = normalizeDnForComparison(dbSubjectDn);
                if (dbNormalized == targetNormalized) {
                    matchedRow = static_cast<int>(i);
                    spdlog::debug("[CertificateRepository] Found matching CSCA at row {}", i);
                    break;
                }
            }
        }

        if (matchedRow < 0) {
            spdlog::warn("[CertificateRepository] CSCA not found for issuer DN: {}",
                issuerDn.substr(0, 80));
            return nullptr;
        }

        // Parse binary certificate data from hex-encoded string
        std::string certDataHex = result[matchedRow].get("certificate_data", "").asString();
        X509* cert = parseCertificateDataFromHex(certDataHex);

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

        // Build parameterized query using component-based matching
        // ORDER BY created_at DESC: prefer newest CSCA first (most likely to match current DSCs)
        std::string query = "SELECT certificate_data, subject_dn FROM certificate "
                           "WHERE certificate_type = 'CSCA'";
        std::vector<std::string> params;
        int paramIdx = 1;

        if (!cn.empty()) {
            query += " AND LOWER(subject_dn) LIKE LOWER($" + std::to_string(paramIdx++) + ")";
            params.push_back("%cn=" + cn + "%");
        }
        if (!country.empty()) {
            query += " AND LOWER(subject_dn) LIKE LOWER($" + std::to_string(paramIdx++) + ")";
            params.push_back("%c=" + country + "%");
        }
        if (!org.empty()) {
            query += " AND LOWER(subject_dn) LIKE LOWER($" + std::to_string(paramIdx++) + ")";
            params.push_back("%o=" + org + "%");
        }

        query += " ORDER BY created_at DESC";

        Json::Value rows = queryExecutor_->executeQuery(query, params);

        // Post-filter: match using normalized DN comparison
        std::string targetNormalized = normalizeDnForComparison(subjectDn);

        for (Json::ArrayIndex i = 0; i < rows.size(); i++) {
            std::string dbSubjectDn = rows[i].get("subject_dn", "").asString();
            if (!dbSubjectDn.empty()) {
                std::string dbNormalized = normalizeDnForComparison(dbSubjectDn);
                if (dbNormalized == targetNormalized) {
                    std::string certDataHex = rows[i].get("certificate_data", "").asString();
                    X509* cert = parseCertificateDataFromHex(certDataHex);
                    if (cert) {
                        result.push_back(cert);
                        spdlog::debug("[CertificateRepository] Added CSCA {} to result", i);
                    }
                }
            }
        }

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

    try {
        // Query DSC/DSC_NC certificates where CSCA was not found (failed validation)
        const char* query =
            "SELECT c.id, c.issuer_dn, c.certificate_data, c.fingerprint_sha256 "
            "FROM certificate c "
            "JOIN validation_result vr ON c.id = vr.certificate_id "
            "WHERE c.certificate_type IN ('DSC', 'DSC_NC') "
            "AND vr.csca_found = FALSE "
            "AND vr.validation_status IN ('INVALID', 'PENDING') "
            "ORDER BY c.not_after DESC "
            "LIMIT $1";

        std::vector<std::string> params = {std::to_string(limit)};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        // Transform field names to match expected format (camelCase)
        for (Json::ArrayIndex i = 0; i < result.size(); i++) {
            // Field names are already in the correct format from query executor
            // Just ensure certificateData field exists
            if (!result[i].isMember("certificateData") && result[i].isMember("certificate_data")) {
                result[i]["certificateData"] = result[i]["certificate_data"];
                result[i].removeMember("certificate_data");
            }
            if (!result[i].isMember("issuerDn") && result[i].isMember("issuer_dn")) {
                result[i]["issuerDn"] = result[i]["issuer_dn"];
                result[i].removeMember("issuer_dn");
            }
            if (!result[i].isMember("fingerprint") && result[i].isMember("fingerprint_sha256")) {
                result[i]["fingerprint"] = result[i]["fingerprint_sha256"];
                result[i].removeMember("fingerprint_sha256");
            }
        }

        spdlog::info("[CertificateRepository] Found {} DSC(s) for re-validation", result.size());
        return result;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] findDscForRevalidation failed: {}", e.what());
        return Json::arrayValue;
    }
}

// ========================================================================
// Private Helper Methods
// ========================================================================

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

X509* CertificateRepository::parseCertificateDataFromHex(const std::string& hexData)
{
    if (hexData.empty()) {
        spdlog::warn("[CertificateRepository] Empty certificate data");
        return nullptr;
    }

    // Helper: decode hex string (with \x prefix) to bytes
    auto decodeHex = [](const std::string& hex, size_t start) -> std::vector<uint8_t> {
        std::vector<uint8_t> bytes;
        bytes.reserve((hex.size() - start) / 2);
        for (size_t i = start; i + 1 < hex.size(); i += 2) {
            char h[3] = {hex[i], hex[i + 1], 0};
            bytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
        }
        return bytes;
    };

    // Parse bytea hex format (PostgreSQL escape format: \x...)
    std::vector<uint8_t> derBytes;
    if (hexData.size() > 2 && hexData[0] == '\\' && hexData[1] == 'x') {
        // First hex decode
        derBytes = decodeHex(hexData, 2);

        // Handle double-encoded BYTEA: if the decoded bytes start with \x (0x5C 0x78)
        // followed by ASCII hex digits, it means the data was stored as a hex text string
        // rather than raw binary. Decode again.
        if (derBytes.size() > 2 && derBytes[0] == 0x5C && derBytes[1] == 0x78) {
            std::string innerHex(derBytes.begin(), derBytes.end());
            derBytes = decodeHex(innerHex, 2);
            spdlog::debug("[CertificateRepository] Double-encoded BYTEA detected, decoded twice");
        }
    } else {
        // Might be raw binary (starts with 0x30 for DER SEQUENCE)
        if (!hexData.empty() && static_cast<unsigned char>(hexData[0]) == 0x30) {
            derBytes.assign(hexData.begin(), hexData.end());
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

// ============================================================================
// Duplicate Certificate Tracking (v2.2.1)
// ============================================================================

std::string CertificateRepository::findFirstUploadIdByFingerprint(const std::string& fingerprint) {
    try {
        const char* query =
            "SELECT upload_id FROM certificate "
            "WHERE fingerprint_sha256 = $1 "
            "ORDER BY uploaded_at ASC LIMIT 1";

        std::vector<std::string> params = {fingerprint};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (!result.empty()) {
            std::string uploadId = result[0].get("upload_id", "").asString();
            spdlog::debug("[CertificateRepository] Found first upload_id={} for fingerprint={}",
                         uploadId, fingerprint.substr(0, 16));
            return uploadId;
        }

        return "";

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] findFirstUploadIdByFingerprint failed: {}", e.what());
        return "";
    }
}

bool CertificateRepository::saveDuplicate(const std::string& uploadId,
                                         const std::string& firstUploadId,
                                         const std::string& fingerprint,
                                         const std::string& certType,
                                         const std::string& subjectDn,
                                         const std::string& issuerDn,
                                         const std::string& countryCode,
                                         const std::string& serialNumber) {
    try {
        const char* query =
            "INSERT INTO duplicate_certificate "
            "(upload_id, first_upload_id, fingerprint_sha256, certificate_type, "
            "subject_dn, issuer_dn, country_code, serial_number, duplicate_count, detection_timestamp) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, 1, CURRENT_TIMESTAMP) "
            "ON CONFLICT (upload_id, fingerprint_sha256, certificate_type) "
            "DO UPDATE SET duplicate_count = duplicate_certificate.duplicate_count + 1";

        std::vector<std::string> params = {
            uploadId,
            firstUploadId,
            fingerprint,
            certType,
            subjectDn,
            issuerDn,
            countryCode.empty() ? "" : countryCode,
            serialNumber.empty() ? "" : serialNumber
        };

        queryExecutor_->executeCommand(query, params);

        spdlog::debug("[CertificateRepository] Saved duplicate: fingerprint={}, type={}, upload={}",
                     fingerprint.substr(0, 16), certType, uploadId);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Failed to save duplicate: {}", e.what());
        return false;
    }
}

// ============================================================================
// Certificate Insert & Duplicate Tracking (Phase 6.1 - Oracle Migration)
// ============================================================================

bool CertificateRepository::updateCertificateLdapStatus(
    const std::string& certificateId,
    const std::string& ldapDn
)
{
    spdlog::debug("[CertificateRepository] Updating LDAP status: cert_id={}, ldap_dn={}",
                  certificateId.substr(0, 8) + "...", ldapDn.substr(0, 40) + "...");

    try {
        const char* query =
            "UPDATE certificate "
            "SET stored_in_ldap = $1, ldap_dn = $2 "
            "WHERE id = $3";

        // Database-aware boolean formatting
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string storedValue = (dbType == "oracle") ? "1" : "true";

        std::vector<std::string> params = {storedValue, ldapDn, certificateId};

        queryExecutor_->executeCommand(query, params);

        spdlog::debug("[CertificateRepository] LDAP status updated: cert_id={}",
                     certificateId.substr(0, 8) + "...");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] updateCertificateLdapStatus failed: {}", e.what());
        return false;
    }
}

bool CertificateRepository::incrementDuplicateCount(
    const std::string& certificateId,
    const std::string& uploadId
)
{
    spdlog::debug("[CertificateRepository] Incrementing duplicate count: cert_id={}, upload={}",
                  certificateId.substr(0, 8) + "...", uploadId.substr(0, 8) + "...");

    try {
        const char* query =
            "UPDATE certificate "
            "SET duplicate_count = duplicate_count + 1, "
            "    last_seen_upload_id = $1, "
            "    last_seen_at = CURRENT_TIMESTAMP "
            "WHERE id = $2";

        std::vector<std::string> params = {uploadId, certificateId};

        queryExecutor_->executeCommand(query, params);

        spdlog::debug("[CertificateRepository] Duplicate count incremented: cert_id={}",
                     certificateId.substr(0, 8) + "...");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] incrementDuplicateCount failed: {}", e.what());
        return false;
    }
}

bool CertificateRepository::trackCertificateDuplicate(
    const std::string& certificateId,
    const std::string& uploadId,
    const std::string& sourceType,
    const std::string& sourceCountry,
    const std::string& sourceEntryDn,
    const std::string& sourceFileName
)
{
    spdlog::debug("[CertificateRepository] Tracking duplicate: cert_id={}, upload={}, source_type={}",
                  certificateId.substr(0, 8) + "...", uploadId.substr(0, 8) + "...", sourceType);

    try {
        const char* query =
            "INSERT INTO certificate_duplicates ("
            "certificate_id, upload_id, source_type, source_country, "
            "source_entry_dn, source_file_name, detected_at"
            ") VALUES ("
            "$1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP"
            ") ON CONFLICT (certificate_id, upload_id, source_type) DO NOTHING";

        std::vector<std::string> params = {
            certificateId,
            uploadId,
            sourceType,
            sourceCountry,
            sourceEntryDn,
            sourceFileName
        };

        queryExecutor_->executeCommand(query, params);

        spdlog::debug("[CertificateRepository] Duplicate tracked: cert_id={}, source_type={}",
                     certificateId.substr(0, 8) + "...", sourceType);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] trackCertificateDuplicate failed: {}", e.what());
        return false;
    }
}

std::pair<std::string, bool> CertificateRepository::saveCertificateWithDuplicateCheck(
    const std::string& uploadId,
    const std::string& certType,
    const std::string& countryCode,
    const std::string& subjectDn,
    const std::string& issuerDn,
    const std::string& serialNumber,
    const std::string& fingerprint,
    const std::string& notBefore,
    const std::string& notAfter,
    const std::vector<uint8_t>& certData,
    const std::string& validationStatus,
    const std::string& validationMessage
)
{
    spdlog::debug("[CertificateRepository] Saving certificate: type={}, country={}, fingerprint={}",
                  certType, countryCode, fingerprint.substr(0, 16) + "...");

    try {
        // ====================================================================
        // Step 1: Check if certificate already exists
        // ====================================================================
        const char* checkQuery =
            "SELECT id, first_upload_id FROM certificate "
            "WHERE certificate_type = $1 AND fingerprint_sha256 = $2";

        std::vector<std::string> checkParams = {certType, fingerprint};
        Json::Value checkResult = queryExecutor_->executeQuery(checkQuery, checkParams);

        // If certificate exists, return existing ID with isDuplicate=true
        if (!checkResult.empty()) {
            std::string existingId = checkResult[0]["id"].asString();
            spdlog::debug("[CertificateRepository] Duplicate certificate found: id={}, fingerprint={}",
                         existingId.substr(0, 8) + "...", fingerprint.substr(0, 16) + "...");
            return std::make_pair(existingId, true);
        }

        // ====================================================================
        // Step 2: Extract X.509 metadata from certificate
        // ====================================================================
        const unsigned char* certPtr = certData.data();
        X509* x509cert = d2i_X509(nullptr, &certPtr, static_cast<long>(certData.size()));

        x509::CertificateMetadata x509meta;
        std::string versionStr, sigAlg, sigHashAlg, pubKeyAlg, pubKeySizeStr;
        std::string pubKeyCurve, keyUsageStr, extKeyUsageStr, isCaStr;
        std::string pathLenStr, ski, aki, crlDpStr, ocspUrl, isSelfSignedStr;

        if (x509cert) {
            x509meta = x509::extractMetadata(x509cert);
            X509_free(x509cert);

            // Convert metadata to SQL strings
            versionStr = std::to_string(x509meta.version);
            sigAlg = x509meta.signatureAlgorithm;
            sigHashAlg = x509meta.signatureHashAlgorithm;
            pubKeyAlg = x509meta.publicKeyAlgorithm;
            pubKeySizeStr = std::to_string(x509meta.publicKeySize);
            pubKeyCurve = x509meta.publicKeyCurve.value_or("");

            // Convert arrays to comma-separated strings (database-agnostic)
            // PostgreSQL requires TEXT[] format: {element1,element2} or {} for empty
            // Oracle uses VARCHAR2: element1,element2 or empty string
            std::string dbType = queryExecutor_->getDatabaseType();

            std::ostringstream kuStream, ekuStream, crlStream;
            for (size_t i = 0; i < x509meta.keyUsage.size(); i++) {
                kuStream << x509meta.keyUsage[i];
                if (i < x509meta.keyUsage.size() - 1) kuStream << ",";
            }
            keyUsageStr = kuStream.str();
            if (dbType == "postgres") {
                // PostgreSQL: wrap in braces, use {} for empty
                keyUsageStr = keyUsageStr.empty() ? "{}" : "{" + keyUsageStr + "}";
            }

            for (size_t i = 0; i < x509meta.extendedKeyUsage.size(); i++) {
                ekuStream << x509meta.extendedKeyUsage[i];
                if (i < x509meta.extendedKeyUsage.size() - 1) ekuStream << ",";
            }
            extKeyUsageStr = ekuStream.str();
            if (dbType == "postgres") {
                // PostgreSQL: wrap in braces, use {} for empty
                extKeyUsageStr = extKeyUsageStr.empty() ? "{}" : "{" + extKeyUsageStr + "}";
            }

            for (size_t i = 0; i < x509meta.crlDistributionPoints.size(); i++) {
                crlStream << x509meta.crlDistributionPoints[i];
                if (i < x509meta.crlDistributionPoints.size() - 1) crlStream << ",";
            }
            crlDpStr = crlStream.str();
            if (dbType == "postgres") {
                // PostgreSQL: wrap in braces, use {} for empty
                crlDpStr = crlDpStr.empty() ? "{}" : "{" + crlDpStr + "}";
            }

            // Database-aware boolean formatting (dbType already retrieved above)
            isCaStr = x509meta.isCA ? (dbType == "oracle" ? "1" : "TRUE") : (dbType == "oracle" ? "0" : "FALSE");
            isSelfSignedStr = x509meta.isSelfSigned ? (dbType == "oracle" ? "1" : "TRUE") : (dbType == "oracle" ? "0" : "FALSE");

            pathLenStr = x509meta.pathLenConstraint.has_value() ?
                         std::to_string(x509meta.pathLenConstraint.value()) : "";
            ski = x509meta.subjectKeyIdentifier.value_or("");
            aki = x509meta.authorityKeyIdentifier.value_or("");
            ocspUrl = x509meta.ocspResponderUrl.value_or("");
        } else {
            spdlog::warn("[CertificateRepository] Failed to parse X509 certificate for metadata extraction");
            versionStr = "2";  // Default to v3
            std::string dbType = queryExecutor_->getDatabaseType();
            isCaStr = dbType == "oracle" ? "0" : "FALSE";
            isSelfSignedStr = dbType == "oracle" ? "0" : "FALSE";
            keyUsageStr = "";
            extKeyUsageStr = "";
            crlDpStr = "";
        }

        // ====================================================================
        // Step 3: Insert new certificate with X.509 metadata
        // ====================================================================

        std::string dbType = queryExecutor_->getDatabaseType();

        // Convert DER bytes to hex string
        std::ostringstream hexStream;
        // PostgreSQL: \x prefix for hex bytea format (PQexecParams text mode)
        // Oracle: \\x prefix as BLOB marker detected by OracleQueryExecutor
        hexStream << (dbType == "oracle" ? "\\\\x" : "\\x");
        for (size_t i = 0; i < certData.size(); i++) {
            hexStream << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(certData[i]);
        }
        std::string certDataHex = hexStream.str();

        std::string newId;

        if (dbType == "oracle") {
            // Oracle: Generate UUID in C++ (uuid_generate_v4 is PostgreSQL-only)
            newId = generateUuid();

            std::string insertQuery =
                "INSERT INTO certificate ("
                "id, upload_id, certificate_type, country_code, "
                "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                "not_before, not_after, certificate_data, "
                "validation_status, validation_message, "
                "duplicate_count, first_upload_id, created_at, "
                "version, signature_algorithm, signature_hash_algorithm, "
                "public_key_algorithm, public_key_size, public_key_curve, "
                "key_usage, extended_key_usage, "
                "is_ca, path_len_constraint, "
                "subject_key_identifier, authority_key_identifier, "
                "crl_distribution_points, ocsp_responder_url, is_self_signed"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, "
                "CASE WHEN $9 IS NULL OR $9 = '' THEN NULL ELSE TO_TIMESTAMP($9, 'YYYY-MM-DD HH24:MI:SS') END, "
                "CASE WHEN $10 IS NULL OR $10 = '' THEN NULL ELSE TO_TIMESTAMP($10, 'YYYY-MM-DD HH24:MI:SS') END, "
                "$11, $12, $13, 0, $2, SYSTIMESTAMP, "
                "TO_NUMBER(NULLIF($14, '')), $15, $16, "
                "$17, TO_NUMBER(NULLIF($18, '')), $19, "
                "$20, $21, "
                "TO_NUMBER(NULLIF($22, '')), TO_NUMBER(NULLIF($23, '')), "
                "$24, $25, "
                "$26, $27, TO_NUMBER(NULLIF($28, ''))"
                ")";

            // Convert OpenSSL date format to ISO for Oracle TIMESTAMP columns
            std::string notBeforeIso = convertDateToIso(notBefore);
            std::string notAfterIso = convertDateToIso(notAfter);
            spdlog::debug("[CertificateRepository] Oracle date conversion: '{}' → '{}', '{}' → '{}'",
                notBefore, notBeforeIso, notAfter, notAfterIso);

            std::vector<std::string> insertParams = {
                newId,                                   // $1 (pre-generated id)
                uploadId,                                // $2
                certType,                                // $3
                countryCode,                             // $4
                subjectDn,                               // $5
                issuerDn,                                // $6
                serialNumber,                            // $7
                fingerprint,                             // $8
                notBeforeIso,                            // $9  (ISO format for Oracle)
                notAfterIso,                             // $10 (ISO format for Oracle)
                certDataHex,                             // $11
                validationStatus,                        // $12
                validationMessage,                       // $13
                versionStr,                              // $14
                sigAlg.empty() ? "" : sigAlg,            // $15
                sigHashAlg.empty() ? "" : sigHashAlg,    // $16
                pubKeyAlg.empty() ? "" : pubKeyAlg,      // $17
                pubKeySizeStr == "0" ? "" : pubKeySizeStr, // $18
                pubKeyCurve,                             // $19
                keyUsageStr,                             // $20
                extKeyUsageStr,                          // $21
                isCaStr,                                 // $22
                pathLenStr,                              // $23
                ski,                                     // $24
                aki,                                     // $25
                crlDpStr,                                // $26
                ocspUrl,                                 // $27
                isSelfSignedStr                          // $28
            };

            queryExecutor_->executeCommand(insertQuery, insertParams);

        } else {
            // PostgreSQL: Use RETURNING id
            const char* insertQuery =
                "INSERT INTO certificate ("
                "upload_id, certificate_type, country_code, "
                "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                "not_before, not_after, certificate_data, "
                "validation_status, validation_message, "
                "duplicate_count, first_upload_id, created_at, "
                "version, signature_algorithm, signature_hash_algorithm, "
                "public_key_algorithm, public_key_size, public_key_curve, "
                "key_usage, extended_key_usage, "
                "is_ca, path_len_constraint, "
                "subject_key_identifier, authority_key_identifier, "
                "crl_distribution_points, ocsp_responder_url, is_self_signed"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, 0, $1, CURRENT_TIMESTAMP, "
                "$13, $14, $15, "
                "$16, NULLIF($17, '')::INTEGER, $18, "
                "$19, $20, "
                "$21, NULLIF($22, '')::INTEGER, "
                "$23, $24, "
                "$25, $26, $27"
                ") RETURNING id";

            std::vector<std::string> insertParams = {
                uploadId,                                // $1
                certType,                                // $2
                countryCode,                             // $3
                subjectDn,                               // $4
                issuerDn,                                // $5
                serialNumber,                            // $6
                fingerprint,                             // $7
                notBefore,                               // $8
                notAfter,                                // $9
                certDataHex,                             // $10
                validationStatus,                        // $11
                validationMessage,                       // $12
                versionStr,                              // $13
                sigAlg.empty() ? "" : sigAlg,            // $14
                sigHashAlg.empty() ? "" : sigHashAlg,    // $15
                pubKeyAlg.empty() ? "" : pubKeyAlg,      // $16
                pubKeySizeStr == "0" ? "" : pubKeySizeStr, // $17
                pubKeyCurve,                             // $18
                keyUsageStr,                             // $19
                extKeyUsageStr,                          // $20
                isCaStr,                                 // $21
                pathLenStr,                              // $22
                ski,                                     // $23
                aki,                                     // $24
                crlDpStr,                                // $25
                ocspUrl,                                 // $26
                isSelfSignedStr                          // $27
            };

            Json::Value insertResult = queryExecutor_->executeQuery(insertQuery, insertParams);

            if (insertResult.empty()) {
                spdlog::error("[CertificateRepository] INSERT failed: no ID returned");
                return std::make_pair(std::string(""), false);
            }

            newId = insertResult[0]["id"].asString();
        }

        spdlog::debug("[CertificateRepository] New certificate inserted: id={}, type={}, country={}, fingerprint={}",
                     newId.substr(0, 8) + "...", certType, countryCode, fingerprint.substr(0, 16) + "...");

        return std::make_pair(newId, false);

    } catch (const std::exception& e) {
        std::string errMsg = e.what();
        // ORA-00001: unique constraint violated — treat as duplicate (race condition)
        // This is equivalent to PostgreSQL's ON CONFLICT DO NOTHING behavior
        if (errMsg.find("ORA-00001") != std::string::npos) {
            spdlog::debug("[CertificateRepository] Concurrent duplicate detected (ORA-00001): type={}, fingerprint={}",
                         certType, fingerprint.substr(0, 16) + "...");
            // Re-query to get the existing certificate ID
            try {
                const char* reCheckQuery =
                    "SELECT id FROM certificate WHERE certificate_type = $1 AND fingerprint_sha256 = $2";
                Json::Value reResult = queryExecutor_->executeQuery(reCheckQuery, {certType, fingerprint});
                if (!reResult.empty()) {
                    return std::make_pair(reResult[0]["id"].asString(), true);
                }
            } catch (...) {
                // Ignore re-query failure
            }
            return std::make_pair(std::string(""), true);  // isDuplicate=true
        }
        spdlog::error("[CertificateRepository] saveCertificateWithDuplicateCheck failed: {}", errMsg);
        return std::make_pair(std::string(""), false);
    }
}

// ============================================================================
// Phase 2-2: LDAP Status Count by Upload ID
// ============================================================================

void CertificateRepository::countLdapStatusByUploadId(const std::string& uploadId, int& outTotal, int& outInLdap) {
    std::string dbType = queryExecutor_->getDatabaseType();
    std::string boolTrue = (dbType == "oracle") ? "1" : "true";

    std::string query = "SELECT COUNT(*) as total, "
                        "COALESCE(SUM(CASE WHEN stored_in_ldap = " + boolTrue + " THEN 1 ELSE 0 END), 0) as in_ldap "
                        "FROM certificate WHERE upload_id = $1";

    auto rows = queryExecutor_->executeQuery(query, {uploadId});

    if (!rows.empty()) {
        auto safeInt = [](const Json::Value& v) -> int {
            if (v.isInt()) return v.asInt();
            if (v.isString()) { try { return std::stoi(v.asString()); } catch (...) { return 0; } }
            return 0;
        };
        outTotal = safeInt(rows[0]["total"]);
        outInLdap = safeInt(rows[0]["in_ldap"]);
    } else {
        outTotal = 0;
        outInLdap = 0;
    }
}

// ============================================================================
// Phase 2-4: Distinct Countries
// ============================================================================

Json::Value CertificateRepository::getDistinctCountries() {
    std::string query = "SELECT DISTINCT country_code FROM certificate "
                        "WHERE country_code IS NOT NULL "
                        "ORDER BY country_code";

    return queryExecutor_->executeQuery(query);
}

// ============================================================================
// Phase 2-5: Link Certificate Search
// ============================================================================

Json::Value CertificateRepository::searchLinkCertificates(
    const std::string& countryFilter,
    const std::string& validFilter,
    int limit, int offset)
{
    std::string dbType = queryExecutor_->getDatabaseType();
    std::string boolTrue = (dbType == "oracle") ? "1" : "true";

    std::ostringstream sql;
    sql << "SELECT id, subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
        << "old_csca_subject_dn, new_csca_subject_dn, "
        << "trust_chain_valid, created_at, country_code "
        << "FROM link_certificate WHERE 1=1";

    std::vector<std::string> paramValues;
    int paramIndex = 1;

    if (!countryFilter.empty()) {
        sql << " AND country_code = $" << paramIndex++;
        paramValues.push_back(countryFilter);
    }

    if (validFilter == "true") {
        sql << " AND trust_chain_valid = " << boolTrue;
    }

    if (dbType == "oracle") {
        sql << " ORDER BY created_at DESC"
            << " OFFSET $" << paramIndex << " ROWS";
        paramIndex++;
        sql << " FETCH NEXT $" << paramIndex << " ROWS ONLY";
        paramIndex++;
        paramValues.push_back(std::to_string(offset));
        paramValues.push_back(std::to_string(limit));
    } else {
        sql << " ORDER BY created_at DESC LIMIT $" << paramIndex;
        paramIndex++;
        sql << " OFFSET $" << paramIndex;
        paramIndex++;
        paramValues.push_back(std::to_string(limit));
        paramValues.push_back(std::to_string(offset));
    }

    return queryExecutor_->executeQuery(sql.str(), paramValues);
}

// ============================================================================
// Phase 2-5: Link Certificate Detail by ID
// ============================================================================

Json::Value CertificateRepository::findLinkCertificateById(const std::string& id) {
    std::string query =
        "SELECT id, subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
        "old_csca_subject_dn, old_csca_fingerprint, "
        "new_csca_subject_dn, new_csca_fingerprint, "
        "trust_chain_valid, old_csca_signature_valid, new_csca_signature_valid, "
        "validity_period_valid, not_before, not_after, "
        "extensions_valid, basic_constraints_ca, basic_constraints_pathlen, "
        "key_usage, extended_key_usage, "
        "revocation_status, revocation_message, "
        "ldap_dn_v2, stored_in_ldap, created_at, country_code "
        "FROM link_certificate WHERE id = $1";

    auto rows = queryExecutor_->executeQuery(query, {id});

    if (rows.empty()) {
        return Json::Value::null;
    }
    return rows[0];
}

// ============================================================================
// Bulk Export (All LDAP-stored certificates)
// ============================================================================

Json::Value CertificateRepository::findAllForExport() {
    std::string dbType = queryExecutor_->getDatabaseType();
    std::string storedFlag = (dbType == "oracle") ? "1" : "TRUE";

    std::string query =
        "SELECT certificate_type, country_code, subject_dn, serial_number, "
        "fingerprint_sha256, certificate_data, is_self_signed "
        "FROM certificate WHERE stored_in_ldap = " + storedFlag + " "
        "ORDER BY country_code, certificate_type";

    return queryExecutor_->executeQuery(query);
}

} // namespace repositories

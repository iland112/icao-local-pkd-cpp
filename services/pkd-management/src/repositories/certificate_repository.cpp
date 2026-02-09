#include "certificate_repository.h"
#include "../common/x509_metadata_extractor.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <openssl/x509.h>
#include <openssl/err.h>

namespace repositories {

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

        Json::Value result = queryExecutor_->executeQuery(query);

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

        Json::Value rows = queryExecutor_->executeQuery(query);

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

    // Parse bytea hex format (PostgreSQL escape format: \x...)
    std::vector<uint8_t> derBytes;
    if (hexData.size() > 2 && hexData[0] == '\\' && hexData[1] == 'x') {
        // Hex encoded
        for (size_t i = 2; i + 1 < hexData.size(); i += 2) {
            char hex[3] = {hexData[i], hexData[i + 1], 0};
            derBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
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
            std::ostringstream kuStream, ekuStream, crlStream;
            for (size_t i = 0; i < x509meta.keyUsage.size(); i++) {
                kuStream << x509meta.keyUsage[i];
                if (i < x509meta.keyUsage.size() - 1) kuStream << ",";
            }
            keyUsageStr = kuStream.str();

            for (size_t i = 0; i < x509meta.extendedKeyUsage.size(); i++) {
                ekuStream << x509meta.extendedKeyUsage[i];
                if (i < x509meta.extendedKeyUsage.size() - 1) ekuStream << ",";
            }
            extKeyUsageStr = ekuStream.str();

            for (size_t i = 0; i < x509meta.crlDistributionPoints.size(); i++) {
                crlStream << x509meta.crlDistributionPoints[i];
                if (i < x509meta.crlDistributionPoints.size() - 1) crlStream << ",";
            }
            crlDpStr = crlStream.str();

            // Database-aware boolean formatting
            std::string dbType = queryExecutor_->getDatabaseType();
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

        // Convert DER bytes to hex string (database-agnostic)
        std::ostringstream hexStream;
        hexStream << "\\\\x";  // PostgreSQL bytea format prefix (Oracle will handle differently)
        for (size_t i = 0; i < certData.size(); i++) {
            hexStream << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(certData[i]);
        }
        std::string certDataHex = hexStream.str();

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
            "$16, $17, $18, "
            "$19, $20, "
            "$21, $22, "
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

        std::string newId = insertResult[0]["id"].asString();

        spdlog::debug("[CertificateRepository] New certificate inserted: id={}, type={}, country={}, fingerprint={}",
                     newId.substr(0, 8) + "...", certType, countryCode, fingerprint.substr(0, 16) + "...");

        return std::make_pair(newId, false);

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] saveCertificateWithDuplicateCheck failed: {}", e.what());
        return std::make_pair(std::string(""), false);
    }
}

} // namespace repositories

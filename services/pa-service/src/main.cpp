/**
 * @file main.cpp
 * @brief Passive Authentication Service - ICAO 9303 PA Verification
 *
 * C++ REST API based Passive Authentication Service.
 * Implements full ICAO 9303 PA verification including:
 * - SOD parsing (CMS SignedData)
 * - DSC extraction and Trust Chain validation
 * - SOD signature verification
 * - Data Group hash verification
 * - CRL checking
 *
 * @author SmartCore Inc.
 * @date 2026-01-01
 * @version 2.0.0
 */

#include <drogon/drogon.h>
#include <trantor/utils/Date.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <iostream>
#include <memory>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <array>
#include <thread>
#include <future>
#include <algorithm>
#include <cctype>

// PostgreSQL header for direct connection
#include <libpq-fe.h>

// OpenLDAP header
#include <ldap.h>

// OpenSSL for SOD parsing and certificate verification
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/pkcs7.h>
#include <openssl/cms.h>
#include <openssl/objects.h>
#include <iomanip>
#include <sstream>
#include <random>
#include <map>
#include <set>
#include <optional>
#include <regex>

namespace {

// =============================================================================
// Algorithm OID Mappings (matching Java implementation)
// =============================================================================

const std::map<std::string, std::string> HASH_ALGORITHM_NAMES = {
    {"1.3.14.3.2.26", "SHA-1"},
    {"2.16.840.1.101.3.4.2.1", "SHA-256"},
    {"2.16.840.1.101.3.4.2.2", "SHA-384"},
    {"2.16.840.1.101.3.4.2.3", "SHA-512"}
};

const std::map<std::string, std::string> SIGNATURE_ALGORITHM_NAMES = {
    {"1.2.840.113549.1.1.11", "SHA256withRSA"},
    {"1.2.840.113549.1.1.12", "SHA384withRSA"},
    {"1.2.840.113549.1.1.13", "SHA512withRSA"},
    {"1.2.840.10045.4.3.2", "SHA256withECDSA"},
    {"1.2.840.10045.4.3.3", "SHA384withECDSA"},
    {"1.2.840.10045.4.3.4", "SHA512withECDSA"}
};

// =============================================================================
// CRL Status Enum (matching Java implementation)
// =============================================================================

enum class CrlStatus {
    VALID,
    REVOKED,
    CRL_UNAVAILABLE,
    CRL_EXPIRED,
    CRL_INVALID,
    NOT_CHECKED
};

std::string crlStatusToString(CrlStatus status) {
    switch (status) {
        case CrlStatus::VALID: return "VALID";
        case CrlStatus::REVOKED: return "REVOKED";
        case CrlStatus::CRL_UNAVAILABLE: return "CRL_UNAVAILABLE";
        case CrlStatus::CRL_EXPIRED: return "CRL_EXPIRED";
        case CrlStatus::CRL_INVALID: return "CRL_INVALID";
        case CrlStatus::NOT_CHECKED: return "NOT_CHECKED";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// Result Structures (matching Java DTOs)
// =============================================================================

struct CertificateChainValidationResult {
    bool valid = false;
    std::string dscSubject;
    std::string dscSerialNumber;
    std::string cscaSubject;
    std::string cscaSerialNumber;
    std::string notBefore;
    std::string notAfter;
    bool crlChecked = false;
    bool revoked = false;
    CrlStatus crlStatus = CrlStatus::NOT_CHECKED;
    std::string crlStatusDescription;
    std::string crlStatusDetailedDescription;
    std::string crlStatusSeverity;
    std::string crlMessage;
    std::string validationErrors;
};

struct SodSignatureValidationResult {
    bool valid = false;
    std::string signatureAlgorithm;
    std::string hashAlgorithm;
    std::string validationErrors;
};

struct DataGroupDetailResult {
    bool valid = false;
    std::string expectedHash;
    std::string actualHash;
};

struct DataGroupValidationResult {
    int totalGroups = 0;
    int validGroups = 0;
    int invalidGroups = 0;
    std::map<std::string, DataGroupDetailResult> details;
};

struct CrlCheckResult {
    CrlStatus status = CrlStatus::NOT_CHECKED;
    bool revoked = false;
    std::string revocationDate;
    std::string revocationReason;
    std::string errorMessage;
};

struct PassiveAuthenticationError {
    std::string code;
    std::string message;
    std::string severity;  // CRITICAL, WARNING, INFO
    std::string timestamp;
};

// =============================================================================
// Application Configuration
// =============================================================================

struct AppConfig {
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "localpkd";
    std::string dbUser = "localpkd";
    std::string dbPassword = "localpkd123";

    // LDAP Read: HAProxy for load balancing
    std::string ldapHost = "haproxy";
    int ldapPort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword = "admin";
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    int serverPort = 8082;
    int threadNum = 4;

    static AppConfig fromEnvironment() {
        AppConfig config;

        if (auto val = std::getenv("DB_HOST")) config.dbHost = val;
        if (auto val = std::getenv("DB_PORT")) config.dbPort = std::stoi(val);
        if (auto val = std::getenv("DB_NAME")) config.dbName = val;
        if (auto val = std::getenv("DB_USER")) config.dbUser = val;
        if (auto val = std::getenv("DB_PASSWORD")) config.dbPassword = val;

        if (auto val = std::getenv("LDAP_HOST")) config.ldapHost = val;
        if (auto val = std::getenv("LDAP_PORT")) config.ldapPort = std::stoi(val);
        if (auto val = std::getenv("LDAP_BIND_DN")) config.ldapBindDn = val;
        if (auto val = std::getenv("LDAP_BIND_PASSWORD")) config.ldapBindPassword = val;
        if (auto val = std::getenv("LDAP_BASE_DN")) config.ldapBaseDn = val;

        if (auto val = std::getenv("SERVER_PORT")) config.serverPort = std::stoi(val);
        if (auto val = std::getenv("THREAD_NUM")) config.threadNum = std::stoi(val);

        return config;
    }
};

AppConfig appConfig;

// =============================================================================
// Utility Functions
// =============================================================================

std::string generateUuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);

    return ss.str();
}

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::string bytesToHex(const unsigned char* bytes, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; i++) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return ss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::vector<uint8_t> base64Decode(const std::string& encoded) {
    BIO* bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    std::vector<uint8_t> decoded(encoded.size());
    int len = BIO_read(bio, decoded.data(), static_cast<int>(decoded.size()));
    BIO_free_all(bio);

    if (len > 0) {
        decoded.resize(len);
    } else {
        decoded.clear();
    }
    return decoded;
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    BIO_write(bio, data.data(), static_cast<int>(data.size()));
    BIO_flush(bio);

    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string encoded(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return encoded;
}

std::string toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return str.substr(start, end - start + 1);
}

// =============================================================================
// X509 Helper Functions
// =============================================================================

std::string getX509SubjectDn(X509* cert) {
    if (!cert) return "";
    char* dn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

std::string getX509IssuerDn(X509* cert) {
    if (!cert) return "";
    char* dn = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

std::string getX509SerialNumber(X509* cert) {
    if (!cert) return "";
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    char* hex = BN_bn2hex(bn);
    std::string result(hex);
    OPENSSL_free(hex);
    BN_free(bn);
    return result;
}

std::string getX509NotBefore(X509* cert) {
    if (!cert) return "";
    const ASN1_TIME* time = X509_get0_notBefore(cert);
    struct tm t;
    ASN1_TIME_to_tm(time, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return std::string(buf);
}

std::string getX509NotAfter(X509* cert) {
    if (!cert) return "";
    const ASN1_TIME* time = X509_get0_notAfter(cert);
    struct tm t;
    ASN1_TIME_to_tm(time, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return std::string(buf);
}

std::string extractCountryFromDn(const std::string& dn) {
    std::regex countryRegex("C=([A-Z]{2,3})", std::regex::icase);
    std::smatch match;
    if (std::regex_search(dn, match, countryRegex)) {
        return toUpper(match[1].str());
    }
    return "";
}

std::string extractCnFromDn(const std::string& dn) {
    std::regex cnRegex("CN=([^,/]+)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(dn, match, cnRegex)) {
        return match[1].str();
    }
    return dn;
}

// =============================================================================
// Logging Initialization
// =============================================================================

void printBanner() {
    std::cout << R"(
  ____   _      ____                  _
 |  _ \ / \    / ___|  ___ _ ____   _(_) ___ ___
 | |_) / _ \   \___ \ / _ \ '__\ \ / / |/ __/ _ \
 |  __/ ___ \   ___) |  __/ |   \ V /| | (_|  __/
 |_| /_/   \_\ |____/ \___|_|    \_/ |_|\___\___|

)" << std::endl;
    std::cout << "  PA Service - ICAO Passive Authentication" << std::endl;
    std::cout << "  Version: 2.0.0" << std::endl;
    std::cout << "  (C) 2026 SmartCore Inc." << std::endl;
    std::cout << std::endl;
}

void initializeLogging() {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/pa-service.log", 1024 * 1024 * 10, 5);
        file_sink->set_level(spdlog::level::info);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

        auto logger = std::make_shared<spdlog::logger>("multi_sink",
            spdlog::sinks_init_list{console_sink, file_sink});
        logger->set_level(spdlog::level::debug);

        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(3));

        spdlog::info("Logging initialized");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log init failed: " << ex.what() << std::endl;
    }
}

// =============================================================================
// Database Health Check
// =============================================================================

Json::Value checkDatabase() {
    Json::Value result;
    result["name"] = "database";

    auto start = std::chrono::steady_clock::now();

    std::string conninfo = "host=" + appConfig.dbHost +
                          " port=" + std::to_string(appConfig.dbPort) +
                          " dbname=" + appConfig.dbName +
                          " user=" + appConfig.dbUser +
                          " password=" + appConfig.dbPassword +
                          " connect_timeout=5";

    PGconn* conn = PQconnectdb(conninfo.c_str());

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (PQstatus(conn) == CONNECTION_OK) {
        result["status"] = "UP";
        result["responseTimeMs"] = static_cast<int>(duration.count());

        PGresult* res = PQexec(conn, "SELECT version()");
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            result["version"] = PQgetvalue(res, 0, 0);
        }
        PQclear(res);
    } else {
        result["status"] = "DOWN";
        result["error"] = PQerrorMessage(conn);
    }

    PQfinish(conn);
    return result;
}

// =============================================================================
// LDAP Functions
// =============================================================================

Json::Value checkLdap() {
    Json::Value result;
    result["name"] = "ldap";

    auto start = std::chrono::steady_clock::now();

    std::string ldapUri = "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort);
    std::string cmd = "ldapsearch -x -H " + ldapUri +
                      " -D '" + appConfig.ldapBindDn + "'" +
                      " -w '" + appConfig.ldapBindPassword + "'" +
                      " -b '' -s base '(objectClass=*)' namingContexts 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result["status"] = "DOWN";
        result["error"] = "Failed to execute ldapsearch";
        return result;
    }

    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int exitCode = pclose(pipe);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (exitCode == 0 && output.find("namingContexts") != std::string::npos) {
        result["status"] = "UP";
        result["responseTimeMs"] = static_cast<int>(duration.count());
        result["uri"] = ldapUri;
    } else {
        result["status"] = "DOWN";
        result["error"] = "LDAP connection failed";
    }

    return result;
}

LDAP* getLdapConnection() {
    std::string ldapUri = "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort);

    LDAP* ld = nullptr;
    int rc = ldap_initialize(&ld, ldapUri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    struct berval cred;
    cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
    cred.bv_len = appConfig.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    return ld;
}

// =============================================================================
// SOD Parsing Functions (OpenSSL CMS API)
// =============================================================================

/**
 * @brief Unwrap ICAO Tag 0x77 wrapper from SOD if present
 */
std::vector<uint8_t> unwrapIcaoSod(const std::vector<uint8_t>& sodBytes) {
    if (sodBytes.size() < 4) {
        return sodBytes;
    }

    // Check for ICAO Tag 0x77 (Application 23)
    if (sodBytes[0] == 0x77) {
        // Parse TLV to get content
        size_t pos = 1;
        size_t length = 0;

        if (sodBytes[pos] & 0x80) {
            int numLengthBytes = sodBytes[pos] & 0x7F;
            pos++;
            for (int i = 0; i < numLengthBytes && pos < sodBytes.size(); i++) {
                length = (length << 8) | sodBytes[pos++];
            }
        } else {
            length = sodBytes[pos++];
        }

        if (pos + length <= sodBytes.size()) {
            spdlog::debug("Unwrapped ICAO Tag 0x77: {} bytes -> {} bytes", sodBytes.size(), length);
            return std::vector<uint8_t>(sodBytes.begin() + pos, sodBytes.begin() + pos + length);
        }
    }

    // Already CMS data (starts with SEQUENCE tag 0x30)
    return sodBytes;
}

/**
 * @brief Extract DSC certificate from SOD (CMS SignedData)
 */
X509* extractDscFromSod(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) {
        spdlog::error("Failed to create BIO for SOD");
        return nullptr;
    }

    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("Failed to parse CMS from SOD: {}", ERR_error_string(ERR_get_error(), nullptr));
        return nullptr;
    }

    STACK_OF(X509)* certs = CMS_get1_certs(cms);
    if (!certs || sk_X509_num(certs) == 0) {
        spdlog::error("No certificates found in SOD");
        CMS_ContentInfo_free(cms);
        return nullptr;
    }

    // Get first certificate (DSC)
    X509* dscCert = X509_dup(sk_X509_value(certs, 0));

    sk_X509_pop_free(certs, X509_free);
    CMS_ContentInfo_free(cms);

    if (dscCert) {
        spdlog::info("Extracted DSC from SOD - Subject: {}, Serial: {}",
            getX509SubjectDn(dscCert), getX509SerialNumber(dscCert));
    }

    return dscCert;
}

/**
 * @brief Extract hash algorithm OID from SOD
 */
std::string extractHashAlgorithmOid(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        return "";
    }

    // Get digest algorithms from CMS
    STACK_OF(X509_ALGOR)* digestAlgos = CMS_get0_SignerInfos(cms) ?
        nullptr : nullptr;  // Alternative approach needed

    // Get from SignerInfo
    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
        CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
        X509_ALGOR* digestAlg = nullptr;
        X509_ALGOR* signatureAlg = nullptr;
        CMS_SignerInfo_get0_algs(si, nullptr, nullptr, &digestAlg, &signatureAlg);

        if (digestAlg) {
            const ASN1_OBJECT* obj = nullptr;
            X509_ALGOR_get0(&obj, nullptr, nullptr, digestAlg);
            char oidBuf[80];
            OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
            CMS_ContentInfo_free(cms);
            return std::string(oidBuf);
        }
    }

    CMS_ContentInfo_free(cms);
    return "";
}

/**
 * @brief Extract hash algorithm name from SOD
 */
std::string extractHashAlgorithm(const std::vector<uint8_t>& sodBytes) {
    std::string oid = extractHashAlgorithmOid(sodBytes);
    auto it = HASH_ALGORITHM_NAMES.find(oid);
    if (it != HASH_ALGORITHM_NAMES.end()) {
        return it->second;
    }
    // Default to SHA-256 if not found
    return "SHA-256";
}

/**
 * @brief Extract signature algorithm name from SOD
 */
std::string extractSignatureAlgorithm(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        return "UNKNOWN";
    }

    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
        CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
        X509_ALGOR* digestAlg = nullptr;
        X509_ALGOR* signatureAlg = nullptr;
        CMS_SignerInfo_get0_algs(si, nullptr, nullptr, &digestAlg, &signatureAlg);

        if (signatureAlg) {
            const ASN1_OBJECT* obj = nullptr;
            X509_ALGOR_get0(&obj, nullptr, nullptr, signatureAlg);
            char oidBuf[80];
            OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);

            auto it = SIGNATURE_ALGORITHM_NAMES.find(std::string(oidBuf));
            if (it != SIGNATURE_ALGORITHM_NAMES.end()) {
                CMS_ContentInfo_free(cms);
                return it->second;
            }

            // Try to derive from digest + encryption algorithms
            std::string hashAlg = extractHashAlgorithm(sodBytes);
            if (hashAlg == "SHA-256") {
                CMS_ContentInfo_free(cms);
                return "SHA256withRSA";
            }
        }
    }

    CMS_ContentInfo_free(cms);
    return "SHA256withRSA";  // Default
}

/**
 * @brief Parse Data Group hashes from SOD (LDSSecurityObject)
 *
 * LDSSecurityObject ::= SEQUENCE {
 *   version INTEGER,
 *   hashAlgorithm AlgorithmIdentifier,
 *   dataGroupHashValues SEQUENCE OF DataGroupHash
 * }
 *
 * DataGroupHash ::= SEQUENCE {
 *   dataGroupNumber INTEGER,
 *   dataGroupHashValue OCTET STRING
 * }
 */
std::map<int, std::vector<uint8_t>> parseDataGroupHashes(const std::vector<uint8_t>& sodBytes) {
    std::map<int, std::vector<uint8_t>> result;

    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("Failed to parse CMS for DG hashes");
        return result;
    }

    // Get encapsulated content (LDSSecurityObject)
    ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
    if (!contentPtr || !*contentPtr) {
        spdlog::error("No encapsulated content in CMS");
        CMS_ContentInfo_free(cms);
        return result;
    }

    const unsigned char* p = ASN1_STRING_get0_data(*contentPtr);
    long len = ASN1_STRING_length(*contentPtr);

    // Parse LDSSecurityObject ASN.1
    const unsigned char* contentData = p;

    // Skip outer SEQUENCE tag and length
    if (*contentData != 0x30) {
        spdlog::error("Expected SEQUENCE tag for LDSSecurityObject");
        CMS_ContentInfo_free(cms);
        return result;
    }
    contentData++;

    // Parse length
    size_t contentLen = 0;
    if (*contentData & 0x80) {
        int numBytes = *contentData & 0x7F;
        contentData++;
        for (int i = 0; i < numBytes; i++) {
            contentLen = (contentLen << 8) | *contentData++;
        }
    } else {
        contentLen = *contentData++;
    }

    // Skip version (INTEGER)
    if (*contentData == 0x02) {
        contentData++;
        size_t versionLen = *contentData++;
        contentData += versionLen;
    }

    // Skip hashAlgorithm (SEQUENCE - AlgorithmIdentifier)
    if (*contentData == 0x30) {
        contentData++;
        size_t algLen = 0;
        if (*contentData & 0x80) {
            int numBytes = *contentData & 0x7F;
            contentData++;
            for (int i = 0; i < numBytes; i++) {
                algLen = (algLen << 8) | *contentData++;
            }
        } else {
            algLen = *contentData++;
        }
        contentData += algLen;
    }

    // Parse dataGroupHashValues (SEQUENCE OF DataGroupHash)
    if (*contentData == 0x30) {
        contentData++;
        size_t dgHashesLen = 0;
        if (*contentData & 0x80) {
            int numBytes = *contentData & 0x7F;
            contentData++;
            for (int i = 0; i < numBytes; i++) {
                dgHashesLen = (dgHashesLen << 8) | *contentData++;
            }
        } else {
            dgHashesLen = *contentData++;
        }

        const unsigned char* dgHashesEnd = contentData + dgHashesLen;

        // Parse each DataGroupHash
        while (contentData < dgHashesEnd) {
            if (*contentData != 0x30) break;
            contentData++;

            size_t dgHashLen = 0;
            if (*contentData & 0x80) {
                int numBytes = *contentData & 0x7F;
                contentData++;
                for (int i = 0; i < numBytes; i++) {
                    dgHashLen = (dgHashLen << 8) | *contentData++;
                }
            } else {
                dgHashLen = *contentData++;
            }

            const unsigned char* dgHashEnd = contentData + dgHashLen;

            // Parse dataGroupNumber (INTEGER)
            int dgNumber = 0;
            if (*contentData == 0x02) {
                contentData++;
                size_t intLen = *contentData++;
                for (size_t i = 0; i < intLen; i++) {
                    dgNumber = (dgNumber << 8) | *contentData++;
                }
            }

            // Parse dataGroupHashValue (OCTET STRING)
            if (*contentData == 0x04) {
                contentData++;
                size_t hashLen = 0;
                if (*contentData & 0x80) {
                    int numBytes = *contentData & 0x7F;
                    contentData++;
                    for (int i = 0; i < numBytes; i++) {
                        hashLen = (hashLen << 8) | *contentData++;
                    }
                } else {
                    hashLen = *contentData++;
                }

                std::vector<uint8_t> hashValue(contentData, contentData + hashLen);
                result[dgNumber] = hashValue;
                contentData += hashLen;

                spdlog::debug("Parsed DG{} hash: {} bytes", dgNumber, hashLen);
            }

            contentData = dgHashEnd;
        }
    }

    CMS_ContentInfo_free(cms);
    spdlog::info("Parsed {} Data Group hashes from SOD", result.size());
    return result;
}

// =============================================================================
// Hash Calculation Functions
// =============================================================================

std::vector<uint8_t> calculateHash(const std::vector<uint8_t>& data, const std::string& algorithm) {
    const EVP_MD* md = nullptr;

    if (algorithm == "SHA-256" || algorithm == "SHA256") {
        md = EVP_sha256();
    } else if (algorithm == "SHA-384" || algorithm == "SHA384") {
        md = EVP_sha384();
    } else if (algorithm == "SHA-512" || algorithm == "SHA512") {
        md = EVP_sha512();
    } else if (algorithm == "SHA-1" || algorithm == "SHA1") {
        md = EVP_sha1();
    } else {
        // Default to SHA-256
        md = EVP_sha256();
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    std::vector<uint8_t> hash(EVP_MAX_MD_SIZE);
    unsigned int hashLen = 0;

    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash.data(), &hashLen);
    EVP_MD_CTX_free(ctx);

    hash.resize(hashLen);
    return hash;
}

// =============================================================================
// LDAP CSCA Lookup Functions
// =============================================================================

/**
 * @brief Retrieve CSCA certificate from LDAP by issuer DN
 */
X509* retrieveCscaFromLdap(LDAP* ld, const std::string& issuerDn) {
    if (!ld) return nullptr;

    // Extract country code from issuer DN
    std::string countryCode = extractCountryFromDn(issuerDn);
    if (countryCode.empty()) {
        spdlog::warn("Could not extract country code from issuer DN: {}", issuerDn);
        return nullptr;
    }

    // Build LDAP search base for CSCA
    std::string baseDn = "o=csca,c=" + countryCode + ",dc=data,dc=download," + appConfig.ldapBaseDn;
    std::string filter = "(objectClass=pkdDownload)";
    char* attrs[] = {const_cast<char*>("userCertificate;binary"), nullptr};

    spdlog::debug("Searching CSCA in LDAP: base={}, filter={}", baseDn, filter);

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(ld, baseDn.c_str(), LDAP_SCOPE_SUBTREE, filter.c_str(),
                               attrs, 0, nullptr, nullptr, nullptr, 100, &res);

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("LDAP search failed: {}", ldap_err2string(rc));
        if (res) ldap_msgfree(res);
        return nullptr;
    }

    X509* cscaCert = nullptr;
    X509* fallbackCsca = nullptr;
    std::string issuerCn = extractCnFromDn(issuerDn);
    std::string issuerCnLower = toLower(issuerCn);

    spdlog::debug("Looking for CSCA matching issuer CN: {}", issuerCn);

    // Iterate through all results to find matching CSCA
    LDAPMessage* entry = ldap_first_entry(ld, res);
    while (entry) {
        struct berval** values = ldap_get_values_len(ld, entry, "userCertificate;binary");
        if (values && values[0]) {
            const unsigned char* certData = reinterpret_cast<const unsigned char*>(values[0]->bv_val);
            X509* cert = d2i_X509(nullptr, &certData, values[0]->bv_len);

            if (cert) {
                std::string certSubject = getX509SubjectDn(cert);
                std::string certCn = extractCnFromDn(certSubject);
                std::string certCnLower = toLower(certCn);

                spdlog::debug("Checking CSCA: {} (CN={})", certSubject, certCn);

                // Exact CN match (case-insensitive)
                if (issuerCnLower == certCnLower) {
                    if (cscaCert) X509_free(cscaCert);
                    cscaCert = cert;
                    spdlog::info("Found exact matching CSCA: {}", certSubject);
                    ldap_value_free_len(values);
                    break;  // Found exact match, stop searching
                }
                // Partial match - issuer CN contains CSCA CN or vice versa
                else if (issuerCnLower.find(certCnLower) != std::string::npos ||
                         certCnLower.find(issuerCnLower) != std::string::npos) {
                    if (!cscaCert) {
                        cscaCert = cert;
                        spdlog::info("Found partial matching CSCA: {}", certSubject);
                    } else {
                        X509_free(cert);
                    }
                }
                // Keep first as fallback
                else if (!fallbackCsca) {
                    fallbackCsca = cert;
                    spdlog::debug("Keeping as fallback CSCA: {}", certSubject);
                } else {
                    X509_free(cert);
                }
            }

            ldap_value_free_len(values);
        }
        entry = ldap_next_entry(ld, entry);
    }

    ldap_msgfree(res);

    // Return matched CSCA, or fallback if no match found
    if (cscaCert) {
        if (fallbackCsca) X509_free(fallbackCsca);
        return cscaCert;
    }

    if (fallbackCsca) {
        spdlog::warn("No exact CSCA match found for issuer CN '{}', using fallback", issuerCn);
        return fallbackCsca;
    }

    spdlog::warn("No CSCA found for issuer: {}", issuerDn);
    return nullptr;
}

/**
 * @brief Search CRL from LDAP for a given CSCA
 */
X509_CRL* searchCrlFromLdap(LDAP* ld, const std::string& countryCode) {
    if (!ld) return nullptr;

    std::string baseDn = "o=crl,c=" + countryCode + ",dc=data,dc=download," + appConfig.ldapBaseDn;
    std::string filter = "(objectClass=pkdDownload)";
    char* attrs[] = {const_cast<char*>("certificateRevocationList;binary"), nullptr};

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(ld, baseDn.c_str(), LDAP_SCOPE_SUBTREE, filter.c_str(),
                               attrs, 0, nullptr, nullptr, nullptr, 10, &res);

    if (rc != LDAP_SUCCESS) {
        spdlog::debug("CRL search failed: {}", ldap_err2string(rc));
        if (res) ldap_msgfree(res);
        return nullptr;
    }

    X509_CRL* crl = nullptr;
    LDAPMessage* entry = ldap_first_entry(ld, res);
    if (entry) {
        struct berval** values = ldap_get_values_len(ld, entry, "certificateRevocationList;binary");
        if (values && values[0]) {
            const unsigned char* crlData = reinterpret_cast<const unsigned char*>(values[0]->bv_val);
            crl = d2i_X509_CRL(nullptr, &crlData, values[0]->bv_len);
            ldap_value_free_len(values);
        }
    }

    ldap_msgfree(res);
    return crl;
}

// =============================================================================
// Verification Functions
// =============================================================================

/**
 * @brief Verify SOD signature using DSC certificate
 */
SodSignatureValidationResult validateSodSignature(
    const std::vector<uint8_t>& sodBytes, X509* dscCert) {

    SodSignatureValidationResult result;
    result.hashAlgorithm = extractHashAlgorithm(sodBytes);
    result.signatureAlgorithm = extractSignatureAlgorithm(sodBytes);

    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        result.valid = false;
        result.validationErrors = "Failed to parse CMS structure";
        return result;
    }

    // Create certificate store with DSC
    X509_STORE* store = X509_STORE_new();
    STACK_OF(X509)* certs = sk_X509_new_null();
    sk_X509_push(certs, dscCert);

    // Verify signature
    int verifyResult = CMS_verify(cms, certs, store, nullptr, nullptr,
                                   CMS_NO_SIGNER_CERT_VERIFY | CMS_NO_ATTR_VERIFY);

    result.valid = (verifyResult == 1);

    if (!result.valid) {
        unsigned long err = ERR_get_error();
        result.validationErrors = ERR_error_string(err, nullptr);
        spdlog::warn("SOD signature verification failed: {}", result.validationErrors);
    } else {
        spdlog::info("SOD signature verification succeeded");
    }

    sk_X509_free(certs);
    X509_STORE_free(store);
    CMS_ContentInfo_free(cms);

    return result;
}

/**
 * @brief Validate certificate chain (DSC -> CSCA)
 */
CertificateChainValidationResult validateCertificateChain(
    X509* dscCert, X509* cscaCert, const std::string& countryCode, LDAP* ld) {

    CertificateChainValidationResult result;

    if (!dscCert) {
        result.valid = false;
        result.validationErrors = "DSC certificate is null";
        return result;
    }

    // Extract DSC info
    result.dscSubject = getX509SubjectDn(dscCert);
    result.dscSerialNumber = getX509SerialNumber(dscCert);
    result.notBefore = getX509NotBefore(dscCert);
    result.notAfter = getX509NotAfter(dscCert);

    if (!cscaCert) {
        result.valid = false;
        result.validationErrors = "CSCA certificate not found in LDAP";
        result.crlStatus = CrlStatus::NOT_CHECKED;
        result.crlStatusDescription = "CSCA not available";
        result.crlStatusDetailedDescription = "LDAP에서 해당 국가의 CSCA를 찾을 수 없음";
        result.crlStatusSeverity = "FAILURE";
        return result;
    }

    // Extract CSCA info
    result.cscaSubject = getX509SubjectDn(cscaCert);
    result.cscaSerialNumber = getX509SerialNumber(cscaCert);

    // Verify DSC signature with CSCA public key
    EVP_PKEY* cscaPubKey = X509_get_pubkey(cscaCert);
    if (!cscaPubKey) {
        result.valid = false;
        result.validationErrors = "Failed to extract CSCA public key";
        return result;
    }

    int verifyResult = X509_verify(dscCert, cscaPubKey);
    EVP_PKEY_free(cscaPubKey);

    if (verifyResult != 1) {
        result.valid = false;
        result.validationErrors = "DSC signature verification with CSCA failed";
        spdlog::warn("Trust chain validation failed: DSC not signed by CSCA");
    } else {
        result.valid = true;
        spdlog::info("Trust chain validation passed: DSC verified with CSCA public key");
    }

    // CRL Check
    X509_CRL* crl = searchCrlFromLdap(ld, countryCode);
    if (crl) {
        result.crlChecked = true;

        // Check if DSC is in CRL
        X509_REVOKED* revoked = nullptr;
        int crlResult = X509_CRL_get0_by_cert(crl, &revoked, dscCert);

        if (crlResult == 1 && revoked) {
            result.revoked = true;
            result.valid = false;
            result.crlStatus = CrlStatus::REVOKED;
            result.crlStatusDescription = "Certificate is revoked";
            result.crlStatusDetailedDescription = "인증서가 폐기됨";
            result.crlStatusSeverity = "FAILURE";

            // Get revocation date
            const ASN1_TIME* revTime = X509_REVOKED_get0_revocationDate(revoked);
            if (revTime) {
                struct tm t;
                ASN1_TIME_to_tm(revTime, &t);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
                result.crlMessage = std::string("Certificate revoked on ") + buf;
            }

            spdlog::warn("DSC certificate is REVOKED");
        } else {
            result.revoked = false;
            result.crlStatus = CrlStatus::VALID;
            result.crlStatusDescription = "Certificate is not revoked";
            result.crlStatusDetailedDescription = "CRL 확인 완료 - DSC 인증서가 폐기되지 않음";
            result.crlStatusSeverity = "SUCCESS";
            result.crlMessage = "CRL 확인 완료 - DSC 인증서가 폐기되지 않음";
            spdlog::info("CRL check passed: DSC not revoked");
        }

        X509_CRL_free(crl);
    } else {
        result.crlChecked = false;
        result.crlStatus = CrlStatus::CRL_UNAVAILABLE;
        result.crlStatusDescription = "CRL not available";
        result.crlStatusDetailedDescription = "LDAP에서 해당 CSCA의 CRL을 찾을 수 없음";
        result.crlStatusSeverity = "WARNING";
        result.crlMessage = "LDAP에서 CRL을 찾을 수 없음 (국가: " + countryCode + ")";
        spdlog::debug("CRL not available for country: {}", countryCode);
    }

    return result;
}

/**
 * @brief Validate Data Group hashes
 */
DataGroupValidationResult validateDataGroupHashes(
    const std::map<int, std::vector<uint8_t>>& dataGroups,
    const std::map<int, std::vector<uint8_t>>& expectedHashes,
    const std::string& hashAlgorithm) {

    DataGroupValidationResult result;
    result.totalGroups = static_cast<int>(dataGroups.size());
    result.validGroups = 0;
    result.invalidGroups = 0;

    for (const auto& [dgNum, dgContent] : dataGroups) {
        std::string dgKey = "DG" + std::to_string(dgNum);
        DataGroupDetailResult detail;

        auto expectedIt = expectedHashes.find(dgNum);
        if (expectedIt == expectedHashes.end()) {
            detail.valid = false;
            detail.expectedHash = "";
            detail.actualHash = bytesToHex(calculateHash(dgContent, hashAlgorithm));
            result.invalidGroups++;
            spdlog::warn("No expected hash found in SOD for DG{}", dgNum);
        } else {
            std::vector<uint8_t> actualHash = calculateHash(dgContent, hashAlgorithm);
            detail.expectedHash = bytesToHex(expectedIt->second);
            detail.actualHash = bytesToHex(actualHash);
            detail.valid = (actualHash == expectedIt->second);

            if (detail.valid) {
                result.validGroups++;
                spdlog::debug("DG{} hash validation passed", dgNum);
            } else {
                result.invalidGroups++;
                spdlog::warn("DG{} hash mismatch - Expected: {}, Actual: {}",
                    dgNum, detail.expectedHash, detail.actualHash);
            }
        }

        result.details[dgKey] = detail;
    }

    spdlog::info("Data Group validation completed - Valid: {}, Invalid: {}",
        result.validGroups, result.invalidGroups);

    return result;
}

// =============================================================================
// Database Functions
// =============================================================================

PGconn* getDbConnection() {
    std::string conninfo = "host=" + appConfig.dbHost +
                          " port=" + std::to_string(appConfig.dbPort) +
                          " dbname=" + appConfig.dbName +
                          " user=" + appConfig.dbUser +
                          " password=" + appConfig.dbPassword;

    PGconn* conn = PQconnectdb(conninfo.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("Database connection failed: {}", PQerrorMessage(conn));
        PQfinish(conn);
        return nullptr;
    }
    return conn;
}

std::string savePaVerification(
    PGconn* conn,
    const std::string& verificationId,
    const std::string& status,
    const std::string& countryCode,
    const std::string& documentNumber,
    const std::vector<uint8_t>& sodBytes,
    const CertificateChainValidationResult& chainResult,
    const SodSignatureValidationResult& sodResult,
    const DataGroupValidationResult& dgResult,
    int processingTimeMs) {

    if (!conn) return "";

    // Escape SOD bytes for bytea
    size_t escapedLen = 0;
    unsigned char* escaped = PQescapeByteaConn(conn, sodBytes.data(), sodBytes.size(), &escapedLen);
    std::string sodEscaped(reinterpret_cast<char*>(escaped), escapedLen - 1);
    PQfreemem(escaped);

    // Calculate SOD hash
    std::vector<uint8_t> sodHash = calculateHash(sodBytes, "SHA-256");
    std::string sodHashHex = bytesToHex(sodHash);

    std::stringstream sql;
    sql << "INSERT INTO pa_verification ("
        << "id, issuing_country, document_number, sod_binary, sod_hash, "
        << "dsc_subject_dn, dsc_serial_number, csca_subject_dn, "
        << "verification_status, verification_message, "
        << "trust_chain_valid, trust_chain_message, "
        << "sod_signature_valid, sod_signature_message, "
        << "dg_hashes_valid, dg_hashes_message, "
        << "crl_status, crl_message, "
        << "request_timestamp, completed_timestamp, processing_time_ms"
        << ") VALUES ("
        << "'" << verificationId << "', "
        << "'" << countryCode << "', "
        << (documentNumber.empty() ? "NULL" : "'" + documentNumber + "'") << ", "
        << "'" << sodEscaped << "', "
        << "'" << sodHashHex << "', "
        << "'" << PQescapeLiteral(conn, chainResult.dscSubject.c_str(), chainResult.dscSubject.size()) << "', "
        << "'" << chainResult.dscSerialNumber << "', "
        << "'" << PQescapeLiteral(conn, chainResult.cscaSubject.c_str(), chainResult.cscaSubject.size()) << "', "
        << "'" << status << "', "
        << (chainResult.validationErrors.empty() ? "NULL" :
            "'" + std::string(PQescapeLiteral(conn, chainResult.validationErrors.c_str(),
                chainResult.validationErrors.size())) + "'") << ", "
        << (chainResult.valid ? "TRUE" : "FALSE") << ", "
        << "'" << chainResult.crlMessage << "', "
        << (sodResult.valid ? "TRUE" : "FALSE") << ", "
        << (sodResult.validationErrors.empty() ? "NULL" :
            "'" + std::string(PQescapeLiteral(conn, sodResult.validationErrors.c_str(),
                sodResult.validationErrors.size())) + "'") << ", "
        << (dgResult.invalidGroups == 0 ? "TRUE" : "FALSE") << ", "
        << "NULL, "
        << "'" << crlStatusToString(chainResult.crlStatus) << "', "
        << "'" << chainResult.crlMessage << "', "
        << "NOW(), NOW(), " << processingTimeMs
        << ")";

    // Note: This SQL has some issues with escaping. Using parameterized query would be better.
    // For now, simplified version:

    std::string simpleSql =
        "INSERT INTO pa_verification (id, issuing_country, document_number, sod_binary, sod_hash, "
        "verification_status, trust_chain_valid, sod_signature_valid, dg_hashes_valid, "
        "crl_status, request_timestamp, completed_timestamp, processing_time_ms) VALUES "
        "($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, NOW(), NOW(), $11)";

    const char* paramValues[11];
    paramValues[0] = verificationId.c_str();
    paramValues[1] = countryCode.c_str();
    paramValues[2] = documentNumber.empty() ? nullptr : documentNumber.c_str();
    paramValues[3] = reinterpret_cast<const char*>(sodBytes.data());
    paramValues[4] = sodHashHex.c_str();
    paramValues[5] = status.c_str();
    std::string chainValidStr = chainResult.valid ? "t" : "f";
    std::string sodValidStr = sodResult.valid ? "t" : "f";
    std::string dgValidStr = dgResult.invalidGroups == 0 ? "t" : "f";
    paramValues[6] = chainValidStr.c_str();
    paramValues[7] = sodValidStr.c_str();
    paramValues[8] = dgValidStr.c_str();
    paramValues[9] = crlStatusToString(chainResult.crlStatus).c_str();
    std::string processingTimeStr = std::to_string(processingTimeMs);
    paramValues[10] = processingTimeStr.c_str();

    int paramLengths[11] = {0, 0, 0, static_cast<int>(sodBytes.size()), 0, 0, 0, 0, 0, 0, 0};
    int paramFormats[11] = {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};  // 1 = binary for SOD

    PGresult* res = PQexecParams(conn, simpleSql.c_str(), 11, nullptr,
        paramValues, paramLengths, paramFormats, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to save PA verification: {}", PQerrorMessage(conn));
        PQclear(res);
        return "";
    }

    PQclear(res);
    spdlog::info("Saved PA verification to database: {}", verificationId);
    return verificationId;
}

void savePaDataGroups(
    PGconn* conn,
    const std::string& verificationId,
    const DataGroupValidationResult& dgResult,
    const std::string& hashAlgorithm,
    const std::map<int, std::vector<uint8_t>>& dataGroups) {

    if (!conn) return;

    for (const auto& [dgKey, detail] : dgResult.details) {
        int dgNum = std::stoi(dgKey.substr(2));  // Extract number from "DG1", "DG2", etc.

        std::string sql =
            "INSERT INTO pa_data_group (verification_id, dg_number, expected_hash, actual_hash, "
            "hash_algorithm, hash_valid, dg_binary) VALUES ($1, $2, $3, $4, $5, $6, $7)";

        std::string dgNumStr = std::to_string(dgNum);
        std::string hashValidStr = detail.valid ? "t" : "f";

        // Get DG binary if available
        const std::vector<uint8_t>* dgBinary = nullptr;
        auto it = dataGroups.find(dgNum);
        if (it != dataGroups.end()) {
            dgBinary = &(it->second);
        }

        const char* paramValues[7];
        paramValues[0] = verificationId.c_str();
        paramValues[1] = dgNumStr.c_str();
        paramValues[2] = detail.expectedHash.c_str();
        paramValues[3] = detail.actualHash.c_str();
        paramValues[4] = hashAlgorithm.c_str();
        paramValues[5] = hashValidStr.c_str();
        paramValues[6] = dgBinary ? reinterpret_cast<const char*>(dgBinary->data()) : nullptr;

        int paramLengths[7] = {0, 0, 0, 0, 0, 0, dgBinary ? static_cast<int>(dgBinary->size()) : 0};
        int paramFormats[7] = {0, 0, 0, 0, 0, 0, 1};

        PGresult* res = PQexecParams(conn, sql.c_str(), 7, nullptr,
            paramValues, paramLengths, paramFormats, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::warn("Failed to save DG{}: {}", dgNum, PQerrorMessage(conn));
        }
        PQclear(res);
    }

    spdlog::debug("Saved {} data groups for verification {}", dgResult.details.size(), verificationId);
}

// =============================================================================
// JSON Response Builders (matching Java DTOs)
// =============================================================================

Json::Value buildCertificateChainValidationJson(const CertificateChainValidationResult& result) {
    Json::Value json;
    json["valid"] = result.valid;
    json["dscSubject"] = result.dscSubject;
    json["dscSerialNumber"] = result.dscSerialNumber;
    json["cscaSubject"] = result.cscaSubject;
    json["cscaSerialNumber"] = result.cscaSerialNumber;
    json["notBefore"] = result.notBefore;
    json["notAfter"] = result.notAfter;
    json["crlChecked"] = result.crlChecked;
    json["revoked"] = result.revoked;
    json["crlStatus"] = crlStatusToString(result.crlStatus);
    json["crlStatusDescription"] = result.crlStatusDescription;
    json["crlStatusDetailedDescription"] = result.crlStatusDetailedDescription;
    json["crlStatusSeverity"] = result.crlStatusSeverity;
    json["crlMessage"] = result.crlMessage;
    if (!result.validationErrors.empty()) {
        json["validationErrors"] = result.validationErrors;
    }
    return json;
}

Json::Value buildSodSignatureValidationJson(const SodSignatureValidationResult& result) {
    Json::Value json;
    json["valid"] = result.valid;
    json["signatureAlgorithm"] = result.signatureAlgorithm;
    json["hashAlgorithm"] = result.hashAlgorithm;
    if (!result.validationErrors.empty()) {
        json["validationErrors"] = result.validationErrors;
    }
    return json;
}

Json::Value buildDataGroupValidationJson(const DataGroupValidationResult& result) {
    Json::Value json;
    json["totalGroups"] = result.totalGroups;
    json["validGroups"] = result.validGroups;
    json["invalidGroups"] = result.invalidGroups;

    Json::Value details;
    for (const auto& [dgKey, detail] : result.details) {
        Json::Value dgJson;
        dgJson["valid"] = detail.valid;
        dgJson["expectedHash"] = detail.expectedHash;
        dgJson["actualHash"] = detail.actualHash;
        details[dgKey] = dgJson;
    }
    json["details"] = details;

    return json;
}

// =============================================================================
// API Route Handlers
// =============================================================================

void registerRoutes() {
    auto& app = drogon::app();

    // Health check endpoint
    app.registerHandler(
        "/api/health",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["service"] = "pa-service";
            result["status"] = "UP";
            result["version"] = "2.0.0";
            result["timestamp"] = getCurrentTimestamp();

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Database health check
    app.registerHandler(
        "/api/health/database",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/health/database");
            auto result = checkDatabase();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // LDAP health check
    app.registerHandler(
        "/api/health/ldap",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/health/ldap");
            auto result = checkLdap();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // PA verify endpoint - POST /api/pa/verify
    app.registerHandler(
        "/api/pa/verify",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

            spdlog::info("POST /api/pa/verify - Passive Authentication verification");
            auto startTime = std::chrono::steady_clock::now();
            std::string verificationId = generateUuid();
            std::vector<PassiveAuthenticationError> errors;

            // Parse request body
            auto jsonBody = req->getJsonObject();
            if (!jsonBody) {
                Json::Value error;
                error["status"] = "ERROR";
                error["verificationId"] = verificationId;
                error["errors"][0]["code"] = "INVALID_REQUEST";
                error["errors"][0]["message"] = "Invalid JSON body";
                error["errors"][0]["severity"] = "CRITICAL";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Get SOD data (Base64 encoded)
            std::string sodBase64 = (*jsonBody)["sod"].asString();
            if (sodBase64.empty()) {
                Json::Value error;
                error["status"] = "ERROR";
                error["verificationId"] = verificationId;
                error["errors"][0]["code"] = "MISSING_SOD";
                error["errors"][0]["message"] = "SOD data is required";
                error["errors"][0]["severity"] = "CRITICAL";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Decode SOD
            std::vector<uint8_t> sodBytes = base64Decode(sodBase64);
            if (sodBytes.empty()) {
                Json::Value error;
                error["status"] = "ERROR";
                error["verificationId"] = verificationId;
                error["errors"][0]["code"] = "INVALID_SOD";
                error["errors"][0]["message"] = "Failed to decode SOD (invalid Base64)";
                error["errors"][0]["severity"] = "CRITICAL";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Parse Data Groups
            std::map<int, std::vector<uint8_t>> dataGroups;
            if (jsonBody->isMember("dataGroups")) {
                if ((*jsonBody)["dataGroups"].isArray()) {
                    // Array format: [{number: "DG1", data: "base64..."}, ...]
                    for (const auto& dg : (*jsonBody)["dataGroups"]) {
                        std::string dgNumStr = dg["number"].asString();
                        std::string dgData = dg["data"].asString();
                        int dgNum = std::stoi(dgNumStr.substr(2));  // Extract number from "DG1"
                        dataGroups[dgNum] = base64Decode(dgData);
                    }
                } else if ((*jsonBody)["dataGroups"].isObject()) {
                    // Object format: {"DG1": "base64...", "DG2": "base64..."}
                    for (const auto& key : (*jsonBody)["dataGroups"].getMemberNames()) {
                        int dgNum = std::stoi(key.substr(2));
                        std::string dgData = (*jsonBody)["dataGroups"][key].asString();
                        dataGroups[dgNum] = base64Decode(dgData);
                    }
                }
            }

            // Get optional fields
            std::string issuingCountry = (*jsonBody).get("issuingCountry", "").asString();
            std::string documentNumber = (*jsonBody).get("documentNumber", "").asString();

            // Extract documentNumber from DG1 if not provided in request
            if (documentNumber.empty() && dataGroups.count(1) > 0) {
                const auto& dg1Data = dataGroups[1];
                // Local helper to clean MRZ field (remove trailing '<' chars)
                auto cleanDocNum = [](const std::string& field) -> std::string {
                    std::string result;
                    for (char c : field) {
                        if (c != '<') result += c;
                    }
                    // Trim trailing spaces
                    size_t end = result.find_last_not_of(' ');
                    if (end != std::string::npos) {
                        result = result.substr(0, end + 1);
                    }
                    return result;
                };
                // Extract MRZ from DG1 (skip TLV header - find 0x5F1F tag)
                size_t pos = 0;
                while (pos + 3 < dg1Data.size()) {
                    if (dg1Data[pos] == 0x5F && dg1Data[pos + 1] == 0x1F) {
                        // Found MRZ tag 5F1F
                        pos += 2;
                        size_t mrzLen = dg1Data[pos++];
                        if (mrzLen > 127) {
                            // Long form length
                            size_t numBytes = mrzLen & 0x7F;
                            mrzLen = 0;
                            for (size_t i = 0; i < numBytes && pos < dg1Data.size(); i++) {
                                mrzLen = (mrzLen << 8) | dg1Data[pos++];
                            }
                        }
                        if (pos + mrzLen <= dg1Data.size()) {
                            std::string mrzData(dg1Data.begin() + pos, dg1Data.begin() + pos + mrzLen);
                            // Parse MRZ to extract document number
                            if (mrzData.length() == 88) {
                                // TD3 format (passport): line2 starts at 44, docNum at 0-9
                                std::string line2 = mrzData.substr(44, 44);
                                documentNumber = cleanDocNum(line2.substr(0, 9));
                            } else if (mrzData.length() == 72) {
                                // TD2 format: line2 starts at 36, docNum at 0-9
                                std::string line2 = mrzData.substr(36, 36);
                                documentNumber = cleanDocNum(line2.substr(0, 9));
                            } else if (mrzData.length() == 90) {
                                // TD1 format: docNum at 5-14
                                documentNumber = cleanDocNum(mrzData.substr(5, 9));
                            }
                            if (!documentNumber.empty()) {
                                spdlog::info("Extracted documentNumber from DG1: {}", documentNumber);
                            }
                        }
                        break;
                    }
                    pos++;
                }
            }

            // Start verification process
            std::string status = "VALID";
            CertificateChainValidationResult chainResult;
            SodSignatureValidationResult sodResult;
            DataGroupValidationResult dgResult;

            try {
                // Step 1: Extract DSC from SOD
                X509* dscCert = extractDscFromSod(sodBytes);
                if (!dscCert) {
                    throw std::runtime_error("Failed to extract DSC from SOD");
                }

                // Extract country from DSC if not provided
                if (issuingCountry.empty()) {
                    issuingCountry = extractCountryFromDn(getX509SubjectDn(dscCert));
                }

                // Step 2: Get LDAP connection and lookup CSCA
                LDAP* ld = getLdapConnection();
                X509* cscaCert = nullptr;
                if (ld) {
                    std::string issuerDn = getX509IssuerDn(dscCert);
                    cscaCert = retrieveCscaFromLdap(ld, issuerDn);
                }

                // Step 3: Validate certificate chain
                chainResult = validateCertificateChain(dscCert, cscaCert, issuingCountry, ld);

                // Step 4: Validate SOD signature
                sodResult = validateSodSignature(sodBytes, dscCert);

                // Step 5: Parse and validate Data Group hashes
                std::map<int, std::vector<uint8_t>> expectedHashes = parseDataGroupHashes(sodBytes);
                std::string hashAlgorithm = extractHashAlgorithm(sodBytes);
                dgResult = validateDataGroupHashes(dataGroups, expectedHashes, hashAlgorithm);

                // Determine overall status
                if (!chainResult.valid || chainResult.revoked) {
                    status = "INVALID";
                    PassiveAuthenticationError err;
                    err.code = chainResult.revoked ? "CERTIFICATE_REVOKED" : "CHAIN_VALIDATION_FAILED";
                    err.message = chainResult.validationErrors.empty() ?
                        "Certificate chain validation failed" : chainResult.validationErrors;
                    err.severity = "CRITICAL";
                    err.timestamp = getCurrentTimestamp();
                    errors.push_back(err);
                }

                if (!sodResult.valid) {
                    status = "INVALID";
                    PassiveAuthenticationError err;
                    err.code = "SOD_SIGNATURE_INVALID";
                    err.message = "SOD signature verification failed";
                    err.severity = "CRITICAL";
                    err.timestamp = getCurrentTimestamp();
                    errors.push_back(err);
                }

                if (dgResult.invalidGroups > 0) {
                    status = "INVALID";
                    PassiveAuthenticationError err;
                    err.code = "DG_HASH_MISMATCH";
                    err.message = "Data Group hash validation failed";
                    err.severity = "CRITICAL";
                    err.timestamp = getCurrentTimestamp();
                    errors.push_back(err);
                }

                // Clean up
                X509_free(dscCert);
                if (cscaCert) X509_free(cscaCert);
                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);

                // Save to database
                PGconn* conn = getDbConnection();
                if (conn) {
                    auto endTime = std::chrono::steady_clock::now();
                    int processingTimeMs = static_cast<int>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

                    savePaVerification(conn, verificationId, status, issuingCountry, documentNumber,
                        sodBytes, chainResult, sodResult, dgResult, processingTimeMs);
                    savePaDataGroups(conn, verificationId, dgResult, hashAlgorithm, dataGroups);
                    PQfinish(conn);
                }

            } catch (const std::exception& e) {
                spdlog::error("PA verification failed: {}", e.what());
                status = "ERROR";
                PassiveAuthenticationError err;
                err.code = "PA_EXECUTION_ERROR";
                err.message = std::string("Passive Authentication execution failed: ") + e.what();
                err.severity = "CRITICAL";
                err.timestamp = getCurrentTimestamp();
                errors.push_back(err);
            }

            // Calculate processing time
            auto endTime = std::chrono::steady_clock::now();
            int processingTimeMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

            // Build response (matching Java PassiveAuthenticationResponse)
            Json::Value response;
            response["status"] = status;
            response["verificationId"] = verificationId;
            response["verificationTimestamp"] = getCurrentTimestamp();
            response["issuingCountry"] = issuingCountry;
            response["documentNumber"] = documentNumber;
            response["certificateChainValidation"] = buildCertificateChainValidationJson(chainResult);
            response["sodSignatureValidation"] = buildSodSignatureValidationJson(sodResult);
            response["dataGroupValidation"] = buildDataGroupValidationJson(dgResult);
            response["processingDurationMs"] = processingTimeMs;

            Json::Value errorsJson(Json::arrayValue);
            for (const auto& err : errors) {
                Json::Value errJson;
                errJson["code"] = err.code;
                errJson["message"] = err.message;
                errJson["severity"] = err.severity;
                errJson["timestamp"] = err.timestamp;
                errorsJson.append(errJson);
            }
            response["errors"] = errorsJson;

            spdlog::info("PA verification completed - Status: {}, ID: {}, Duration: {}ms",
                status, verificationId, processingTimeMs);

            // Wrap response in {success: true/false, data: {...}} format for frontend compatibility
            Json::Value wrappedResponse;
            wrappedResponse["success"] = (status == "VALID" || status == "INVALID");
            wrappedResponse["data"] = response;
            if (status == "ERROR") {
                wrappedResponse["success"] = false;
                wrappedResponse["error"] = errors.empty() ? "Unknown error" : errors[0].message;
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(wrappedResponse);
            callback(resp);
        },
        {drogon::Post}
    );

    // PA history endpoint
    app.registerHandler(
        "/api/pa/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/history");

            int page = 0;
            int size = 20;
            std::string statusFilter;
            std::string countryFilter;

            if (auto p = req->getParameter("page"); !p.empty()) {
                page = std::stoi(p);
            }
            if (auto s = req->getParameter("size"); !s.empty()) {
                size = std::stoi(s);
            }
            if (auto st = req->getParameter("status"); !st.empty()) {
                statusFilter = st;
            }
            if (auto c = req->getParameter("issuingCountry"); !c.empty()) {
                countryFilter = c;
            }

            PGconn* conn = getDbConnection();
            Json::Value result;
            result["content"] = Json::Value(Json::arrayValue);
            result["page"] = page;
            result["size"] = size;

            if (conn) {
                // Build query
                std::string countSql = "SELECT COUNT(*) FROM pa_verification";
                std::string sql = "SELECT id, issuing_country, document_number, verification_status, "
                    "request_timestamp, processing_time_ms, "
                    "trust_chain_valid, sod_signature_valid, dg_hashes_valid, crl_status "
                    "FROM pa_verification";

                std::string whereClause;
                if (!statusFilter.empty()) {
                    whereClause = " WHERE verification_status = '" + statusFilter + "'";
                }
                if (!countryFilter.empty()) {
                    if (whereClause.empty()) {
                        whereClause = " WHERE issuing_country = '" + countryFilter + "'";
                    } else {
                        whereClause += " AND issuing_country = '" + countryFilter + "'";
                    }
                }

                countSql += whereClause;
                sql += whereClause + " ORDER BY request_timestamp DESC LIMIT " +
                    std::to_string(size) + " OFFSET " + std::to_string(page * size);

                // Get total count
                PGresult* countRes = PQexec(conn, countSql.c_str());
                int totalElements = 0;
                if (PQresultStatus(countRes) == PGRES_TUPLES_OK && PQntuples(countRes) > 0) {
                    totalElements = std::stoi(PQgetvalue(countRes, 0, 0));
                }
                PQclear(countRes);

                result["totalElements"] = totalElements;
                result["totalPages"] = (totalElements + size - 1) / size;
                result["first"] = (page == 0);
                result["last"] = (page >= (totalElements / size));

                // Get records
                PGresult* res = PQexec(conn, sql.c_str());
                if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                    int nRows = PQntuples(res);
                    for (int i = 0; i < nRows; i++) {
                        Json::Value item;
                        item["verificationId"] = PQgetvalue(res, i, 0);
                        item["issuingCountry"] = PQgetvalue(res, i, 1);
                        item["documentNumber"] = PQgetvalue(res, i, 2);
                        item["status"] = PQgetvalue(res, i, 3);
                        item["verificationTimestamp"] = PQgetvalue(res, i, 4);
                        item["processingDurationMs"] = std::stoi(PQgetvalue(res, i, 5));

                        // Build validation summaries
                        Json::Value chainValidation;
                        chainValidation["valid"] = (std::string(PQgetvalue(res, i, 6)) == "t");
                        item["certificateChainValidation"] = chainValidation;

                        Json::Value sodValidation;
                        sodValidation["valid"] = (std::string(PQgetvalue(res, i, 7)) == "t");
                        item["sodSignatureValidation"] = sodValidation;

                        Json::Value dgValidation;
                        dgValidation["valid"] = (std::string(PQgetvalue(res, i, 8)) == "t");
                        item["dataGroupValidation"] = dgValidation;

                        result["content"].append(item);
                    }
                }
                PQclear(res);
                PQfinish(conn);
            } else {
                result["totalElements"] = 0;
                result["totalPages"] = 0;
                result["first"] = true;
                result["last"] = true;
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // PA detail by ID
    app.registerHandler(
        "/api/pa/{id}",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& id) {
            spdlog::info("GET /api/pa/{}", id);

            PGconn* conn = getDbConnection();
            Json::Value result;

            if (conn) {
                std::string sql = "SELECT id, issuing_country, document_number, verification_status, "
                    "request_timestamp, completed_timestamp, processing_time_ms, "
                    "dsc_subject_dn, dsc_serial_number, csca_subject_dn, csca_fingerprint, "
                    "trust_chain_valid, trust_chain_message, "
                    "sod_signature_valid, sod_signature_message, "
                    "dg_hashes_valid, dg_hashes_message, "
                    "crl_status, crl_message "
                    "FROM pa_verification WHERE id = $1";

                const char* paramValues[1] = {id.c_str()};
                PGresult* res = PQexecParams(conn, sql.c_str(), 1, nullptr, paramValues, nullptr, nullptr, 0);

                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    result["verificationId"] = PQgetvalue(res, 0, 0);
                    result["issuingCountry"] = PQgetvalue(res, 0, 1);
                    result["documentNumber"] = PQgetvalue(res, 0, 2);
                    result["status"] = PQgetvalue(res, 0, 3);
                    result["verificationTimestamp"] = PQgetvalue(res, 0, 4);
                    result["processingDurationMs"] = std::stoi(PQgetvalue(res, 0, 6));

                    // Certificate chain validation
                    Json::Value chainValidation;
                    chainValidation["valid"] = (std::string(PQgetvalue(res, 0, 11)) == "t");
                    chainValidation["dscSubject"] = PQgetvalue(res, 0, 7);
                    chainValidation["dscSerialNumber"] = PQgetvalue(res, 0, 8);
                    chainValidation["cscaSubject"] = PQgetvalue(res, 0, 9);
                    chainValidation["crlStatus"] = PQgetvalue(res, 0, 17);
                    chainValidation["crlMessage"] = PQgetvalue(res, 0, 18);
                    result["certificateChainValidation"] = chainValidation;

                    // SOD signature validation
                    Json::Value sodValidation;
                    sodValidation["valid"] = (std::string(PQgetvalue(res, 0, 13)) == "t");
                    result["sodSignatureValidation"] = sodValidation;

                    // Data group validation
                    Json::Value dgValidation;
                    dgValidation["valid"] = (std::string(PQgetvalue(res, 0, 15)) == "t");

                    // Fetch DG details
                    std::string dgSql = "SELECT dg_number, expected_hash, actual_hash, hash_valid "
                        "FROM pa_data_group WHERE verification_id = $1 ORDER BY dg_number";
                    PGresult* dgRes = PQexecParams(conn, dgSql.c_str(), 1, nullptr, paramValues, nullptr, nullptr, 0);

                    if (PQresultStatus(dgRes) == PGRES_TUPLES_OK) {
                        int nDgs = PQntuples(dgRes);
                        dgValidation["totalGroups"] = nDgs;
                        int validCount = 0;
                        Json::Value details;

                        for (int i = 0; i < nDgs; i++) {
                            std::string dgKey = "DG" + std::string(PQgetvalue(dgRes, i, 0));
                            Json::Value dgDetail;
                            dgDetail["valid"] = (std::string(PQgetvalue(dgRes, i, 3)) == "t");
                            dgDetail["expectedHash"] = PQgetvalue(dgRes, i, 1);
                            dgDetail["actualHash"] = PQgetvalue(dgRes, i, 2);
                            details[dgKey] = dgDetail;

                            if (dgDetail["valid"].asBool()) validCount++;
                        }

                        dgValidation["validGroups"] = validCount;
                        dgValidation["invalidGroups"] = nDgs - validCount;
                        dgValidation["details"] = details;
                    }
                    PQclear(dgRes);

                    result["dataGroupValidation"] = dgValidation;
                    result["errors"] = Json::Value(Json::arrayValue);

                    PQclear(res);
                    PQfinish(conn);

                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                    callback(resp);
                    return;
                }

                PQclear(res);
                PQfinish(conn);
            }

            // Not found
            result["status"] = "NOT_FOUND";
            result["message"] = "PA verification record not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
        },
        {drogon::Get}
    );

    // PA statistics endpoint
    app.registerHandler(
        "/api/pa/statistics",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/statistics");

            PGconn* conn = getDbConnection();
            Json::Value result;
            result["totalVerifications"] = 0;
            result["validCount"] = 0;
            result["invalidCount"] = 0;
            result["errorCount"] = 0;
            result["averageProcessingTimeMs"] = 0;
            result["countriesVerified"] = 0;

            if (conn) {
                // Total count
                PGresult* res = PQexec(conn, "SELECT COUNT(*) FROM pa_verification");
                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    result["totalVerifications"] = std::stoi(PQgetvalue(res, 0, 0));
                }
                PQclear(res);

                // Valid count
                res = PQexec(conn, "SELECT COUNT(*) FROM pa_verification WHERE verification_status = 'VALID'");
                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    result["validCount"] = std::stoi(PQgetvalue(res, 0, 0));
                }
                PQclear(res);

                // Invalid count
                res = PQexec(conn, "SELECT COUNT(*) FROM pa_verification WHERE verification_status = 'INVALID'");
                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    result["invalidCount"] = std::stoi(PQgetvalue(res, 0, 0));
                }
                PQclear(res);

                // Error count
                res = PQexec(conn, "SELECT COUNT(*) FROM pa_verification WHERE verification_status = 'ERROR'");
                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    result["errorCount"] = std::stoi(PQgetvalue(res, 0, 0));
                }
                PQclear(res);

                // Average processing time
                res = PQexec(conn, "SELECT AVG(processing_time_ms) FROM pa_verification");
                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    const char* val = PQgetvalue(res, 0, 0);
                    if (val && strlen(val) > 0) {
                        result["averageProcessingTimeMs"] = static_cast<int>(std::stod(val));
                    }
                }
                PQclear(res);

                // Countries count
                res = PQexec(conn, "SELECT COUNT(DISTINCT issuing_country) FROM pa_verification");
                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    result["countriesVerified"] = std::stoi(PQgetvalue(res, 0, 0));
                }
                PQclear(res);

                PQfinish(conn);
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Helper function to convert YYMMDD to YYYY-MM-DD (Java compatible)
    auto convertMrzDate = [](const std::string& yymmdd) -> std::string {
        if (yymmdd.length() != 6) return yymmdd;

        int year = std::stoi(yymmdd.substr(0, 2));
        std::string month = yymmdd.substr(2, 2);
        std::string day = yymmdd.substr(4, 2);

        // ICAO 9303 rule: years 00-99 map to 1900-2099
        // For birth dates: assume 00-23 = 2000-2023, 24-99 = 1924-1999
        // For expiry dates: assume 00-49 = 2000-2049, 50-99 = 1950-1999
        int fullYear = (year <= 23) ? 2000 + year : 1900 + year;

        return std::to_string(fullYear) + "-" + month + "-" + day;
    };

    // Helper function to convert expiry date YYMMDD to YYYY-MM-DD
    auto convertMrzExpiryDate = [](const std::string& yymmdd) -> std::string {
        if (yymmdd.length() != 6) return yymmdd;

        int year = std::stoi(yymmdd.substr(0, 2));
        std::string month = yymmdd.substr(2, 2);
        std::string day = yymmdd.substr(4, 2);

        // For expiry dates: assume 00-49 = 2000-2049, 50-99 = 1950-1999
        int fullYear = (year <= 49) ? 2000 + year : 1900 + year;

        return std::to_string(fullYear) + "-" + month + "-" + day;
    };

    // Helper function to clean MRZ field (remove filler characters)
    auto cleanMrzField = [](const std::string& field) -> std::string {
        std::string result = field;
        // Remove trailing < characters
        while (!result.empty() && result.back() == '<') {
            result.pop_back();
        }
        return result;
    };

    // Parse DG1 (MRZ) endpoint - Java compatible
    app.registerHandler(
        "/api/pa/parse-dg1",
        [&convertMrzDate, &convertMrzExpiryDate, &cleanMrzField](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-dg1");

            auto jsonBody = req->getJsonObject();
            std::string dg1Base64;

            if (jsonBody) {
                dg1Base64 = (*jsonBody).get("dg1Base64", "").asString();
                if (dg1Base64.empty()) {
                    dg1Base64 = (*jsonBody).get("dg1", "").asString();
                }
                if (dg1Base64.empty()) {
                    dg1Base64 = (*jsonBody).get("data", "").asString();
                }
            }

            if (dg1Base64.empty()) {
                Json::Value error;
                error["error"] = "DG1 data is required (dg1Base64, dg1, or data field)";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            std::vector<uint8_t> dg1Bytes = base64Decode(dg1Base64);
            if (dg1Bytes.empty()) {
                Json::Value error;
                error["error"] = "Invalid Base64 encoding";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Parse DG1 (MRZ) - ICAO 9303 compliant
            // DG1 structure: Tag 0x61, Length, Tag 0x5F1F, Length, MRZ data
            Json::Value result;

            // Try to find MRZ data in DG1
            std::string mrzData;
            for (size_t i = 0; i < dg1Bytes.size() - 2; i++) {
                if (dg1Bytes[i] == 0x5F && dg1Bytes[i+1] == 0x1F) {
                    // Found MRZ tag
                    i += 2;
                    size_t mrzLen = dg1Bytes[i++];
                    if (mrzLen > 0x80) {
                        int numBytes = mrzLen & 0x7F;
                        mrzLen = 0;
                        for (int j = 0; j < numBytes; j++) {
                            mrzLen = (mrzLen << 8) | dg1Bytes[i++];
                        }
                    }
                    mrzData = std::string(reinterpret_cast<const char*>(&dg1Bytes[i]), mrzLen);
                    break;
                }
            }

            if (mrzData.length() >= 88) {
                // TD3 format (passport): 2 lines x 44 characters
                std::string line1 = mrzData.substr(0, 44);
                std::string line2 = mrzData.substr(44, 44);

                // Java compatible: mrzLine1, mrzLine2, mrzFull
                result["mrzLine1"] = line1;
                result["mrzLine2"] = line2;
                result["mrzFull"] = mrzData;

                // Document type (first 2 characters)
                result["documentType"] = cleanMrzField(line1.substr(0, 2));
                result["issuingCountry"] = line1.substr(2, 3);

                // Parse name (surname<<givennames)
                size_t nameStart = 5;
                size_t nameEnd = line1.find("<<", nameStart);
                std::string surname, givenNames;

                if (nameEnd != std::string::npos) {
                    surname = line1.substr(nameStart, nameEnd - nameStart);
                    std::replace(surname.begin(), surname.end(), '<', ' ');
                    surname = trim(surname);

                    std::string givenPart = line1.substr(nameEnd + 2);
                    std::replace(givenPart.begin(), givenPart.end(), '<', ' ');
                    givenNames = trim(givenPart);
                } else {
                    // No << separator found, try single < as separator
                    nameEnd = line1.find('<', nameStart);
                    if (nameEnd != std::string::npos) {
                        surname = line1.substr(nameStart, nameEnd - nameStart);
                        surname = trim(surname);
                    }
                }

                result["surname"] = surname;
                result["givenNames"] = givenNames;

                // Java compatible: fullName field
                if (!surname.empty() && !givenNames.empty()) {
                    result["fullName"] = surname + " " + givenNames;
                } else if (!surname.empty()) {
                    result["fullName"] = surname;
                } else {
                    result["fullName"] = givenNames;
                }

                // Line 2 parsing
                std::string docNum = cleanMrzField(line2.substr(0, 9));
                result["documentNumber"] = docNum;
                result["documentNumberCheckDigit"] = line2.substr(9, 1);

                result["nationality"] = line2.substr(10, 3);

                // Date of birth with YYYY-MM-DD format (Java compatible)
                std::string dobRaw = line2.substr(13, 6);
                result["dateOfBirth"] = convertMrzDate(dobRaw);
                result["dateOfBirthRaw"] = dobRaw;
                result["dateOfBirthCheckDigit"] = line2.substr(19, 1);

                // Sex
                result["sex"] = line2.substr(20, 1);

                // Date of expiry with YYYY-MM-DD format (Java compatible)
                std::string expiryRaw = line2.substr(21, 6);
                result["dateOfExpiry"] = convertMrzExpiryDate(expiryRaw);
                result["dateOfExpiryRaw"] = expiryRaw;
                result["dateOfExpiryCheckDigit"] = line2.substr(27, 1);

                // Optional data and composite check digit
                result["optionalData1"] = cleanMrzField(line2.substr(28, 14));
                result["compositeCheckDigit"] = line2.substr(43, 1);

                result["success"] = true;
            } else if (mrzData.length() >= 72) {
                // TD2 format: 2 lines x 36 characters
                std::string line1 = mrzData.substr(0, 36);
                std::string line2 = mrzData.substr(36, 36);

                result["mrzLine1"] = line1;
                result["mrzLine2"] = line2;
                result["mrzFull"] = mrzData;
                result["documentType"] = cleanMrzField(line1.substr(0, 2));
                result["issuingCountry"] = line1.substr(2, 3);

                // Name parsing
                size_t nameStart = 5;
                size_t nameEnd = line1.find("<<", nameStart);
                std::string surname, givenNames;

                if (nameEnd != std::string::npos) {
                    surname = line1.substr(nameStart, nameEnd - nameStart);
                    std::replace(surname.begin(), surname.end(), '<', ' ');
                    surname = trim(surname);

                    std::string givenPart = line1.substr(nameEnd + 2);
                    std::replace(givenPart.begin(), givenPart.end(), '<', ' ');
                    givenNames = trim(givenPart);
                }

                result["surname"] = surname;
                result["givenNames"] = givenNames;
                result["fullName"] = !surname.empty() ? (surname + " " + givenNames) : givenNames;

                // Line 2
                result["documentNumber"] = cleanMrzField(line2.substr(0, 9));
                result["nationality"] = line2.substr(10, 3);
                result["dateOfBirth"] = convertMrzDate(line2.substr(13, 6));
                result["dateOfBirthRaw"] = line2.substr(13, 6);
                result["sex"] = line2.substr(20, 1);
                result["dateOfExpiry"] = convertMrzExpiryDate(line2.substr(21, 6));
                result["dateOfExpiryRaw"] = line2.substr(21, 6);

                result["success"] = true;
            } else if (mrzData.length() >= 30) {
                // TD1 format: 3 lines x 30 characters (ID cards)
                result["mrzFull"] = mrzData;
                result["documentType"] = cleanMrzField(mrzData.substr(0, 2));
                result["issuingCountry"] = mrzData.substr(2, 3);
                result["documentNumber"] = cleanMrzField(mrzData.substr(5, 9));

                // For TD1, birth date is at different position
                if (mrzData.length() >= 60) {
                    result["dateOfBirth"] = convertMrzDate(mrzData.substr(30, 6));
                    result["dateOfBirthRaw"] = mrzData.substr(30, 6);
                    result["sex"] = mrzData.substr(37, 1);
                    result["dateOfExpiry"] = convertMrzExpiryDate(mrzData.substr(38, 6));
                    result["dateOfExpiryRaw"] = mrzData.substr(38, 6);
                    result["nationality"] = mrzData.substr(45, 3);
                }

                result["success"] = true;
            } else {
                result["error"] = "MRZ data too short or invalid format (length: " +
                    std::to_string(mrzData.length()) + ")";
                result["success"] = false;
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Parse MRZ text endpoint - Java compatible
    app.registerHandler(
        "/api/pa/parse-mrz-text",
        [&convertMrzDate, &convertMrzExpiryDate, &cleanMrzField](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-mrz-text");

            auto jsonBody = req->getJsonObject();
            if (!jsonBody || (*jsonBody)["mrzText"].asString().empty()) {
                Json::Value error;
                error["error"] = "MRZ text is required";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            std::string mrzText = (*jsonBody)["mrzText"].asString();
            // Remove newlines and spaces
            mrzText.erase(std::remove(mrzText.begin(), mrzText.end(), '\n'), mrzText.end());
            mrzText.erase(std::remove(mrzText.begin(), mrzText.end(), '\r'), mrzText.end());

            Json::Value result;

            if (mrzText.length() >= 88) {
                std::string line1 = mrzText.substr(0, 44);
                std::string line2 = mrzText.substr(44, 44);

                // Java compatible: mrzLine1, mrzLine2, mrzFull
                result["mrzLine1"] = line1;
                result["mrzLine2"] = line2;
                result["mrzFull"] = mrzText;

                result["documentType"] = cleanMrzField(line1.substr(0, 2));
                result["issuingCountry"] = line1.substr(2, 3);
                result["documentNumber"] = cleanMrzField(line2.substr(0, 9));
                result["documentNumberCheckDigit"] = line2.substr(9, 1);
                result["nationality"] = line2.substr(10, 3);

                // Date fields with YYYY-MM-DD format
                std::string dobRaw = line2.substr(13, 6);
                result["dateOfBirth"] = convertMrzDate(dobRaw);
                result["dateOfBirthRaw"] = dobRaw;
                result["dateOfBirthCheckDigit"] = line2.substr(19, 1);

                result["sex"] = line2.substr(20, 1);

                std::string expiryRaw = line2.substr(21, 6);
                result["dateOfExpiry"] = convertMrzExpiryDate(expiryRaw);
                result["dateOfExpiryRaw"] = expiryRaw;
                result["dateOfExpiryCheckDigit"] = line2.substr(27, 1);

                result["optionalData1"] = cleanMrzField(line2.substr(28, 14));
                result["compositeCheckDigit"] = line2.substr(43, 1);

                // Parse name
                size_t nameEnd = line1.find("<<", 5);
                std::string surname, givenNames;
                if (nameEnd != std::string::npos) {
                    surname = line1.substr(5, nameEnd - 5);
                    std::replace(surname.begin(), surname.end(), '<', ' ');
                    surname = trim(surname);
                    result["surname"] = surname;

                    std::string givenPart = line1.substr(nameEnd + 2);
                    std::replace(givenPart.begin(), givenPart.end(), '<', ' ');
                    givenNames = trim(givenPart);
                    result["givenNames"] = givenNames;
                }

                // Java compatible: fullName
                if (!surname.empty() && !givenNames.empty()) {
                    result["fullName"] = surname + " " + givenNames;
                } else if (!surname.empty()) {
                    result["fullName"] = surname;
                } else {
                    result["fullName"] = givenNames;
                }

                result["success"] = true;
            } else {
                result["error"] = "Invalid MRZ format (expected 88 characters for TD3, got " +
                    std::to_string(mrzText.length()) + ")";
                result["success"] = false;
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Parse DG2 (Face Image) endpoint - Java compatible with ISO 19794-5 FAC support
    app.registerHandler(
        "/api/pa/parse-dg2",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-dg2");

            auto jsonBody = req->getJsonObject();
            std::string dg2Base64;

            if (jsonBody) {
                dg2Base64 = (*jsonBody).get("dg2Base64", "").asString();
                if (dg2Base64.empty()) {
                    dg2Base64 = (*jsonBody).get("dg2", "").asString();
                }
                if (dg2Base64.empty()) {
                    dg2Base64 = (*jsonBody).get("data", "").asString();
                }
            }

            if (dg2Base64.empty()) {
                Json::Value error;
                error["error"] = "DG2 data is required (dg2Base64, dg2, or data field)";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            std::vector<uint8_t> dg2Bytes = base64Decode(dg2Base64);
            if (dg2Bytes.empty()) {
                Json::Value error;
                error["error"] = "Invalid Base64 encoding";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // ICAO 9303 DG2 parsing with ISO 19794-5 FAC container support
            Json::Value result;
            result["success"] = true;
            result["dg2Size"] = static_cast<int>(dg2Bytes.size());

            // Helper to find FAC container (ISO/IEC 19794-5 format)
            // FAC header: "FAC" (0x46, 0x41, 0x43) followed by format identifier
            auto findFacContainer = [](const std::vector<uint8_t>& data, size_t startPos) -> std::pair<size_t, size_t> {
                for (size_t i = startPos; i < data.size() - 20; i++) {
                    // Look for "FAC" followed by 0x00 (Face Image Type)
                    if (data[i] == 0x46 && data[i+1] == 0x41 && data[i+2] == 0x43 && data[i+3] == 0x00) {
                        // FAC header found - ISO 19794-5 format
                        // Skip FAC header (14-20 bytes typically) to find image data
                        // The image usually starts after offset ~20 from FAC header
                        return {i, i + 20};
                    }
                }
                return {std::string::npos, 0};
            };

            // Helper to find JPEG in data
            auto findJpeg = [](const std::vector<uint8_t>& data, size_t startPos) -> std::pair<size_t, size_t> {
                for (size_t i = startPos; i < data.size() - 1; i++) {
                    if (data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF) {
                        // Found JPEG SOI marker (FF D8 FF)
                        size_t jpegStart = i;
                        // Find JPEG EOI marker (FF D9)
                        for (size_t j = i + 3; j < data.size() - 1; j++) {
                            if (data[j] == 0xFF && data[j+1] == 0xD9) {
                                return {jpegStart, j + 2 - jpegStart};
                            }
                        }
                        // No EOI found, take rest of data
                        return {jpegStart, data.size() - jpegStart};
                    }
                }
                return {std::string::npos, 0};
            };

            // Helper to find JPEG2000 in data
            auto findJpeg2000 = [](const std::vector<uint8_t>& data, size_t startPos) -> std::pair<size_t, size_t> {
                for (size_t i = startPos; i < data.size() - 12; i++) {
                    // JPEG2000 signature: 00 00 00 0C 6A 50 20 20 0D 0A 87 0A
                    // or just the "jP" box: 00 00 00 0C 6A 50
                    if (data[i] == 0x00 && data[i+1] == 0x00 &&
                        data[i+2] == 0x00 && data[i+3] == 0x0C &&
                        data[i+4] == 0x6A && data[i+5] == 0x50) {
                        // JPEG2000 found - take rest of data (J2K doesn't have clear EOI)
                        return {i, data.size() - i};
                    }
                }
                return {std::string::npos, 0};
            };

            // Parse DG2 structure and extract face images
            Json::Value faceImages(Json::arrayValue);
            int faceCount = 0;
            size_t searchPos = 0;

            // First, try to find Tag 0x75 (Biometric Info Template Group)
            // or Tag 0x7F61 (Biometric Info Template)
            bool foundBiometricTemplate = false;
            for (size_t i = 0; i < dg2Bytes.size() - 4; i++) {
                // Look for 0x75 (DG2 outer tag) or 0x7F61 (biometric template)
                if ((dg2Bytes[i] == 0x75) ||
                    (dg2Bytes[i] == 0x7F && dg2Bytes[i+1] == 0x61)) {
                    foundBiometricTemplate = true;
                    break;
                }
            }

            // Search for images
            while (searchPos < dg2Bytes.size()) {
                Json::Value faceImage;
                std::string imageFormat = "UNKNOWN";
                size_t imageStart = std::string::npos;
                size_t imageSize = 0;

                // Try FAC container first (ISO 19794-5)
                auto [facPos, facImageStart] = findFacContainer(dg2Bytes, searchPos);
                if (facPos != std::string::npos) {
                    result["hasFacContainer"] = true;
                    // Look for image within FAC container
                    auto [jpegStart, jpegSize] = findJpeg(dg2Bytes, facImageStart);
                    if (jpegStart != std::string::npos) {
                        imageFormat = "JPEG";
                        imageStart = jpegStart;
                        imageSize = jpegSize;
                    } else {
                        auto [jp2Start, jp2Size] = findJpeg2000(dg2Bytes, facImageStart);
                        if (jp2Start != std::string::npos) {
                            imageFormat = "JPEG2000";
                            imageStart = jp2Start;
                            imageSize = jp2Size;
                        }
                    }
                    searchPos = (imageStart != std::string::npos) ? imageStart + imageSize : facPos + 20;
                } else {
                    // No FAC, search for raw image data
                    auto [jpegStart, jpegSize] = findJpeg(dg2Bytes, searchPos);
                    if (jpegStart != std::string::npos) {
                        imageFormat = "JPEG";
                        imageStart = jpegStart;
                        imageSize = jpegSize;
                        searchPos = imageStart + imageSize;
                    } else {
                        auto [jp2Start, jp2Size] = findJpeg2000(dg2Bytes, searchPos);
                        if (jp2Start != std::string::npos) {
                            imageFormat = "JPEG2000";
                            imageStart = jp2Start;
                            imageSize = jp2Size;
                            searchPos = imageStart + imageSize;
                        } else {
                            break; // No more images found
                        }
                    }
                }

                if (imageStart != std::string::npos && imageSize > 0) {
                    faceCount++;
                    faceImage["index"] = faceCount;
                    faceImage["imageFormat"] = imageFormat;
                    faceImage["imageSize"] = static_cast<int>(imageSize);
                    faceImage["imageOffset"] = static_cast<int>(imageStart);

                    // Extract and encode image data
                    if (imageStart + imageSize <= dg2Bytes.size()) {
                        std::vector<uint8_t> imageData(dg2Bytes.begin() + imageStart,
                                                        dg2Bytes.begin() + imageStart + imageSize);

                        std::string mimeType;
                        if (imageFormat == "JPEG") {
                            mimeType = "image/jpeg";
                        } else if (imageFormat == "JPEG2000") {
                            mimeType = "image/jp2";
                        } else {
                            mimeType = "application/octet-stream";
                        }

                        faceImage["imageDataUrl"] = "data:" + mimeType + ";base64," + base64Encode(imageData);

                        // Try to extract image dimensions from JPEG header
                        if (imageFormat == "JPEG" && imageSize > 20) {
                            // Parse JPEG segments to find SOF0 marker for dimensions
                            for (size_t j = 0; j < imageSize - 10; j++) {
                                if (imageData[j] == 0xFF && imageData[j+1] == 0xC0) {
                                    // SOF0 marker found
                                    int height = (imageData[j+5] << 8) | imageData[j+6];
                                    int width = (imageData[j+7] << 8) | imageData[j+8];
                                    faceImage["width"] = width;
                                    faceImage["height"] = height;
                                    break;
                                }
                            }
                        }
                    }

                    faceImages.append(faceImage);

                    // Limit to prevent infinite loops
                    if (faceCount >= 10) break;
                } else {
                    break;
                }
            }

            result["faceCount"] = faceCount;
            result["faceImages"] = faceImages;
            result["biometricTemplateFound"] = foundBiometricTemplate;

            if (faceCount == 0) {
                result["warning"] = "No face images found in DG2 data";
                result["success"] = false;
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Parse SOD (Security Object) endpoint - Java compatible
    app.registerHandler(
        "/api/pa/parse-sod",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-sod");

            auto jsonBody = req->getJsonObject();
            std::string sodBase64;

            if (jsonBody) {
                sodBase64 = (*jsonBody).get("sodBase64", "").asString();
                if (sodBase64.empty()) {
                    sodBase64 = (*jsonBody).get("sod", "").asString();
                }
                if (sodBase64.empty()) {
                    sodBase64 = (*jsonBody).get("data", "").asString();
                }
            }

            if (sodBase64.empty()) {
                Json::Value error;
                error["error"] = "SOD data is required (sodBase64, sod, or data field)";
                error["success"] = false;
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            std::vector<uint8_t> sodBytes = base64Decode(sodBase64);
            if (sodBytes.empty()) {
                Json::Value error;
                error["error"] = "Invalid Base64 encoding";
                error["success"] = false;
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            Json::Value result;
            result["success"] = true;
            result["sodSize"] = static_cast<int>(sodBytes.size());

            try {
                // Extract hash algorithm
                std::string hashAlgorithm = extractHashAlgorithm(sodBytes);
                std::string hashAlgorithmOid = extractHashAlgorithmOid(sodBytes);
                result["hashAlgorithm"] = hashAlgorithm;
                result["hashAlgorithmOid"] = hashAlgorithmOid;

                // Extract signature algorithm
                std::string signatureAlgorithm = extractSignatureAlgorithm(sodBytes);
                result["signatureAlgorithm"] = signatureAlgorithm;

                // Extract DSC certificate info
                X509* dscCert = extractDscFromSod(sodBytes);
                if (dscCert) {
                    Json::Value dscInfo;

                    // Subject DN
                    char* subjectDn = X509_NAME_oneline(X509_get_subject_name(dscCert), nullptr, 0);
                    if (subjectDn) {
                        dscInfo["subjectDn"] = subjectDn;
                        OPENSSL_free(subjectDn);
                    }

                    // Issuer DN
                    char* issuerDn = X509_NAME_oneline(X509_get_issuer_name(dscCert), nullptr, 0);
                    if (issuerDn) {
                        dscInfo["issuerDn"] = issuerDn;
                        OPENSSL_free(issuerDn);
                    }

                    // Serial number
                    ASN1_INTEGER* serialAsn1 = X509_get_serialNumber(dscCert);
                    if (serialAsn1) {
                        BIGNUM* bn = ASN1_INTEGER_to_BN(serialAsn1, nullptr);
                        if (bn) {
                            char* serialHex = BN_bn2hex(bn);
                            if (serialHex) {
                                dscInfo["serialNumber"] = serialHex;
                                OPENSSL_free(serialHex);
                            }
                            BN_free(bn);
                        }
                    }

                    // Validity period
                    const ASN1_TIME* notBefore = X509_get0_notBefore(dscCert);
                    const ASN1_TIME* notAfter = X509_get0_notAfter(dscCert);

                    if (notBefore) {
                        BIO* bio = BIO_new(BIO_s_mem());
                        ASN1_TIME_print(bio, notBefore);
                        char buf[256];
                        int len = BIO_read(bio, buf, sizeof(buf) - 1);
                        if (len > 0) {
                            buf[len] = '\0';
                            dscInfo["notBefore"] = buf;
                        }
                        BIO_free(bio);
                    }

                    if (notAfter) {
                        BIO* bio = BIO_new(BIO_s_mem());
                        ASN1_TIME_print(bio, notAfter);
                        char buf[256];
                        int len = BIO_read(bio, buf, sizeof(buf) - 1);
                        if (len > 0) {
                            buf[len] = '\0';
                            dscInfo["notAfter"] = buf;
                        }
                        BIO_free(bio);
                    }

                    // Country code from issuer
                    X509_NAME* issuerName = X509_get_issuer_name(dscCert);
                    int countryIdx = X509_NAME_get_index_by_NID(issuerName, NID_countryName, -1);
                    if (countryIdx >= 0) {
                        X509_NAME_ENTRY* entry = X509_NAME_get_entry(issuerName, countryIdx);
                        if (entry) {
                            ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
                            if (data) {
                                unsigned char* utf8 = nullptr;
                                int utf8len = ASN1_STRING_to_UTF8(&utf8, data);
                                if (utf8len > 0) {
                                    dscInfo["countryCode"] = std::string(reinterpret_cast<char*>(utf8), utf8len);
                                    OPENSSL_free(utf8);
                                }
                            }
                        }
                    }

                    result["dscCertificate"] = dscInfo;
                    X509_free(dscCert);
                } else {
                    result["dscCertificate"] = Json::nullValue;
                    result["warning"] = "Failed to extract DSC certificate from SOD";
                }

                // Extract contained data groups
                std::map<int, std::vector<uint8_t>> dgHashes = parseDataGroupHashes(sodBytes);
                Json::Value containedDgs(Json::arrayValue);
                for (const auto& [dgNum, hash] : dgHashes) {
                    Json::Value dgInfo;
                    dgInfo["dgNumber"] = dgNum;
                    dgInfo["dgName"] = "DG" + std::to_string(dgNum);
                    dgInfo["hashValue"] = bytesToHex(hash);
                    dgInfo["hashLength"] = static_cast<int>(hash.size());
                    containedDgs.append(dgInfo);
                }
                result["containedDataGroups"] = containedDgs;
                result["dataGroupCount"] = static_cast<int>(dgHashes.size());

                // Check if ICAO wrapper (Tag 0x77) was present
                bool hasIcaoWrapper = (sodBytes.size() > 0 && sodBytes[0] == 0x77);
                result["hasIcaoWrapper"] = hasIcaoWrapper;

                // LDS version (if available from DG hashes)
                if (!dgHashes.empty()) {
                    // Check for DG14 (Active Authentication) and DG15 (Extended Access Control)
                    result["hasDg14"] = (dgHashes.find(14) != dgHashes.end());
                    result["hasDg15"] = (dgHashes.find(15) != dgHashes.end());
                }

            } catch (const std::exception& e) {
                spdlog::error("Error parsing SOD: {}", e.what());
                result["success"] = false;
                result["error"] = std::string("Failed to parse SOD: ") + e.what();
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Data groups endpoint with full DG1/DG2 parsing
    app.registerHandler(
        "/api/pa/{id}/datagroups",
        [&convertMrzDate, &convertMrzExpiryDate, &cleanMrzField](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& id) {
            spdlog::info("GET /api/pa/{}/datagroups", id);

            PGconn* conn = getDbConnection();
            Json::Value result;
            result["verificationId"] = id;
            result["hasDg1"] = false;
            result["hasDg2"] = false;

            if (!conn) {
                spdlog::error("Failed to connect to database for datagroups");
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            // Query using text result format (not binary) for dg_number
            std::string sql = "SELECT dg_number, dg_binary FROM pa_data_group "
                "WHERE verification_id = $1 ORDER BY dg_number";
            const char* paramValues[1] = {id.c_str()};
            PGresult* res = PQexecParams(conn, sql.c_str(), 1, nullptr, paramValues, nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                spdlog::error("Failed to query data groups: {}", PQerrorMessage(conn));
                PQclear(res);
                PQfinish(conn);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);
                return;
            }

            int nRows = PQntuples(res);
            spdlog::debug("Found {} data groups for verification {}", nRows, id);

            for (int i = 0; i < nRows; i++) {
                int dgNum = std::stoi(PQgetvalue(res, i, 0));

                // Get binary data (bytea field returns escaped format)
                char* binaryStr = PQgetvalue(res, i, 1);
                size_t binaryLen = 0;
                unsigned char* binaryData = PQunescapeBytea(reinterpret_cast<unsigned char*>(binaryStr), &binaryLen);

                if (!binaryData || binaryLen == 0) {
                    spdlog::warn("Empty DG{} data for verification {}", dgNum, id);
                    continue;
                }

                std::vector<uint8_t> dgBytes(binaryData, binaryData + binaryLen);
                PQfreemem(binaryData);

                if (dgNum == 1) {
                    result["hasDg1"] = true;
                    spdlog::debug("Parsing DG1 ({} bytes)", dgBytes.size());

                    // Parse DG1 (MRZ) - same logic as parse-dg1 endpoint
                    Json::Value dg1Result;
                    std::string mrzData;

                    for (size_t j = 0; j < dgBytes.size() - 2; j++) {
                        if (dgBytes[j] == 0x5F && dgBytes[j+1] == 0x1F) {
                            j += 2;
                            size_t mrzLen = dgBytes[j++];
                            if (mrzLen > 0x80) {
                                int numBytes = mrzLen & 0x7F;
                                mrzLen = 0;
                                for (int k = 0; k < numBytes; k++) {
                                    mrzLen = (mrzLen << 8) | dgBytes[j++];
                                }
                            }
                            if (j + mrzLen <= dgBytes.size()) {
                                mrzData = std::string(reinterpret_cast<const char*>(&dgBytes[j]), mrzLen);
                            }
                            break;
                        }
                    }

                    if (mrzData.length() >= 88) {
                        std::string line1 = mrzData.substr(0, 44);
                        std::string line2 = mrzData.substr(44, 44);

                        dg1Result["mrzLine1"] = line1;
                        dg1Result["mrzLine2"] = line2;
                        dg1Result["mrzFull"] = mrzData;
                        dg1Result["documentType"] = cleanMrzField(line1.substr(0, 2));
                        dg1Result["issuingCountry"] = line1.substr(2, 3);

                        // Parse name
                        size_t nameEnd = line1.find("<<", 5);
                        std::string surname, givenNames;
                        if (nameEnd != std::string::npos) {
                            surname = line1.substr(5, nameEnd - 5);
                            std::replace(surname.begin(), surname.end(), '<', ' ');
                            while (!surname.empty() && surname.back() == ' ') surname.pop_back();
                            while (!surname.empty() && surname.front() == ' ') surname.erase(0, 1);
                            dg1Result["surname"] = surname;

                            std::string givenPart = line1.substr(nameEnd + 2);
                            std::replace(givenPart.begin(), givenPart.end(), '<', ' ');
                            while (!givenPart.empty() && givenPart.back() == ' ') givenPart.pop_back();
                            while (!givenPart.empty() && givenPart.front() == ' ') givenPart.erase(0, 1);
                            givenNames = givenPart;
                            dg1Result["givenNames"] = givenNames;
                        }

                        dg1Result["fullName"] = !surname.empty() ? (surname + " " + givenNames) : givenNames;
                        dg1Result["documentNumber"] = cleanMrzField(line2.substr(0, 9));
                        dg1Result["nationality"] = line2.substr(10, 3);
                        dg1Result["dateOfBirth"] = convertMrzDate(line2.substr(13, 6));
                        dg1Result["sex"] = line2.substr(20, 1);
                        dg1Result["expirationDate"] = convertMrzExpiryDate(line2.substr(21, 6));

                        result["dg1"] = dg1Result;
                        spdlog::debug("DG1 parsed: documentNumber={}", dg1Result["documentNumber"].asString());
                    } else if (mrzData.length() >= 72) {
                        // TD2 format
                        std::string line1 = mrzData.substr(0, 36);
                        std::string line2 = mrzData.substr(36, 36);

                        dg1Result["mrzLine1"] = line1;
                        dg1Result["mrzLine2"] = line2;
                        dg1Result["documentNumber"] = cleanMrzField(line2.substr(0, 9));
                        dg1Result["nationality"] = line2.substr(10, 3);
                        dg1Result["dateOfBirth"] = convertMrzDate(line2.substr(13, 6));
                        dg1Result["sex"] = line2.substr(20, 1);
                        dg1Result["expirationDate"] = convertMrzExpiryDate(line2.substr(21, 6));

                        result["dg1"] = dg1Result;
                    }
                } else if (dgNum == 2) {
                    result["hasDg2"] = true;
                    spdlog::debug("Parsing DG2 ({} bytes)", dgBytes.size());

                    // Parse DG2 (Face Image) - simplified version
                    Json::Value dg2Result;
                    Json::Value faceImages(Json::arrayValue);

                    // Find JPEG or JPEG2000 image markers
                    auto findImageStart = [](const std::vector<uint8_t>& data, size_t start)
                        -> std::pair<size_t, std::string> {
                        for (size_t i = start; i < data.size() - 4; i++) {
                            // JPEG: FF D8 FF
                            if (data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF) {
                                return {i, "JPEG"};
                            }
                            // JPEG2000: 00 00 00 0C 6A 50
                            if (i + 5 < data.size() &&
                                data[i] == 0x00 && data[i+1] == 0x00 &&
                                data[i+2] == 0x00 && data[i+3] == 0x0C &&
                                data[i+4] == 0x6A && data[i+5] == 0x50) {
                                return {i, "JP2"};
                            }
                        }
                        return {std::string::npos, ""};
                    };

                    auto findImageEnd = [](const std::vector<uint8_t>& data, size_t start, const std::string& format)
                        -> size_t {
                        if (format == "JPEG") {
                            // Find JPEG EOI marker: FF D9
                            for (size_t i = start + 2; i < data.size() - 1; i++) {
                                if (data[i] == 0xFF && data[i+1] == 0xD9) {
                                    return i + 2;
                                }
                            }
                        } else if (format == "JP2") {
                            // For JP2, look for next image or end of data
                            for (size_t i = start + 12; i < data.size() - 4; i++) {
                                if (data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF) {
                                    return i;
                                }
                            }
                            return data.size();
                        }
                        return data.size();
                    };

                    auto [imgStart, imgFormat] = findImageStart(dgBytes, 0);
                    if (imgStart != std::string::npos) {
                        size_t imgEnd = findImageEnd(dgBytes, imgStart, imgFormat);

                        std::vector<uint8_t> imageData(dgBytes.begin() + imgStart, dgBytes.begin() + imgEnd);

                        // Base64 encode the image
                        std::string base64Image = base64Encode(imageData);
                        std::string mimeType = (imgFormat == "JPEG") ? "image/jpeg" : "image/jp2";
                        std::string dataUrl = "data:" + mimeType + ";base64," + base64Image;

                        Json::Value faceImage;
                        faceImage["imageFormat"] = imgFormat;
                        faceImage["imageSize"] = static_cast<int>(imageData.size());
                        faceImage["imageDataUrl"] = dataUrl;
                        faceImages.append(faceImage);

                        spdlog::debug("DG2 face image found: format={}, size={}", imgFormat, imageData.size());
                    }

                    dg2Result["faceCount"] = faceImages.size();
                    dg2Result["faceImages"] = faceImages;
                    result["dg2"] = dg2Result;
                }
            }

            PQclear(res);
            PQfinish(conn);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Root endpoint
    app.registerHandler(
        "/",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["name"] = "PA Service";
            result["description"] = "ICAO Passive Authentication Service - ePassport PA Verification";
            result["version"] = "2.0.0";
            result["endpoints"]["health"] = "/api/health";
            result["endpoints"]["pa"] = "/api/pa";

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // API info endpoint
    app.registerHandler(
        "/api",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["api"] = "PA Service REST API";
            result["version"] = "v2";

            Json::Value endpoints(Json::arrayValue);

            Json::Value verify;
            verify["method"] = "POST";
            verify["path"] = "/api/pa/verify";
            verify["description"] = "Perform Passive Authentication verification";
            endpoints.append(verify);

            Json::Value history;
            history["method"] = "GET";
            history["path"] = "/api/pa/history";
            history["description"] = "Get PA verification history";
            endpoints.append(history);

            Json::Value stats;
            stats["method"] = "GET";
            stats["path"] = "/api/pa/statistics";
            stats["description"] = "Get PA verification statistics";
            endpoints.append(stats);

            result["endpoints"] = endpoints;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // OpenAPI specification endpoint
    app.registerHandler(
        "/api/openapi.yaml",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/openapi.yaml");

            std::string openApiSpec = R"(openapi: 3.0.3
info:
  title: PA Service API
  description: ICAO 9303 Passive Authentication Verification Service
  version: 2.0.0
servers:
  - url: /
tags:
  - name: Health
    description: Health check endpoints
  - name: PA
    description: Passive Authentication operations
  - name: Parser
    description: Document parsing utilities
paths:
  /api/health:
    get:
      tags: [Health]
      summary: Application health check
      responses:
        '200':
          description: Service is healthy
  /api/health/database:
    get:
      tags: [Health]
      summary: Database health check
      responses:
        '200':
          description: Database status
  /api/health/ldap:
    get:
      tags: [Health]
      summary: LDAP health check
      responses:
        '200':
          description: LDAP status
  /api/pa/verify:
    post:
      tags: [PA]
      summary: Verify Passive Authentication
      description: Perform complete ICAO 9303 PA verification
      requestBody:
        content:
          application/json:
            schema:
              type: object
              required: [sod, dataGroups]
              properties:
                sod:
                  type: string
                  description: Base64 encoded SOD
                dataGroups:
                  type: object
                  description: Map of DG number to Base64 data
      responses:
        '200':
          description: Verification result
  /api/pa/statistics:
    get:
      tags: [PA]
      summary: Get PA statistics
      responses:
        '200':
          description: PA verification statistics
  /api/pa/history:
    get:
      tags: [PA]
      summary: Get PA verification history
      parameters:
        - name: limit
          in: query
          schema:
            type: integer
        - name: offset
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: PA history list
  /api/pa/{id}:
    get:
      tags: [PA]
      summary: Get verification details
      parameters:
        - name: id
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: Verification details
  /api/pa/{id}/datagroups:
    get:
      tags: [PA]
      summary: Get data groups info
      parameters:
        - name: id
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: Data groups information
  /api/pa/parse-dg1:
    post:
      tags: [Parser]
      summary: Parse DG1 (MRZ) data
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                dg1:
                  type: string
      responses:
        '200':
          description: Parsed MRZ data
  /api/pa/parse-dg2:
    post:
      tags: [Parser]
      summary: Parse DG2 (Face Image)
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                dg2:
                  type: string
      responses:
        '200':
          description: Extracted face image
  /api/pa/parse-mrz-text:
    post:
      tags: [Parser]
      summary: Parse MRZ text
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                mrz:
                  type: string
      responses:
        '200':
          description: Parsed MRZ data
  /api/pa/parse-sod:
    post:
      tags: [Parser]
      summary: Parse SOD (Security Object)
      description: Extract metadata from SOD including DSC certificate, hash algorithm, and contained data groups
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                sod:
                  type: string
                  description: Base64 encoded SOD data
      responses:
        '200':
          description: Parsed SOD metadata
          content:
            application/json:
              schema:
                type: object
                properties:
                  success:
                    type: boolean
                  hashAlgorithm:
                    type: string
                  signatureAlgorithm:
                    type: string
                  dscCertificate:
                    type: object
                  containedDataGroups:
                    type: array
)";

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(openApiSpec);
            resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            resp->addHeader("Content-Type", "application/x-yaml");
            callback(resp);
        },
        {drogon::Get}
    );

    // Swagger UI redirect
    app.registerHandler(
        "/api/docs",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newRedirectionResponse("/swagger-ui/index.html");
            callback(resp);
        },
        {drogon::Get}
    );

    spdlog::info("PA Service API routes registered");
}

} // anonymous namespace

// =============================================================================
// Main Entry Point
// =============================================================================

int main(int /* argc */, char* /* argv */[]) {
    printBanner();
    initializeLogging();

    appConfig = AppConfig::fromEnvironment();

    spdlog::info("Starting PA Service v2.0.0...");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP: {}:{}", appConfig.ldapHost, appConfig.ldapPort);

    try {
        auto& app = drogon::app();

        app.setLogPath("logs")
           .setLogLevel(trantor::Logger::kInfo)
           .addListener("0.0.0.0", appConfig.serverPort)
           .setThreadNum(appConfig.threadNum)
           .enableGzip(true)
           .setClientMaxBodySize(50 * 1024 * 1024)
           .setDocumentRoot("./static");

        // Enable CORS
        app.registerPreSendingAdvice([](const drogon::HttpRequestPtr& /* req */,
                                         const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-User-Id");
        });

        // Handle OPTIONS requests for CORS preflight
        app.registerHandler(
            "/{path}",
            [](const drogon::HttpRequestPtr& /* req */,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& /* path */) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                callback(resp);
            },
            {drogon::Options}
        );

        registerRoutes();

        spdlog::info("Server starting on http://0.0.0.0:{}", appConfig.serverPort);
        spdlog::info("Press Ctrl+C to stop the server");

        app.run();

    } catch (const std::exception& e) {
        spdlog::error("Application error: {}", e.what());
        return 1;
    }

    spdlog::info("Server stopped");
    return 0;
}

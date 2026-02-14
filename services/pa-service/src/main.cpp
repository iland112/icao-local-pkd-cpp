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
 * @version 2.1.1
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

#include <icao/audit/audit_log.h>
#include "db_connection_pool.h"
#include "db_connection_pool_factory.h"

#include "repositories/pa_verification_repository.h"
#include "repositories/data_group_repository.h"
#include "repositories/ldap_certificate_repository.h"
#include "repositories/ldap_crl_repository.h"
// ICAO 9303 Parser Library (Shared)
#include <sod_parser.h>
#include <dg_parser.h>
#include "services/certificate_validation_service.h"
#include "services/dsc_auto_registration_service.h"
#include "services/pa_verification_service.h"
#include "common/country_code_utils.h"

namespace {

// Suppress warnings for legacy unused functions (kept for reference)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// --- Algorithm OID Mappings ---

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

// --- CRL Status Enum ---

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

// --- Result Structures ---

struct CertificateChainValidationResult {
    bool valid = false;
    std::string dscSubject;
    std::string dscSerialNumber;
    std::string cscaSubject;
    std::string cscaSerialNumber;
    std::string notBefore;
    std::string notAfter;
    // Certificate expiration status (ICAO 9303 - point-in-time validation)
    bool dscExpired = false;           // DSC certificate currently expired
    bool cscaExpired = false;          // CSCA certificate currently expired
    bool validAtSigningTime = true;    // Was valid at document signing time
    std::string expirationStatus;      // "VALID", "WARNING", "EXPIRED"
    std::string expirationMessage;     // Human-readable expiration message
    bool crlChecked = false;
    bool revoked = false;
    CrlStatus crlStatus = CrlStatus::NOT_CHECKED;
    std::string crlStatusDescription;
    std::string crlStatusDetailedDescription;
    std::string crlStatusSeverity;
    std::string crlMessage;
    std::string crlThisUpdate;     // CRL thisUpdate (발행일)
    std::string crlNextUpdate;     // CRL nextUpdate (다음 갱신일)
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

// --- Application Configuration ---

struct AppConfig {
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "localpkd";
    std::string dbUser = "localpkd";
    std::string dbPassword;  // Must be set via environment variable

    // LDAP Read: HAProxy for load balancing
    std::string ldapHost = "haproxy";
    int ldapPort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword;  // Must be set via environment variable
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

    // Validate required credentials are set
    void validateRequiredCredentials() const {
        if (dbPassword.empty()) {
            throw std::runtime_error("FATAL: DB_PASSWORD environment variable not set");
        }
        if (ldapBindPassword.empty()) {
            throw std::runtime_error("FATAL: LDAP_BIND_PASSWORD environment variable not set");
        }
        spdlog::info("✅ All required credentials loaded from environment");
    }
};

AppConfig appConfig;

std::shared_ptr<common::IDbConnectionPool> dbPool;
std::unique_ptr<common::IQueryExecutor> queryExecutor;

// --- Global Service and Repository Pointers ---

// Repositories
repositories::PaVerificationRepository* paVerificationRepository = nullptr;
repositories::DataGroupRepository* dataGroupRepository = nullptr;
repositories::LdapCertificateRepository* ldapCertificateRepository = nullptr;
repositories::LdapCrlRepository* ldapCrlRepository = nullptr;

// Services
icao::SodParser* sodParserService = nullptr;
icao::DgParser* dataGroupParserService = nullptr;
services::CertificateValidationService* certificateValidationService = nullptr;
services::DscAutoRegistrationService* dscAutoRegistrationService = nullptr;
services::PaVerificationService* paVerificationService = nullptr;

// --- Utility Functions ---

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
    if (!bio) return {};
    BIO* b64 = BIO_new(BIO_f_base64());
    if (!b64) { BIO_free(bio); return {}; }
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

// --- X509 Helper Functions ---

std::string getX509SubjectDn(X509* cert) {
    if (!cert) return "";
    char* dn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (!dn) return "";
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

std::string getX509IssuerDn(X509* cert) {
    if (!cert) return "";
    char* dn = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    if (!dn) return "";
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

std::string getX509SerialNumber(X509* cert) {
    if (!cert) return "";
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    if (!serial) return "";
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    if (!bn) return "";
    char* hex = BN_bn2hex(bn);
    if (!hex) { BN_free(bn); return ""; }
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

// --- Logging Initialization ---

void printBanner() {
    std::cout << R"(
  ____   _      ____                  _
 |  _ \ / \    / ___|  ___ _ ____   _(_) ___ ___
 | |_) / _ \   \___ \ / _ \ '__\ \ / / |/ __/ _ \
 |  __/ ___ \   ___) |  __/ |   \ V /| | (_|  __/
 |_| /_/   \_\ |____/ \___|_|    \_/ |_|\___\___|

)" << std::endl;
    std::cout << "  PA Service - ICAO Passive Authentication" << std::endl;
    std::cout << "  Version: 2.1.0 LDAP-RETRY" << std::endl;
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

// --- Database Health Check ---

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

// --- LDAP Functions ---

Json::Value checkLdap() {
    Json::Value result;
    result["name"] = "ldap";

    auto start = std::chrono::steady_clock::now();

    std::string ldapUri = "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort);
    LDAP* ld = nullptr;
    int rc = ldap_initialize(&ld, ldapUri.c_str());

    if (rc == LDAP_SUCCESS) {
        int version = LDAP_VERSION3;
        ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

        struct timeval tv = {3, 0};  // 3 second timeout
        ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv);

        // Anonymous bind to verify connectivity
        struct berval cred = {0, nullptr};
        rc = ldap_sasl_bind_s(ld, nullptr, LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
        ldap_unbind_ext_s(ld, nullptr, nullptr);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (rc == LDAP_SUCCESS) {
        result["status"] = "UP";
        result["responseTimeMs"] = static_cast<int>(duration.count());
        result["uri"] = ldapUri;
    } else {
        result["status"] = "DOWN";
        result["error"] = std::string("LDAP connection failed: ") + ldap_err2string(rc);
    }

    return result;
}

LDAP* getLdapConnection() {
    std::string ldapUri = "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort);
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 100;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        LDAP* ld = nullptr;
        int rc = ldap_initialize(&ld, ldapUri.c_str());
        if (rc != LDAP_SUCCESS) {
            spdlog::warn("LDAP initialize failed (attempt {}/{}): {}", attempt, MAX_RETRIES, ldap_err2string(rc));
            if (attempt < MAX_RETRIES) {
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                continue;
            }
            return nullptr;
        }

        int version = LDAP_VERSION3;
        ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

        // Set network timeout
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &timeout);

        struct berval cred;
        cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
        cred.bv_len = appConfig.ldapBindPassword.length();

        rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
        if (rc != LDAP_SUCCESS) {
            spdlog::warn("LDAP bind failed (attempt {}/{}): {}", attempt, MAX_RETRIES, ldap_err2string(rc));
            ldap_unbind_ext_s(ld, nullptr, nullptr);
            if (attempt < MAX_RETRIES) {
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                continue;
            }
            return nullptr;
        }

        spdlog::debug("LDAP connection established (attempt {})", attempt);
        return ld;
    }

    return nullptr;
}

// --- SOD Parsing Functions (OpenSSL CMS API) ---

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
    if (!bio) return "";
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        return "";
    }

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
    if (!bio) return "UNKNOWN";
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
    if (!bio) { spdlog::error("Failed to create BIO for DG hashes"); return result; }
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
    long dataLen = ASN1_STRING_length(*contentPtr);

    // Parse LDSSecurityObject ASN.1
    const unsigned char* contentData = p;
    const unsigned char* end = p + dataLen;  // Buffer boundary for all checks

    // Skip outer SEQUENCE tag and length
    if (contentData >= end || *contentData != 0x30) {
        spdlog::error("Expected SEQUENCE tag for LDSSecurityObject");
        CMS_ContentInfo_free(cms);
        return result;
    }
    contentData++;

    // Parse length
    if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
    size_t contentLen = 0;
    if (*contentData & 0x80) {
        int numBytes = *contentData & 0x7F;
        contentData++;
        if (contentData + numBytes > end) { CMS_ContentInfo_free(cms); return result; }
        for (int i = 0; i < numBytes; i++) {
            contentLen = (contentLen << 8) | *contentData++;
        }
    } else {
        contentLen = *contentData++;
    }

    // Skip version (INTEGER)
    if (contentData < end && *contentData == 0x02) {
        contentData++;
        if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
        size_t versionLen = *contentData++;
        if (contentData + versionLen > end) { CMS_ContentInfo_free(cms); return result; }
        contentData += versionLen;
    }

    // Skip hashAlgorithm (SEQUENCE - AlgorithmIdentifier)
    if (contentData < end && *contentData == 0x30) {
        contentData++;
        if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
        size_t algLen = 0;
        if (*contentData & 0x80) {
            int numBytes = *contentData & 0x7F;
            contentData++;
            if (contentData + numBytes > end) { CMS_ContentInfo_free(cms); return result; }
            for (int i = 0; i < numBytes; i++) {
                algLen = (algLen << 8) | *contentData++;
            }
        } else {
            algLen = *contentData++;
        }
        if (contentData + algLen > end) { CMS_ContentInfo_free(cms); return result; }
        contentData += algLen;
    }

    // Parse dataGroupHashValues (SEQUENCE OF DataGroupHash)
    if (contentData < end && *contentData == 0x30) {
        contentData++;
        if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
        size_t dgHashesLen = 0;
        if (*contentData & 0x80) {
            int numBytes = *contentData & 0x7F;
            contentData++;
            if (contentData + numBytes > end) { CMS_ContentInfo_free(cms); return result; }
            for (int i = 0; i < numBytes; i++) {
                dgHashesLen = (dgHashesLen << 8) | *contentData++;
            }
        } else {
            dgHashesLen = *contentData++;
        }

        // Clamp dgHashesEnd to buffer boundary
        const unsigned char* dgHashesEnd = contentData + dgHashesLen;
        if (dgHashesEnd > end) dgHashesEnd = end;

        // Parse each DataGroupHash
        while (contentData < dgHashesEnd) {
            if (*contentData != 0x30) break;
            contentData++;
            if (contentData >= dgHashesEnd) break;

            size_t dgHashLen = 0;
            if (*contentData & 0x80) {
                int numBytes = *contentData & 0x7F;
                contentData++;
                if (contentData + numBytes > dgHashesEnd) break;
                for (int i = 0; i < numBytes; i++) {
                    dgHashLen = (dgHashLen << 8) | *contentData++;
                }
            } else {
                dgHashLen = *contentData++;
            }

            const unsigned char* dgHashEnd = contentData + dgHashLen;
            if (dgHashEnd > dgHashesEnd) break;

            // Parse dataGroupNumber (INTEGER)
            int dgNumber = 0;
            if (contentData < dgHashEnd && *contentData == 0x02) {
                contentData++;
                if (contentData >= dgHashEnd) { contentData = dgHashEnd; continue; }
                size_t intLen = *contentData++;
                if (contentData + intLen > dgHashEnd) { contentData = dgHashEnd; continue; }
                for (size_t i = 0; i < intLen; i++) {
                    dgNumber = (dgNumber << 8) | *contentData++;
                }
            }

            // Parse dataGroupHashValue (OCTET STRING)
            if (contentData < dgHashEnd && *contentData == 0x04) {
                contentData++;
                if (contentData >= dgHashEnd) { contentData = dgHashEnd; continue; }
                size_t hashLen = 0;
                if (*contentData & 0x80) {
                    int numBytes = *contentData & 0x7F;
                    contentData++;
                    if (contentData + numBytes > dgHashEnd) { contentData = dgHashEnd; continue; }
                    for (int i = 0; i < numBytes; i++) {
                        hashLen = (hashLen << 8) | *contentData++;
                    }
                } else {
                    hashLen = *contentData++;
                }

                if (contentData + hashLen <= dgHashEnd) {
                    std::vector<uint8_t> hashValue(contentData, contentData + hashLen);
                    result[dgNumber] = hashValue;
                    contentData += hashLen;
                    spdlog::debug("Parsed DG{} hash: {} bytes", dgNumber, hashLen);
                }
            }

            contentData = dgHashEnd;
        }
    }

    CMS_ContentInfo_free(cms);
    spdlog::info("Parsed {} Data Group hashes from SOD", result.size());
    return result;
}

// --- Hash Calculation Functions ---

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
    if (!ctx) {
        spdlog::error("Failed to create EVP_MD_CTX");
        return {};
    }
    std::vector<uint8_t> hash(EVP_MAX_MD_SIZE);
    unsigned int hashLen = 0;

    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash.data(), &hashLen);
    EVP_MD_CTX_free(ctx);

    hash.resize(hashLen);
    return hash;
}

// --- LDAP CSCA Lookup Functions ---

/**
 * @brief Helper function to search CSCA in a specific organizational unit
 */
X509* searchCscaInOu(LDAP* ld, const std::string& ou, const std::string& countryCode, const std::string& issuerCn) {
    std::string baseDn = "o=" + ou + ",c=" + countryCode + ",dc=data," + appConfig.ldapBaseDn;
    std::string filter = "(objectClass=pkdDownload)";
    char* attrs[] = {const_cast<char*>("userCertificate;binary"), nullptr};

    spdlog::debug("Searching CSCA in LDAP: base={}, filter={}", baseDn, filter);

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(ld, baseDn.c_str(), LDAP_SCOPE_SUBTREE, filter.c_str(),
                               attrs, 0, nullptr, nullptr, nullptr, 100, &res);

    if (rc != LDAP_SUCCESS) {
        spdlog::debug("LDAP search in {} failed: {}", baseDn, ldap_err2string(rc));
        if (res) ldap_msgfree(res);
        return nullptr;
    }

    X509* cscaCert = nullptr;
    X509* fallbackCsca = nullptr;
    std::string issuerCnLower = toLower(issuerCn);

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
                    spdlog::info("Found exact matching CSCA in {}: {}", baseDn, certSubject);
                    ldap_value_free_len(values);
                    break;  // Found exact match, stop searching
                }
                // Partial match - issuer CN contains CSCA CN or vice versa
                else if (issuerCnLower.find(certCnLower) != std::string::npos ||
                         certCnLower.find(issuerCnLower) != std::string::npos) {
                    if (!cscaCert) {
                        cscaCert = cert;
                        spdlog::info("Found partial matching CSCA in {}: {}", baseDn, certSubject);
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
        spdlog::debug("No exact CSCA match found in {}, using fallback", baseDn);
        return fallbackCsca;
    }

    return nullptr;
}

/**
 * @brief Retrieve CSCA certificate from LDAP by issuer DN
 *
 * Searches in both o=csca (self-signed) and o=lc (Link Certificates).
 */
X509* retrieveCscaFromLdap(LDAP* ld, const std::string& issuerDn) {
    if (!ld) return nullptr;

    // Extract country code from issuer DN
    std::string countryCode = extractCountryFromDn(issuerDn);
    if (countryCode.empty()) {
        spdlog::warn("Could not extract country code from issuer DN: {}", issuerDn);
        return nullptr;
    }

    std::string issuerCn = extractCnFromDn(issuerDn);
    spdlog::debug("Looking for CSCA matching issuer CN: {} in country: {}", issuerCn, countryCode);

    // Try o=csca first, then o=lc (Link Certificates)
    X509* cscaCert = searchCscaInOu(ld, "csca", countryCode, issuerCn);
    if (cscaCert) {
        return cscaCert;
    }

    // If not found in o=csca, try o=lc (Link Certificate CSCA)
    spdlog::debug("CSCA not found in o=csca, trying o=lc (Link Certificates)");
    cscaCert = searchCscaInOu(ld, "lc", countryCode, issuerCn);
    if (cscaCert) {
        return cscaCert;
    }

    spdlog::warn("No CSCA found for issuer: {} in either o=csca or o=lc", issuerDn);
    return nullptr;
}

/**
 * @brief Search CRL from LDAP for a given CSCA
 */
X509_CRL* searchCrlFromLdap(LDAP* ld, const std::string& countryCode) {
    if (!ld) return nullptr;

    std::string baseDn = "o=crl,c=" + countryCode + ",dc=data," + appConfig.ldapBaseDn;
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

// --- Verification Functions ---

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
    if (!bio) {
        result.valid = false;
        result.validationErrors = "Failed to create BIO";
        return result;
    }
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        result.valid = false;
        result.validationErrors = "Failed to parse CMS structure";
        return result;
    }

    // Create certificate store with DSC
    X509_STORE* store = X509_STORE_new();
    if (!store) {
        result.valid = false;
        result.validationErrors = "Failed to create X509 store";
        CMS_ContentInfo_free(cms);
        return result;
    }
    STACK_OF(X509)* certs = sk_X509_new_null();
    if (!certs) {
        result.valid = false;
        result.validationErrors = "Failed to create certificate stack";
        X509_STORE_free(store);
        CMS_ContentInfo_free(cms);
        return result;
    }
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

    // Check certificate expiration status (ICAO 9303 - point-in-time validation)
    time_t now = time(nullptr);

    // Check DSC expiration
    const ASN1_TIME* dscNotAfter = X509_get0_notAfter(dscCert);
    if (dscNotAfter) {
        int dscExpCmp = X509_cmp_time(dscNotAfter, &now);
        result.dscExpired = (dscExpCmp < 0);  // expired if notAfter < now
    }

    // Check CSCA expiration
    const ASN1_TIME* cscaNotAfter = X509_get0_notAfter(cscaCert);
    if (cscaNotAfter) {
        int cscaExpCmp = X509_cmp_time(cscaNotAfter, &now);
        result.cscaExpired = (cscaExpCmp < 0);  // expired if notAfter < now
    }

    // Set expiration status and message
    // Per ICAO 9303: Trust Chain validation is still valid if certificate was valid at signing time
    result.validAtSigningTime = true;  // Assumed true - would need document signing date for accurate check

    if (result.dscExpired && result.cscaExpired) {
        result.expirationStatus = "EXPIRED";
        result.expirationMessage = "DSC 및 CSCA 인증서가 모두 만료됨. 단, 서명 당시에는 유효했을 수 있음 (ICAO 9303 기준)";
    } else if (result.dscExpired) {
        result.expirationStatus = "EXPIRED";
        result.expirationMessage = "DSC 인증서가 만료됨. 단, 서명 당시에는 유효했을 수 있음 (ICAO 9303 기준)";
    } else if (result.cscaExpired) {
        result.expirationStatus = "WARNING";
        result.expirationMessage = "CSCA 인증서가 만료됨. DSC 인증서는 유효함";
    } else {
        // Check if expiring soon (within 90 days)
        const ASN1_TIME* dscNotAfterCheck = X509_get0_notAfter(dscCert);
        time_t future = now + (90 * 24 * 60 * 60);  // 90 days from now
        int expiringSoon = X509_cmp_time(dscNotAfterCheck, &future);

        if (expiringSoon < 0) {
            result.expirationStatus = "WARNING";
            result.expirationMessage = "DSC 인증서가 90일 이내에 만료 예정";
        } else {
            result.expirationStatus = "VALID";
            result.expirationMessage = "";
        }
    }

    if (result.dscExpired || result.cscaExpired) {
        spdlog::info("Certificate expiration check - DSC expired: {}, CSCA expired: {}, Status: {}",
            result.dscExpired, result.cscaExpired, result.expirationStatus);
    }

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

        // Extract CRL thisUpdate / nextUpdate dates
        auto asn1TimeToString = [](const ASN1_TIME* t) -> std::string {
            if (!t) return "";
            struct tm tm_val;
            if (ASN1_TIME_to_tm(t, &tm_val) == 1) {
                char buf[32];
                strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
                return std::string(buf);
            }
            return "";
        };

        const ASN1_TIME* thisUpdate = X509_CRL_get0_lastUpdate(crl);
        const ASN1_TIME* nextUpdate = X509_CRL_get0_nextUpdate(crl);
        result.crlThisUpdate = asn1TimeToString(thisUpdate);
        result.crlNextUpdate = asn1TimeToString(nextUpdate);

        spdlog::info("CRL dates - thisUpdate: {}, nextUpdate: {}", result.crlThisUpdate, result.crlNextUpdate);

        // Check CRL expiration (nextUpdate < now)
        bool crlExpired = false;
        if (nextUpdate) {
            time_t now = time(nullptr);
            if (X509_cmp_time(nextUpdate, &now) < 0) {
                crlExpired = true;
            }
        }

        if (crlExpired) {
            result.revoked = false;
            result.crlStatus = CrlStatus::CRL_EXPIRED;
            result.crlStatusDescription = "CRL has expired";
            result.crlStatusDetailedDescription = "CRL의 nextUpdate(" + result.crlNextUpdate + ")가 현재 시간보다 이전임. "
                "만료된 CRL로는 폐기 상태를 신뢰할 수 없습니다. ICAO Doc 9303 Part 11에 따라 경고 처리합니다.";
            result.crlStatusSeverity = "WARNING";
            result.crlMessage = "CRL 만료됨 (nextUpdate: " + result.crlNextUpdate + ")";
            spdlog::warn("CRL expired for country {} (nextUpdate: {})", countryCode, result.crlNextUpdate);
        } else {
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
                    result.crlMessage = std::string("Certificate revoked on ") + asn1TimeToString(revTime);
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

// --- Database Functions ---

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

// --- JSON Response Builders ---

Json::Value buildCertificateChainValidationJson(const CertificateChainValidationResult& result) {
    Json::Value json;
    json["valid"] = result.valid;
    json["dscSubject"] = result.dscSubject;
    json["dscSerialNumber"] = result.dscSerialNumber;
    json["cscaSubject"] = result.cscaSubject;
    json["cscaSerialNumber"] = result.cscaSerialNumber;
    json["notBefore"] = result.notBefore;
    json["notAfter"] = result.notAfter;
    // Certificate expiration status (ICAO 9303)
    json["dscExpired"] = result.dscExpired;
    json["cscaExpired"] = result.cscaExpired;
    json["validAtSigningTime"] = result.validAtSigningTime;
    json["expirationStatus"] = result.expirationStatus;
    if (!result.expirationMessage.empty()) {
        json["expirationMessage"] = result.expirationMessage;
    }
    json["crlChecked"] = result.crlChecked;
    json["revoked"] = result.revoked;
    json["crlStatus"] = crlStatusToString(result.crlStatus);
    json["crlStatusDescription"] = result.crlStatusDescription;
    json["crlStatusDetailedDescription"] = result.crlStatusDetailedDescription;
    json["crlStatusSeverity"] = result.crlStatusSeverity;
    json["crlMessage"] = result.crlMessage;
    if (!result.crlThisUpdate.empty()) {
        json["crlThisUpdate"] = result.crlThisUpdate;
    }
    if (!result.crlNextUpdate.empty()) {
        json["crlNextUpdate"] = result.crlNextUpdate;
    }
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

// --- Service Initialization ---

/**
 * @brief Initialize all services and repositories with dependency injection
 *
 * Initialization order:
 * 1. Database connection pool (Factory Pattern)
 * 2. Query Executor (database abstraction)
 * 3. LDAP connection
 * 4. Repositories (with Query Executor/LDAP injection)
 * 5. Services (with repository injection)
 *
 */
void initializeServices() {
    spdlog::info("Initializing Repository Pattern services...");

    try {
        // Step 1: Initialize database connection pool
        spdlog::debug("Creating database connection pool using Factory Pattern...");
        dbPool = common::DbConnectionPoolFactory::createFromEnv();

        if (!dbPool) {
            throw std::runtime_error("Failed to create database connection pool from environment");
        }

        if (!dbPool->initialize()) {
            throw std::runtime_error("Failed to initialize database connection pool");
        }

        std::string dbType = dbPool->getDatabaseType();
        spdlog::info("✅ Database connection pool initialized (type={})", dbType);

        // Step 2: Create Query Executor
        spdlog::debug("Creating Query Executor from connection pool...");
        queryExecutor = common::createQueryExecutor(dbPool.get());
        if (!queryExecutor) {
            throw std::runtime_error("Failed to create Query Executor");
        }
        spdlog::info("✅ Query Executor initialized (DB type: {})", queryExecutor->getDatabaseType());

        // Step 3: Get LDAP connection
        LDAP* ldapConn = getLdapConnection();
        if (!ldapConn) {
            throw std::runtime_error("Failed to get LDAP connection");
        }

        // Step 4: Initialize Repositories
        spdlog::debug("Creating PaVerificationRepository...");
        paVerificationRepository = new repositories::PaVerificationRepository(queryExecutor.get());

        spdlog::debug("Creating DataGroupRepository...");
        dataGroupRepository = new repositories::DataGroupRepository(queryExecutor.get());

        spdlog::debug("Creating LdapCertificateRepository...");
        ldapCertificateRepository = new repositories::LdapCertificateRepository(
            ldapConn,
            appConfig.ldapBaseDn
        );

        spdlog::debug("Creating LdapCrlRepository...");
        ldapCrlRepository = new repositories::LdapCrlRepository(
            ldapConn,
            appConfig.ldapBaseDn
        );

        // Step 5: Initialize Services (constructor-based dependency injection)
        spdlog::debug("Creating icao::SodParser...");
        sodParserService = new icao::SodParser();

        spdlog::debug("Creating icao::DgParser...");
        dataGroupParserService = new icao::DgParser();

        spdlog::debug("Creating CertificateValidationService...");
        certificateValidationService = new services::CertificateValidationService(
            ldapCertificateRepository,
            ldapCrlRepository
        );

        spdlog::debug("Creating DscAutoRegistrationService...");
        dscAutoRegistrationService = new services::DscAutoRegistrationService(
            queryExecutor.get()
        );

        spdlog::debug("Creating PaVerificationService...");
        paVerificationService = new services::PaVerificationService(
            paVerificationRepository,
            dataGroupRepository,
            sodParserService,
            certificateValidationService,
            dataGroupParserService,
            dscAutoRegistrationService
        );

        spdlog::info("✅ All services initialized successfully");

    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize services: {}", e.what());
        throw;
    }
}

/**
 * @brief Cleanup all services and repositories
 */
void cleanupServices() {
    spdlog::info("Cleaning up services...");

    // Delete in reverse order of initialization
    delete paVerificationService;
    delete dscAutoRegistrationService;
    delete certificateValidationService;
    delete dataGroupParserService;
    delete sodParserService;
    delete ldapCrlRepository;
    delete ldapCertificateRepository;
    delete dataGroupRepository;
    delete paVerificationRepository;

    paVerificationService = nullptr;
    dscAutoRegistrationService = nullptr;
    certificateValidationService = nullptr;
    dataGroupParserService = nullptr;
    sodParserService = nullptr;
    ldapCrlRepository = nullptr;
    ldapCertificateRepository = nullptr;
    dataGroupRepository = nullptr;
    paVerificationRepository = nullptr;

    // Shutdown connection pool (shared_ptr manages memory automatically)
    if (dbPool) {
        spdlog::debug("Shutting down database connection pool...");
        dbPool->shutdown();
        dbPool.reset();  // Release shared_ptr (optional - will be released at scope exit)
        spdlog::info("✅ Database connection pool shut down");
    }

    spdlog::info("✅ All services cleaned up");
}

// --- API Route Handlers ---

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
            result["version"] = "2.1.1";
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

    // PA verify endpoint - POST /api/pa/verify (Repository Pattern)
    app.registerHandler(
        "/api/pa/verify",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

            spdlog::info("POST /api/pa/verify - Passive Authentication verification (Service Layer)");


            // Log request details for debugging
            auto contentType = req->getHeader("Content-Type");
            auto contentLength = req->getHeader("Content-Length");
            auto bodyLength = req->body().length();
            spdlog::info("Request - Content-Type: {}, Content-Length: {}, Body Length: {}",
                        contentType.empty() ? "(empty)" : contentType,
                        contentLength.empty() ? "(empty)" : contentLength,
                        bodyLength);

            try {
                // Parse request body
                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    spdlog::error("Failed to parse JSON body");
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "Invalid JSON body";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                // Get SOD data (Base64 encoded)
                std::string sodBase64 = (*jsonBody)["sod"].asString();
                if (sodBase64.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "SOD data is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                // Decode SOD
                std::vector<uint8_t> sodBytes = base64Decode(sodBase64);
                if (sodBytes.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "Failed to decode SOD (invalid Base64)";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                // Parse Data Groups (convert to map with string keys)
                std::map<std::string, std::vector<uint8_t>> dataGroups;
                if (jsonBody->isMember("dataGroups")) {
                    if ((*jsonBody)["dataGroups"].isArray()) {
                        // Array format: [{number: "DG1", data: "base64..."}, ...]
                        for (const auto& dg : (*jsonBody)["dataGroups"]) {
                            std::string dgNumStr = dg["number"].asString();
                            std::string dgData = dg["data"].asString();
                            // Extract number from "DG1" -> "1"
                            std::string dgKey = dgNumStr.length() > 2 ? dgNumStr.substr(2) : dgNumStr;
                            dataGroups[dgKey] = base64Decode(dgData);
                        }
                    } else if ((*jsonBody)["dataGroups"].isObject()) {
                        // Object format: {"DG1": "base64...", "DG2": "base64..."} OR {"1": "base64...", "2": "base64..."}
                        for (const auto& key : (*jsonBody)["dataGroups"].getMemberNames()) {
                            std::string dgKey;
                            // Support both "DG1" format and "1" format
                            if (key.length() > 2 && (key.substr(0, 2) == "DG" || key.substr(0, 2) == "dg")) {
                                dgKey = key.substr(2);  // "DG1" -> "1"
                            } else {
                                dgKey = key;  // "1" -> "1"
                            }
                            std::string dgData = (*jsonBody)["dataGroups"][key].asString();
                            dataGroups[dgKey] = base64Decode(dgData);
                        }
                    }
                }

                // Get optional fields
                std::string countryCode = (*jsonBody).get("issuingCountry", "").asString();
                // Normalize alpha-3 country codes (e.g., KOR→KR) for LDAP compatibility
                if (!countryCode.empty()) {
                    std::string normalized = common::normalizeCountryCodeToAlpha2(countryCode);
                    if (normalized != countryCode) {
                        spdlog::info("Country code normalized: {} -> {}", countryCode, normalized);
                    }
                    countryCode = normalized;
                }
                std::string documentNumber = (*jsonBody).get("documentNumber", "").asString();

                // Extract documentNumber from DG1 if not provided
                if (documentNumber.empty() && dataGroups.count("1") > 0) {
                    const auto& dg1Data = dataGroups["1"];
                    // Simple extraction: find MRZ in DG1 and extract document number
                    // This is a simplified version - full parsing is in icao::DgParser
                    size_t pos = 0;
                    while (pos + 3 < dg1Data.size()) {
                        if (dg1Data[pos] == 0x5F && dg1Data[pos + 1] == 0x1F) {
                            // Found MRZ tag 5F1F
                            pos += 2;
                            size_t mrzLen = dg1Data[pos++];
                            if (mrzLen > 127) {
                                size_t numBytes = mrzLen & 0x7F;
                                mrzLen = 0;
                                for (size_t i = 0; i < numBytes && pos < dg1Data.size(); i++) {
                                    mrzLen = (mrzLen << 8) | dg1Data[pos++];
                                }
                            }
                            if (pos + mrzLen <= dg1Data.size() && mrzLen >= 88) {
                                std::string mrzData(dg1Data.begin() + pos, dg1Data.begin() + pos + mrzLen);
                                // TD3 format: document number is at line2[0:9]
                                if (mrzData.length() >= 88) {
                                    std::string docNum = mrzData.substr(44, 9);  // Line 2, position 0-8
                                    // Remove < characters
                                    docNum.erase(std::remove(docNum.begin(), docNum.end(), '<'), docNum.end());
                                    documentNumber = docNum;
                                    spdlog::debug("Extracted document number from DG1: {}", documentNumber);
                                }
                            }
                            break;
                        }
                        pos++;
                    }
                }

                spdlog::info("PA verification request: country={}, documentNumber={}, dataGroups={}",
                            countryCode.empty() ? "(unknown)" : countryCode,
                            documentNumber.empty() ? "(unknown)" : documentNumber,
                            dataGroups.size());

                // Call service layer - this replaces ~400 lines of complex logic
                Json::Value result = paVerificationService->verifyPassiveAuthentication(
                    sodBytes,
                    dataGroups,
                    documentNumber,
                    countryCode
                );

                // Return response
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                if (!result["success"].asBool()) {
                    resp->setStatusCode(drogon::k400BadRequest);
                }
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Error in POST /api/pa/verify: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = "Internal Server Error";
                error["message"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Post}
    );

    // PA history endpoint
    // GET /api/pa/history - PA verification history (Repository Pattern)
    app.registerHandler(
        "/api/pa/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/history");

            try {
                // Parse query parameters
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

                // Calculate limit and offset
                int limit = size;
                int offset = page * size;

                // Call service layer (100% parameterized SQL, secure)
                Json::Value result = paVerificationService->getVerificationHistory(
                    limit,
                    offset,
                    statusFilter,
                    countryFilter
                );

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Error in GET /api/pa/history: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // PA detail by ID
    // GET /api/pa/{id} - Get PA verification by ID (Repository Pattern)
    app.registerHandler(
        "/api/pa/{id}",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& id) {
            spdlog::info("GET /api/pa/{}", id);

            try {
                // Call service layer
                Json::Value result = paVerificationService->getVerificationById(id);

                if (result.isNull() || result.empty()) {
                    // Not found
                    Json::Value notFound;
                    notFound["status"] = "NOT_FOUND";
                    notFound["message"] = "PA verification record not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(notFound);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Error in GET /api/pa/{}: {}", id, e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // PA statistics endpoint
    // GET /api/pa/statistics - PA verification statistics (Repository Pattern)
    app.registerHandler(
        "/api/pa/statistics",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/statistics");

            try {
                // Call service layer
                Json::Value result = paVerificationService->getStatistics();

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Error in GET /api/pa/statistics: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // Parse DG1 (MRZ) endpoint - Java compatible
    app.registerHandler(
        "/api/pa/parse-dg1",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-dg1");

            // Parse request body
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

            // Base64 decode
            std::vector<uint8_t> dg1Bytes = base64Decode(dg1Base64);
            if (dg1Bytes.empty()) {
                Json::Value error;
                error["error"] = "Invalid Base64 encoding";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Use icao::DgParser to parse DG1
            Json::Value result = dataGroupParserService->parseDg1(dg1Bytes);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Parse MRZ text endpoint - Java compatible
    app.registerHandler(
        "/api/pa/parse-mrz-text",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-mrz-text");

            // Parse request body
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

            // Use icao::DgParser to parse MRZ text
            Json::Value result = dataGroupParserService->parseMrzText(mrzText);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Parse DG2 (Face Image) endpoint
    // NOTE: Current service implementation provides basic format detection only
    // Full ISO 19794-5 FAC container support and image extraction can be added to service layer later
    app.registerHandler(
        "/api/pa/parse-dg2",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-dg2");

            // Parse request body
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

            // Base64 decode
            std::vector<uint8_t> dg2Bytes = base64Decode(dg2Base64);
            if (dg2Bytes.empty()) {
                Json::Value error;
                error["error"] = "Invalid Base64 encoding";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Use icao::DgParser to parse DG2
            Json::Value result = dataGroupParserService->parseDg2(dg2Bytes);

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

            // Parse request body
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

            // Base64 decode
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

            // Use icao::SodParser to parse SOD
            Json::Value result = sodParserService->parseSodForApi(sodBytes);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Data groups endpoint with full DG1/DG2 parsing
    app.registerHandler(
        "/api/pa/{id}/datagroups",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& id) {
            spdlog::info("GET /api/pa/{}/datagroups", id);

            try {
                // Use DataGroupRepository to fetch data groups (returns JSON array)
                Json::Value dataGroups = dataGroupRepository->findByVerificationId(id);

                Json::Value result;
                result["verificationId"] = id;
                result["hasDg1"] = false;
                result["hasDg2"] = false;

                spdlog::debug("Found {} data groups for verification {}", dataGroups.size(), id);

                // Process each data group
                for (const auto& dg : dataGroups) {
                    int dgNumber = dg["dgNumber"].asInt();

                    // Convert hex string back to binary for parsing
                    std::string dgBinaryHex = dg["dgBinary"].asString();
                    std::vector<uint8_t> dgBytes;

                    // Remove \x prefix if present
                    size_t startPos = 0;
                    if (dgBinaryHex.length() >= 2 && dgBinaryHex[0] == '\\' && dgBinaryHex[1] == 'x') {
                        startPos = 2;
                    }

                    // Convert hex string to bytes
                    for (size_t i = startPos; i < dgBinaryHex.length(); i += 2) {
                        if (i + 1 < dgBinaryHex.length()) {
                            std::string byteStr = dgBinaryHex.substr(i, 2);
                            uint8_t byte = static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16));
                            dgBytes.push_back(byte);
                        }
                    }

                    if (dgNumber == 1) {
                        result["hasDg1"] = true;
                        spdlog::debug("Parsing DG1 ({} bytes)", dgBytes.size());

                        // Use icao::DgParser to parse DG1
                        Json::Value dg1Result = dataGroupParserService->parseDg1(dgBytes);
                        if (dg1Result["success"].asBool()) {
                            result["dg1"] = dg1Result;
                            spdlog::debug("DG1 parsed successfully");
                        } else {
                            spdlog::warn("Failed to parse DG1: {}", dg1Result["error"].asString());
                        }
                    } else if (dgNumber == 2) {
                        result["hasDg2"] = true;
                        spdlog::debug("Parsing DG2 ({} bytes)", dgBytes.size());

                        // Use icao::DgParser to parse DG2
                        Json::Value dg2Result = dataGroupParserService->parseDg2(dgBytes);
                        if (dg2Result["success"].asBool()) {
                            result["dg2"] = dg2Result;
                            spdlog::debug("DG2 parsed successfully");
                        } else {
                            spdlog::warn("Failed to parse DG2: {}", dg2Result["error"].asString());
                        }
                    }
                }

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);
            } catch (const std::exception& e) {
                spdlog::error("Error in /api/pa/{}/datagroups: {}", id, e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
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
            result["version"] = "2.1.1";
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
        [](const drogon::HttpRequestPtr& /* req */,
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
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newRedirectionResponse("/swagger-ui/index.html");
            callback(resp);
        },
        {drogon::Get}
    );

    spdlog::info("PA Service API routes registered");
}

#pragma GCC diagnostic pop

} // anonymous namespace

// --- Main Entry Point ---

int main(int /* argc */, char* /* argv */[]) {
    printBanner();
    initializeLogging();

    appConfig = AppConfig::fromEnvironment();

    // Validate required credentials
    try {
        appConfig.validateRequiredCredentials();
    } catch (const std::exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    }

    spdlog::info("Starting PA Service v2.1.1 Sprint3-CSCA-LC-Support...");
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

        // Global exception handler
        app.setExceptionHandler([](const std::exception& e,
                                   const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::error("Unhandled exception in {}: {}", req->getPath(), e.what());
            Json::Value error;
            error["success"] = false;
            error["error"] = "Internal Server Error";
            error["message"] = e.what();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
        });

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

        // Initialize services with dependency injection
        initializeServices();

        registerRoutes();

        spdlog::info("Server starting on http://0.0.0.0:{}", appConfig.serverPort);
        spdlog::info("Press Ctrl+C to stop the server");

        app.run();

        // Cleanup services on shutdown
        cleanupServices();

    } catch (const std::exception& e) {
        spdlog::error("Application error: {}", e.what());
        cleanupServices();  // Cleanup on error
        return 1;
    }

    spdlog::info("Server stopped");
    return 0;
}

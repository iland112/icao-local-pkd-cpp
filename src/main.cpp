/**
 * @file main.cpp
 * @brief ICAO Local PKD Application Entry Point
 *
 * C++ REST API based ICAO Local PKD Management and
 * Passive Authentication (PA) Verification System.
 *
 * @author SmartCore Inc.
 * @date 2025-12-29
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

// PostgreSQL header for direct connection test
#include <libpq-fe.h>

// OpenLDAP header
#include <ldap.h>

// OpenSSL for hash computation and certificate parsing
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
#include <iomanip>
#include <sstream>
#include <random>
#include <map>
#include <set>
#include <optional>
#include <regex>

namespace {

/**
 * @brief Application configuration
 */
struct AppConfig {
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "localpkd";
    std::string dbUser = "localpkd";
    std::string dbPassword = "localpkd123";

    // LDAP Read: HAProxy for load balancing across MMR nodes
    std::string ldapHost = "haproxy";
    int ldapPort = 389;
    // LDAP Write: Direct connection to primary master (openldap1) for write operations
    std::string ldapWriteHost = "openldap1";
    int ldapWritePort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword = "admin";
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    int serverPort = 8081;
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
        if (auto val = std::getenv("LDAP_WRITE_HOST")) config.ldapWriteHost = val;
        if (auto val = std::getenv("LDAP_WRITE_PORT")) config.ldapWritePort = std::stoi(val);
        if (auto val = std::getenv("LDAP_BIND_DN")) config.ldapBindDn = val;
        if (auto val = std::getenv("LDAP_BIND_PASSWORD")) config.ldapBindPassword = val;
        if (auto val = std::getenv("LDAP_BASE_DN")) config.ldapBaseDn = val;

        if (auto val = std::getenv("SERVER_PORT")) config.serverPort = std::stoi(val);
        if (auto val = std::getenv("THREAD_NUM")) config.threadNum = std::stoi(val);

        return config;
    }
};

// Global configuration
AppConfig appConfig;

/**
 * @brief Initialize logging system
 */
void initializeLogging() {
    try {
        // Console sink (colored output)
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

        // File sink (rotating)
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/icao-local-pkd.log", 1024 * 1024 * 10, 5);
        file_sink->set_level(spdlog::level::info);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

        // Multi-sink logger
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::debug);

        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::warn);

        spdlog::info("Logging system initialized");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

/**
 * @brief Print application banner
 */
void printBanner() {
    std::cout << R"(
  _____ _____          ____    _                    _   ____  _  ______
 |_   _/ ____|   /\   / __ \  | |                  | | |  _ \| |/ /  _ \
   | || |       /  \ | |  | | | |     ___   ___ __ | | | |_) | ' /| | | |
   | || |      / /\ \| |  | | | |    / _ \ / __/ _` | | |  _ <|  < | | | |
  _| || |____ / ____ \ |__| | | |___| (_) | (_| (_| | | | |_) | . \| |_| |
 |_____\_____/_/    \_\____/  |______\___/ \___\__,_|_| |____/|_|\_\____/

)" << std::endl;
    std::cout << "  ICAO Local PKD Management & Passive Authentication System" << std::endl;
    std::cout << "  Version: 1.0.0" << std::endl;
    std::cout << "  (C) 2025 SmartCore Inc." << std::endl;
    std::cout << std::endl;
}

/**
 * @brief Check database connectivity using libpq directly
 */
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
        // Execute a simple query to verify
        PGresult* res = PQexec(conn, "SELECT version()");
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            result["status"] = "UP";
            result["responseTimeMs"] = static_cast<int>(duration.count());
            result["type"] = "PostgreSQL";
            result["version"] = PQgetvalue(res, 0, 0);
        } else {
            result["status"] = "DOWN";
            result["error"] = PQerrorMessage(conn);
        }
        PQclear(res);
    } else {
        result["status"] = "DOWN";
        result["error"] = PQerrorMessage(conn);
    }

    PQfinish(conn);
    return result;
}

/**
 * @brief Generate UUID v4
 */
std::string generateUuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);

    // Set version (4) and variant (RFC 4122)
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

/**
 * @brief Compute SHA256 hash of content
 */
std::string computeFileHash(const std::vector<uint8_t>& content) {
    unsigned char hash[32];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, content.data(), content.size());
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

/**
 * @brief Escape string for SQL
 */
std::string escapeSqlString(PGconn* conn, const std::string& str) {
    char* escaped = PQescapeLiteral(conn, str.c_str(), str.length());
    if (!escaped) return "''";
    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

/**
 * @brief Save upload record to database
 */
std::string saveUploadRecord(PGconn* conn, const std::string& fileName,
                              int64_t fileSize, const std::string& format,
                              const std::string& fileHash) {
    std::string uploadId = generateUuid();

    std::string query = "INSERT INTO uploaded_file (id, file_name, original_file_name, file_hash, "
                       "file_size, file_format, status, upload_timestamp) VALUES ("
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, fileName) + ", "
                       + escapeSqlString(conn, fileName) + ", "
                       "'" + fileHash + "', "
                       + std::to_string(fileSize) + ", "
                       "'" + format + "', "
                       "'PROCESSING', "
                       "NOW())";

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(conn);
        PQclear(res);
        throw std::runtime_error("Failed to save upload record: " + error);
    }
    PQclear(res);

    return uploadId;
}

/**
 * @brief Update upload status in database
 */
void updateUploadStatus(PGconn* conn, const std::string& uploadId,
                        const std::string& status, int certCount, int crlCount,
                        const std::string& errorMessage) {
    std::string query = "UPDATE uploaded_file SET status = '" + status + "', "
                       "csca_count = " + std::to_string(certCount) + ", "
                       "crl_count = " + std::to_string(crlCount) + ", "
                       "error_message = " + escapeSqlString(conn, errorMessage) + ", "
                       "completed_timestamp = NOW() "
                       "WHERE id = '" + uploadId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Count LDIF entries in content
 */
int countLdifEntries(const std::string& content) {
    int count = 0;
    std::istringstream stream(content);
    std::string line;
    bool inEntry = false;

    while (std::getline(stream, line)) {
        // Remove trailing CR if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            if (inEntry) {
                count++;
                inEntry = false;
            }
        } else if (line.substr(0, 3) == "dn:") {
            inEntry = true;
        }
    }

    // Count last entry if exists
    if (inEntry) count++;

    return count;
}

// ============================================================================
// Certificate/CRL Parsing and DB Storage Functions
// ============================================================================

/**
 * @brief LDIF Entry structure
 */
struct LdifEntry {
    std::string dn;
    std::map<std::string, std::vector<std::string>> attributes;

    bool hasAttribute(const std::string& name) const {
        return attributes.find(name) != attributes.end();
    }

    std::string getFirstAttribute(const std::string& name) const {
        auto it = attributes.find(name);
        if (it != attributes.end() && !it->second.empty()) {
            return it->second[0];
        }
        return "";
    }
};

/**
 * @brief Base64 decode
 */
std::vector<uint8_t> base64Decode(const std::string& encoded) {
    static const std::string base64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<uint8_t> result;
    std::vector<int> decodingTable(256, -1);
    for (size_t i = 0; i < base64Chars.size(); i++) {
        decodingTable[static_cast<unsigned char>(base64Chars[i])] = static_cast<int>(i);
    }

    int val = 0;
    int valb = -8;
    for (unsigned char c : encoded) {
        if (decodingTable[c] == -1) continue;
        val = (val << 6) + decodingTable[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

/**
 * @brief Convert X509_NAME to string
 */
std::string x509NameToString(X509_NAME* name) {
    if (!name) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

/**
 * @brief Convert ASN1_INTEGER to hex string
 */
std::string asn1IntegerToHex(const ASN1_INTEGER* asn1Int) {
    if (!asn1Int) return "";
    BIGNUM* bn = ASN1_INTEGER_to_BN(asn1Int, nullptr);
    if (!bn) return "";
    char* hex = BN_bn2hex(bn);
    std::string result(hex);
    OPENSSL_free(hex);
    BN_free(bn);
    return result;
}

/**
 * @brief Convert ASN1_TIME to ISO8601 string
 */
std::string asn1TimeToIso8601(const ASN1_TIME* asn1Time) {
    if (!asn1Time) return "";

    struct tm tm = {};
    const char* str = reinterpret_cast<const char*>(asn1Time->data);
    size_t len = asn1Time->length;

    if (asn1Time->type == V_ASN1_UTCTIME && len >= 12) {
        int year = (str[0] - '0') * 10 + (str[1] - '0');
        tm.tm_year = (year >= 50 ? 1900 : 2000) + year - 1900;
        tm.tm_mon = (str[2] - '0') * 10 + (str[3] - '0') - 1;
        tm.tm_mday = (str[4] - '0') * 10 + (str[5] - '0');
        tm.tm_hour = (str[6] - '0') * 10 + (str[7] - '0');
        tm.tm_min = (str[8] - '0') * 10 + (str[9] - '0');
        tm.tm_sec = (str[10] - '0') * 10 + (str[11] - '0');
    } else if (asn1Time->type == V_ASN1_GENERALIZEDTIME && len >= 14) {
        tm.tm_year = (str[0] - '0') * 1000 + (str[1] - '0') * 100 +
                     (str[2] - '0') * 10 + (str[3] - '0') - 1900;
        tm.tm_mon = (str[4] - '0') * 10 + (str[5] - '0') - 1;
        tm.tm_mday = (str[6] - '0') * 10 + (str[7] - '0');
        tm.tm_hour = (str[8] - '0') * 10 + (str[9] - '0');
        tm.tm_min = (str[10] - '0') * 10 + (str[11] - '0');
        tm.tm_sec = (str[12] - '0') * 10 + (str[13] - '0');
    }

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf) + "+00";
}

/**
 * @brief Extract country code from DN
 */
std::string extractCountryCode(const std::string& dn) {
    static const std::regex countryRegex(R"((?:^|,\s*)C=([A-Z]{2,3})(?:,|$))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(dn, match, countryRegex)) {
        std::string code = match[1].str();
        for (char& c : code) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return code;
    }
    return "XX";  // Default unknown country
}

/**
 * @brief Parse LDIF content into entries
 */
std::vector<LdifEntry> parseLdifContent(const std::string& content) {
    std::vector<LdifEntry> entries;
    LdifEntry currentEntry;
    std::string currentAttrName;
    std::string currentAttrValue;
    bool inContinuation = false;

    std::istringstream stream(content);
    std::string line;

    auto finalizeAttribute = [&]() {
        if (!currentAttrName.empty()) {
            currentEntry.attributes[currentAttrName].push_back(currentAttrValue);
            currentAttrName.clear();
            currentAttrValue.clear();
        }
    };

    auto finalizeEntry = [&]() {
        finalizeAttribute();
        if (!currentEntry.dn.empty()) {
            entries.push_back(std::move(currentEntry));
            currentEntry = LdifEntry();
        }
    };

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            finalizeEntry();
            inContinuation = false;
            continue;
        }

        if (line[0] == '#') continue;

        if (line[0] == ' ') {
            if (inContinuation) {
                currentAttrValue += line.substr(1);
            }
            continue;
        }

        finalizeAttribute();
        inContinuation = false;

        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        currentAttrName = line.substr(0, colonPos);

        if (colonPos + 1 < line.size() && line[colonPos + 1] == ':') {
            // Base64 encoded value (double colon ::)
            // Only add ;binary suffix if not already present
            if (currentAttrName.find(";binary") == std::string::npos) {
                currentAttrName += ";binary";
            }
            size_t valueStart = colonPos + 2;
            while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
            currentAttrValue = line.substr(valueStart);
        } else {
            size_t valueStart = colonPos + 1;
            while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
            currentAttrValue = line.substr(valueStart);
        }

        if (currentAttrName == "dn") {
            currentEntry.dn = currentAttrValue;
            currentAttrName.clear();
            currentAttrValue.clear();
        } else {
            inContinuation = true;
        }
    }

    finalizeEntry();
    return entries;
}

/**
 * @brief Escape binary data for PostgreSQL BYTEA
 */
std::string escapeBytea(PGconn* conn, const std::vector<uint8_t>& data) {
    size_t toLen = 0;
    unsigned char* escaped = PQescapeByteaConn(conn, data.data(), data.size(), &toLen);
    if (!escaped) return "";
    std::string result(reinterpret_cast<char*>(escaped), toLen - 1);
    PQfreemem(escaped);
    return result;
}

// =============================================================================
// LDAP Storage Functions
// =============================================================================

/**
 * @brief Get LDAP connection for write operations (direct to primary master)
 * In MMR setup, writes go to openldap1 directly to avoid replication conflicts
 */
LDAP* getLdapWriteConnection() {
    LDAP* ld = nullptr;
    std::string uri = "ldap://" + appConfig.ldapWriteHost + ":" + std::to_string(appConfig.ldapWritePort);

    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    struct berval cred;
    cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
    cred.bv_len = appConfig.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    return ld;
}

/**
 * @brief Get LDAP connection for read operations (via HAProxy load balancer)
 */
LDAP* getLdapReadConnection() {
    LDAP* ld = nullptr;
    std::string uri = "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort);

    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    struct berval cred;
    cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
    cred.bv_len = appConfig.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    return ld;
}

/**
 * @brief Build LDAP DN for certificate
 * @param certType CSCA, DSC, or DSC_NC
 * @param countryCode ISO country code
 * @param fingerprint Certificate fingerprint (used as CN)
 */
std::string buildCertificateDn(const std::string& certType, const std::string& countryCode,
                                const std::string& fingerprint) {
    // ICAO PKD DIT structure:
    // dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
    //   └── dc=download
    //       └── dc=data (for CSCA, DSC)
    //           └── c={COUNTRY}
    //               ├── o=csca
    //               └── o=dsc
    //       └── dc=nc-data (for DSC_NC)
    //           └── c={COUNTRY}
    //               └── o=dsc

    std::string ou;
    std::string dataContainer;

    if (certType == "CSCA") {
        ou = "csca";
        dataContainer = "dc=data";
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = "dc=data";
    } else if (certType == "DSC_NC") {
        ou = "dsc";
        dataContainer = "dc=nc-data";
    } else {
        ou = "dsc";
        dataContainer = "dc=data";
    }

    return "cn=" + fingerprint.substr(0, 32) + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + ",dc=download," + appConfig.ldapBaseDn;
}

/**
 * @brief Build LDAP DN for CRL
 */
std::string buildCrlDn(const std::string& countryCode, const std::string& fingerprint) {
    return "cn=" + fingerprint.substr(0, 32) + ",o=crl,c=" + countryCode +
           ",dc=data,dc=download," + appConfig.ldapBaseDn;
}

/**
 * @brief Ensure country organizational unit exists in LDAP
 */
bool ensureCountryOuExists(LDAP* ld, const std::string& countryCode, bool isNcData = false) {
    std::string dataContainer = isNcData ? "dc=nc-data" : "dc=data";
    std::string countryDn = "c=" + countryCode + "," + dataContainer + ",dc=download," + appConfig.ldapBaseDn;

    // Check if country entry exists
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(ld, countryDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);

    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_SUCCESS) {
        return true;  // Country already exists
    }

    if (rc != LDAP_NO_SUCH_OBJECT) {
        spdlog::warn("LDAP search for country {} failed: {}", countryCode, ldap_err2string(rc));
        return false;
    }

    // Create country entry
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {const_cast<char*>("country"), const_cast<char*>("top"), nullptr};
    modObjectClass.mod_values = ocVals;

    LDAPMod modC;
    modC.mod_op = LDAP_MOD_ADD;
    modC.mod_type = const_cast<char*>("c");
    char* cVal[] = {const_cast<char*>(countryCode.c_str()), nullptr};
    modC.mod_values = cVal;

    LDAPMod* mods[] = {&modObjectClass, &modC, nullptr};

    rc = ldap_add_ext_s(ld, countryDn.c_str(), mods, nullptr, nullptr);
    if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
        spdlog::warn("Failed to create country entry {}: {}", countryDn, ldap_err2string(rc));
        return false;
    }

    // Create organizational units under country (csca, dsc, crl)
    std::vector<std::string> ous = isNcData ? std::vector<std::string>{"dsc"}
                                            : std::vector<std::string>{"csca", "dsc", "crl"};

    for (const auto& ouName : ous) {
        std::string ouDn = "o=" + ouName + "," + countryDn;

        LDAPMod ouObjClass;
        ouObjClass.mod_op = LDAP_MOD_ADD;
        ouObjClass.mod_type = const_cast<char*>("objectClass");
        char* ouOcVals[] = {const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        ouObjClass.mod_values = ouOcVals;

        LDAPMod ouO;
        ouO.mod_op = LDAP_MOD_ADD;
        ouO.mod_type = const_cast<char*>("o");
        char* ouVal[] = {const_cast<char*>(ouName.c_str()), nullptr};
        ouO.mod_values = ouVal;

        LDAPMod* ouMods[] = {&ouObjClass, &ouO, nullptr};

        rc = ldap_add_ext_s(ld, ouDn.c_str(), ouMods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::debug("OU creation result for {}: {}", ouDn, ldap_err2string(rc));
        }
    }

    return true;
}

/**
 * @brief Save certificate to LDAP
 * @return LDAP DN or empty string on failure
 */
std::string saveCertificateToLdap(LDAP* ld, const std::string& certType,
                                   const std::string& countryCode,
                                   const std::string& subjectDn, const std::string& issuerDn,
                                   const std::string& serialNumber, const std::string& fingerprint,
                                   const std::vector<uint8_t>& certBinary) {
    bool isNcData = (certType == "DSC_NC");

    // Ensure country structure exists
    if (!ensureCountryOuExists(ld, countryCode, isNcData)) {
        spdlog::warn("Failed to ensure country OU exists for {}", countryCode);
        // Continue anyway - the OU might exist even if we couldn't create it
    }

    std::string dn = buildCertificateDn(certType, countryCode, fingerprint);

    // Build LDAP entry attributes
    // objectClass hierarchy: inetOrgPerson (structural) + pkdDownload (auxiliary, ICAO PKD custom schema)
    // inetOrgPerson <- organizationalPerson <- person <- top
    // Required attributes: cn (from person), sn (from person)
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("person"),
        const_cast<char*>("organizationalPerson"),
        const_cast<char*>("inetOrgPerson"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    // cn (subject DN - required by person)
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    char* cnVals[] = {const_cast<char*>(subjectDn.c_str()), nullptr};
    modCn.mod_values = cnVals;

    // sn (serial number - required by person)
    LDAPMod modSn;
    modSn.mod_op = LDAP_MOD_ADD;
    modSn.mod_type = const_cast<char*>("sn");
    char* snVals[] = {const_cast<char*>(serialNumber.c_str()), nullptr};
    modSn.mod_values = snVals;

    // userCertificate;binary (inetOrgPerson attribute for certificates)
    LDAPMod modCert;
    modCert.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCert.mod_type = const_cast<char*>("userCertificate;binary");
    berval certBv;
    certBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(certBinary.data()));
    certBv.bv_len = certBinary.size();
    berval* certBvVals[] = {&certBv, nullptr};
    modCert.mod_bvalues = certBvVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modSn, &modCert, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // Try to update the certificate
        LDAPMod modCertReplace;
        modCertReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modCertReplace.mod_type = const_cast<char*>("userCertificate;binary");
        modCertReplace.mod_bvalues = certBvVals;

        LDAPMod* replaceMods[] = {&modCertReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save certificate to LDAP {}: {}", dn, ldap_err2string(rc));
        return "";
    }

    spdlog::debug("Saved certificate to LDAP: {}", dn);
    return dn;
}

/**
 * @brief Save CRL to LDAP
 * @return LDAP DN or empty string on failure
 */
std::string saveCrlToLdap(LDAP* ld, const std::string& countryCode,
                           const std::string& issuerDn, const std::string& fingerprint,
                           const std::vector<uint8_t>& crlBinary) {
    // Ensure country structure exists
    if (!ensureCountryOuExists(ld, countryCode, false)) {
        spdlog::warn("Failed to ensure country OU exists for CRL {}", countryCode);
    }

    std::string dn = buildCrlDn(countryCode, fingerprint);

    // Build LDAP entry attributes
    // objectClass: cRLDistributionPoint (structural) + pkdDownload (auxiliary)
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("cRLDistributionPoint"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    // cn
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    std::string cnValue = fingerprint.substr(0, 32);
    char* cnVals[] = {const_cast<char*>(cnValue.c_str()), nullptr};
    modCn.mod_values = cnVals;

    // certificateRevocationList;binary
    LDAPMod modCrl;
    modCrl.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCrl.mod_type = const_cast<char*>("certificateRevocationList;binary");
    berval crlBv;
    crlBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(crlBinary.data()));
    crlBv.bv_len = crlBinary.size();
    berval* crlBvVals[] = {&crlBv, nullptr};
    modCrl.mod_bvalues = crlBvVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modCrl, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // Update existing CRL
        LDAPMod modCrlReplace;
        modCrlReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modCrlReplace.mod_type = const_cast<char*>("certificateRevocationList;binary");
        modCrlReplace.mod_bvalues = crlBvVals;

        LDAPMod* replaceMods[] = {&modCrlReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save CRL to LDAP {}: {}", dn, ldap_err2string(rc));
        return "";
    }

    spdlog::debug("Saved CRL to LDAP: {}", dn);
    return dn;
}

/**
 * @brief Update DB record with LDAP DN after successful LDAP storage
 */
void updateCertificateLdapStatus(PGconn* conn, const std::string& certId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    std::string query = "UPDATE certificate SET "
                       "ldap_dn = " + escapeSqlString(conn, ldapDn) + ", "
                       "stored_in_ldap = TRUE, "
                       "stored_at = NOW() "
                       "WHERE id = '" + certId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Update CRL DB record with LDAP DN after successful LDAP storage
 */
void updateCrlLdapStatus(PGconn* conn, const std::string& crlId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    std::string query = "UPDATE crl SET "
                       "ldap_dn = " + escapeSqlString(conn, ldapDn) + ", "
                       "stored_in_ldap = TRUE, "
                       "stored_at = NOW() "
                       "WHERE id = '" + crlId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

// =============================================================================
// Database Storage Functions
// =============================================================================

/**
 * @brief Save certificate to database
 * @return certificate ID or empty string on failure
 */
std::string saveCertificate(PGconn* conn, const std::string& uploadId,
                            const std::string& certType, const std::string& countryCode,
                            const std::string& subjectDn, const std::string& issuerDn,
                            const std::string& serialNumber, const std::string& fingerprint,
                            const std::string& notBefore, const std::string& notAfter,
                            const std::vector<uint8_t>& certBinary) {
    std::string certId = generateUuid();
    std::string byteaEscaped = escapeBytea(conn, certBinary);

    std::string query = "INSERT INTO certificate (id, upload_id, certificate_type, country_code, "
                       "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                       "not_before, not_after, certificate_binary, validation_status, created_at) VALUES ("
                       "'" + certId + "', "
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, certType) + ", "
                       + escapeSqlString(conn, countryCode) + ", "
                       + escapeSqlString(conn, subjectDn) + ", "
                       + escapeSqlString(conn, issuerDn) + ", "
                       + escapeSqlString(conn, serialNumber) + ", "
                       "'" + fingerprint + "', "
                       "'" + notBefore + "', "
                       "'" + notAfter + "', "
                       "E'" + byteaEscaped + "', "
                       "'PENDING', NOW()) "
                       "ON CONFLICT (certificate_type, fingerprint_sha256) DO NOTHING";

    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);

    return success ? certId : "";
}

/**
 * @brief Save CRL to database
 * @return CRL ID or empty string on failure
 */
std::string saveCrl(PGconn* conn, const std::string& uploadId,
                    const std::string& countryCode, const std::string& issuerDn,
                    const std::string& thisUpdate, const std::string& nextUpdate,
                    const std::string& crlNumber, const std::string& fingerprint,
                    const std::vector<uint8_t>& crlBinary) {
    std::string crlId = generateUuid();
    std::string byteaEscaped = escapeBytea(conn, crlBinary);

    std::string query = "INSERT INTO crl (id, upload_id, country_code, issuer_dn, "
                       "this_update, next_update, crl_number, fingerprint_sha256, "
                       "crl_binary, validation_status, created_at) VALUES ("
                       "'" + crlId + "', "
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, countryCode) + ", "
                       + escapeSqlString(conn, issuerDn) + ", "
                       "'" + thisUpdate + "', "
                       + (nextUpdate.empty() ? "NULL" : "'" + nextUpdate + "'") + ", "
                       + escapeSqlString(conn, crlNumber) + ", "
                       "'" + fingerprint + "', "
                       "E'" + byteaEscaped + "', "
                       "'PENDING', NOW()) "
                       "ON CONFLICT DO NOTHING";

    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);

    return success ? crlId : "";
}

/**
 * @brief Save revoked certificate to database
 */
void saveRevokedCertificate(PGconn* conn, const std::string& crlId,
                            const std::string& serialNumber, const std::string& revocationDate,
                            const std::string& reason) {
    std::string query = "INSERT INTO revoked_certificate (id, crl_id, serial_number, "
                       "revocation_date, revocation_reason, created_at) VALUES ("
                       "'" + generateUuid() + "', "
                       "'" + crlId + "', "
                       + escapeSqlString(conn, serialNumber) + ", "
                       "'" + revocationDate + "', "
                       + escapeSqlString(conn, reason) + ", "
                       "NOW())";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Parse and save certificate from LDIF entry (DB + LDAP)
 */
bool parseCertificateEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                           const LdifEntry& entry, const std::string& attrName,
                           int& cscaCount, int& dscCount, int& ldapStoredCount) {
    std::string base64Value = entry.getFirstAttribute(attrName);
    if (base64Value.empty()) return false;

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) return false;

    const uint8_t* data = derBytes.data();
    X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!cert) {
        spdlog::warn("Failed to parse certificate from entry: {}", entry.dn);
        return false;
    }

    std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
    std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
    std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
    std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
    std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));
    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(subjectDn);
    if (countryCode == "XX") {
        countryCode = extractCountryCode(issuerDn);
    }

    // Determine certificate type
    std::string certType;
    if (subjectDn == issuerDn) {
        certType = "CSCA";
        cscaCount++;
    } else if (entry.dn.find("nc-data") != std::string::npos) {
        certType = "DSC_NC";
        dscCount++;
    } else {
        certType = "DSC";
        dscCount++;
    }

    X509_free(cert);

    // 1. Save to DB
    std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                         subjectDn, issuerDn, serialNumber, fingerprint,
                                         notBefore, notAfter, derBytes);

    if (!certId.empty()) {
        spdlog::debug("Saved certificate to DB: type={}, country={}, fingerprint={}",
                     certType, countryCode, fingerprint.substr(0, 16));

        // 2. Save to LDAP
        if (ld) {
            std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                        subjectDn, issuerDn, serialNumber,
                                                        fingerprint, derBytes);
            if (!ldapDn.empty()) {
                updateCertificateLdapStatus(conn, certId, ldapDn);
                ldapStoredCount++;
                spdlog::debug("Saved certificate to LDAP: {}", ldapDn);
            }
        }
    }

    return !certId.empty();
}

/**
 * @brief Parse and save CRL from LDIF entry (DB + LDAP)
 */
bool parseCrlEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                   const LdifEntry& entry, int& crlCount, int& ldapCrlStoredCount) {
    std::string base64Value = entry.getFirstAttribute("certificateRevocationList;binary");
    if (base64Value.empty()) return false;

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) return false;

    const uint8_t* data = derBytes.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!crl) {
        spdlog::warn("Failed to parse CRL from entry: {}", entry.dn);
        return false;
    }

    std::string issuerDn = x509NameToString(X509_CRL_get_issuer(crl));
    std::string thisUpdate = asn1TimeToIso8601(X509_CRL_get0_lastUpdate(crl));
    std::string nextUpdate;
    if (X509_CRL_get0_nextUpdate(crl)) {
        nextUpdate = asn1TimeToIso8601(X509_CRL_get0_nextUpdate(crl));
    }

    std::string crlNumber;
    ASN1_INTEGER* crlNumAsn1 = static_cast<ASN1_INTEGER*>(
        X509_CRL_get_ext_d2i(crl, NID_crl_number, nullptr, nullptr));
    if (crlNumAsn1) {
        crlNumber = asn1IntegerToHex(crlNumAsn1);
        ASN1_INTEGER_free(crlNumAsn1);
    }

    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(issuerDn);

    // 1. Save to DB
    std::string crlId = saveCrl(conn, uploadId, countryCode, issuerDn,
                                thisUpdate, nextUpdate, crlNumber, fingerprint, derBytes);

    if (!crlId.empty()) {
        crlCount++;

        // Save revoked certificates to DB
        STACK_OF(X509_REVOKED)* revokedStack = X509_CRL_get_REVOKED(crl);
        if (revokedStack) {
            int revokedCount = sk_X509_REVOKED_num(revokedStack);
            for (int i = 0; i < revokedCount; i++) {
                X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedStack, i);
                if (revoked) {
                    std::string serialNum = asn1IntegerToHex(X509_REVOKED_get0_serialNumber(revoked));
                    std::string revDate = asn1TimeToIso8601(X509_REVOKED_get0_revocationDate(revoked));
                    std::string reason = "unspecified";

                    ASN1_ENUMERATED* reasonEnum = static_cast<ASN1_ENUMERATED*>(
                        X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, nullptr, nullptr));
                    if (reasonEnum) {
                        long reasonCode = ASN1_ENUMERATED_get(reasonEnum);
                        switch (reasonCode) {
                            case 1: reason = "keyCompromise"; break;
                            case 2: reason = "cACompromise"; break;
                            case 3: reason = "affiliationChanged"; break;
                            case 4: reason = "superseded"; break;
                            case 5: reason = "cessationOfOperation"; break;
                            case 6: reason = "certificateHold"; break;
                        }
                        ASN1_ENUMERATED_free(reasonEnum);
                    }

                    saveRevokedCertificate(conn, crlId, serialNum, revDate, reason);
                }
            }
            spdlog::debug("Saved CRL to DB with {} revoked certificates, issuer={}",
                         revokedCount, issuerDn.substr(0, 50));
        }

        // 2. Save to LDAP
        if (ld) {
            std::string ldapDn = saveCrlToLdap(ld, countryCode, issuerDn, fingerprint, derBytes);
            if (!ldapDn.empty()) {
                updateCrlLdapStatus(conn, crlId, ldapDn);
                ldapCrlStoredCount++;
                spdlog::debug("Saved CRL to LDAP: {}", ldapDn);
            }
        }
    }

    X509_CRL_free(crl);
    return !crlId.empty();
}

/**
 * @brief Update uploaded_file with parsing statistics
 */
void updateUploadStatistics(PGconn* conn, const std::string& uploadId,
                           const std::string& status, int cscaCount, int dscCount,
                           int crlCount, int totalEntries, int processedEntries,
                           const std::string& errorMessage) {
    std::string query = "UPDATE uploaded_file SET "
                       "status = '" + status + "', "
                       "csca_count = " + std::to_string(cscaCount) + ", "
                       "dsc_count = " + std::to_string(dscCount) + ", "
                       "crl_count = " + std::to_string(crlCount) + ", "
                       "total_entries = " + std::to_string(totalEntries) + ", "
                       "processed_entries = " + std::to_string(processedEntries) + ", "
                       "error_message = " + escapeSqlString(conn, errorMessage) + ", "
                       "completed_timestamp = NOW() "
                       "WHERE id = '" + uploadId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Process LDIF file asynchronously with full parsing (DB + LDAP)
 */
void processLdifFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
    std::thread([uploadId, content]() {
        spdlog::info("Starting async LDIF processing for upload: {}", uploadId);

        // Connect to PostgreSQL
        std::string conninfo = "host=" + appConfig.dbHost +
                              " port=" + std::to_string(appConfig.dbPort) +
                              " dbname=" + appConfig.dbName +
                              " user=" + appConfig.dbUser +
                              " password=" + appConfig.dbPassword;

        PGconn* conn = PQconnectdb(conninfo.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            spdlog::error("Database connection failed for async processing: {}", PQerrorMessage(conn));
            PQfinish(conn);
            return;
        }

        // Connect to LDAP (write connection - direct to primary master)
        LDAP* ld = getLdapWriteConnection();
        if (!ld) {
            spdlog::warn("LDAP write connection failed - will only save to DB");
        } else {
            spdlog::info("LDAP write connection established for upload {}", uploadId);
        }

        try {
            std::string contentStr(content.begin(), content.end());

            // Parse LDIF content
            std::vector<LdifEntry> entries = parseLdifContent(contentStr);
            int totalEntries = static_cast<int>(entries.size());

            spdlog::info("Parsed {} LDIF entries for upload {}", totalEntries, uploadId);

            int cscaCount = 0;
            int dscCount = 0;
            int crlCount = 0;
            int processedEntries = 0;
            int ldapCertStoredCount = 0;
            int ldapCrlStoredCount = 0;

            // Process each entry
            for (const auto& entry : entries) {
                try {
                    // Check for userCertificate;binary
                    if (entry.hasAttribute("userCertificate;binary")) {
                        parseCertificateEntry(conn, ld, uploadId, entry, "userCertificate;binary",
                                             cscaCount, dscCount, ldapCertStoredCount);
                    }
                    // Check for cACertificate;binary
                    else if (entry.hasAttribute("cACertificate;binary")) {
                        parseCertificateEntry(conn, ld, uploadId, entry, "cACertificate;binary",
                                             cscaCount, dscCount, ldapCertStoredCount);
                    }

                    // Check for CRL
                    if (entry.hasAttribute("certificateRevocationList;binary")) {
                        parseCrlEntry(conn, ld, uploadId, entry, crlCount, ldapCrlStoredCount);
                    }

                } catch (const std::exception& e) {
                    spdlog::warn("Error processing entry {}: {}", entry.dn, e.what());
                }

                processedEntries++;

                // Log progress every 100 entries
                if (processedEntries % 100 == 0) {
                    spdlog::info("Processing progress: {}/{} entries, {} certs ({} LDAP), {} CRLs ({} LDAP)",
                                processedEntries, totalEntries,
                                cscaCount + dscCount, ldapCertStoredCount,
                                crlCount, ldapCrlStoredCount);
                }
            }

            // Update final statistics
            updateUploadStatistics(conn, uploadId, "COMPLETED", cscaCount, dscCount, crlCount,
                                  totalEntries, processedEntries, "");

            spdlog::info("LDIF processing completed for upload {}: {} CSCA, {} DSC, {} CRLs (LDAP: {} certs, {} CRLs)",
                        uploadId, cscaCount, dscCount, crlCount, ldapCertStoredCount, ldapCrlStoredCount);

        } catch (const std::exception& e) {
            spdlog::error("LDIF processing failed for upload {}: {}", uploadId, e.what());
            updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, e.what());
        }

        // Cleanup connections
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }
        PQfinish(conn);
    }).detach();
}

/**
 * @brief Parse Master List (CMS SignedData) and extract CSCA certificates (DB + LDAP)
 * Master List contains CSCA certificates in CMS SignedData format
 */
void processMasterListFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
    std::thread([uploadId, content]() {
        spdlog::info("Starting async Master List processing for upload: {}", uploadId);

        // Connect to PostgreSQL
        std::string conninfo = "host=" + appConfig.dbHost +
                              " port=" + std::to_string(appConfig.dbPort) +
                              " dbname=" + appConfig.dbName +
                              " user=" + appConfig.dbUser +
                              " password=" + appConfig.dbPassword;

        PGconn* conn = PQconnectdb(conninfo.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            spdlog::error("Database connection failed for async processing: {}", PQerrorMessage(conn));
            PQfinish(conn);
            return;
        }

        // Connect to LDAP (write connection - direct to primary master)
        LDAP* ld = getLdapWriteConnection();
        if (!ld) {
            spdlog::warn("LDAP write connection failed - will only save to DB");
        } else {
            spdlog::info("LDAP write connection established for Master List upload {}", uploadId);
        }

        try {
            int cscaCount = 0;
            int dscCount = 0;
            int ldapStoredCount = 0;

            // Try to parse as CMS SignedData (Master List format)
            const uint8_t* p = content.data();
            PKCS7* p7 = d2i_PKCS7(nullptr, &p, static_cast<long>(content.size()));

            if (p7) {
                // Get certificates from PKCS7 structure
                STACK_OF(X509)* certs = nullptr;

                if (PKCS7_type_is_signed(p7)) {
                    certs = p7->d.sign->cert;
                }

                if (certs) {
                    int numCerts = sk_X509_num(certs);
                    spdlog::info("Found {} certificates in Master List", numCerts);

                    for (int i = 0; i < numCerts; i++) {
                        X509* cert = sk_X509_value(certs, i);
                        if (!cert) continue;

                        // Get certificate DER
                        int derLen = i2d_X509(cert, nullptr);
                        if (derLen <= 0) continue;

                        std::vector<uint8_t> derBytes(derLen);
                        uint8_t* derPtr = derBytes.data();
                        i2d_X509(cert, &derPtr);

                        std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
                        std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
                        std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
                        std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
                        std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));
                        std::string fingerprint = computeFileHash(derBytes);
                        std::string countryCode = extractCountryCode(subjectDn);

                        // Master List typically contains CSCA certificates (self-signed)
                        std::string certType = (subjectDn == issuerDn) ? "CSCA" : "DSC";

                        // 1. Save to DB
                        std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                             subjectDn, issuerDn, serialNumber, fingerprint,
                                                             notBefore, notAfter, derBytes);

                        if (!certId.empty()) {
                            if (certType == "CSCA") cscaCount++;
                            else dscCount++;

                            spdlog::debug("Saved {} from Master List to DB: country={}, fingerprint={}",
                                         certType, countryCode, fingerprint.substr(0, 16));

                            // 2. Save to LDAP
                            if (ld) {
                                std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                            subjectDn, issuerDn, serialNumber,
                                                                            fingerprint, derBytes);
                                if (!ldapDn.empty()) {
                                    updateCertificateLdapStatus(conn, certId, ldapDn);
                                    ldapStoredCount++;
                                    spdlog::debug("Saved {} from Master List to LDAP: {}", certType, ldapDn);
                                }
                            }
                        }
                    }
                }

                PKCS7_free(p7);
            } else {
                spdlog::warn("Failed to parse Master List as PKCS7/CMS: {}", ERR_error_string(ERR_get_error(), nullptr));
            }

            // Update statistics
            updateUploadStatistics(conn, uploadId, "COMPLETED", cscaCount, dscCount, 0, 1, 1, "");

            spdlog::info("Master List processing completed for upload {}: {} CSCA, {} DSC certificates (LDAP: {})",
                        uploadId, cscaCount, dscCount, ldapStoredCount);

        } catch (const std::exception& e) {
            spdlog::error("Master List processing failed for upload {}: {}", uploadId, e.what());
            updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, e.what());
        }

        // Cleanup connections
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }
        PQfinish(conn);
    }).detach();
}

/**
 * @brief Check LDAP connectivity
 */
Json::Value checkLdap() {
    Json::Value result;
    result["name"] = "ldap";

    try {
        // Simple LDAP connection test using system ldapsearch
        auto start = std::chrono::steady_clock::now();

        std::string cmd = "ldapsearch -x -H ldap://" + appConfig.ldapHost + ":" +
                         std::to_string(appConfig.ldapPort) +
                         " -b \"\" -s base \"(objectclass=*)\" namingContexts 2>/dev/null | grep -q namingContexts";

        int ret = system(cmd.c_str());
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (ret == 0) {
            result["status"] = "UP";
            result["responseTimeMs"] = static_cast<int>(duration.count());
            result["host"] = appConfig.ldapHost;
            result["port"] = appConfig.ldapPort;
        } else {
            result["status"] = "DOWN";
            result["error"] = "LDAP connection failed";
        }
    } catch (const std::exception& e) {
        result["status"] = "DOWN";
        result["error"] = e.what();
    }

    return result;
}

/**
 * @brief Register API controllers and routes
 */
void registerRoutes() {
    auto& app = drogon::app();

    // Health check endpoint
    app.registerHandler(
        "/api/health",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["status"] = "UP";
            result["service"] = "icao-local-pkd";
            result["version"] = "1.0.0";
            result["timestamp"] = trantor::Date::now().toFormattedString(false);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Database health check endpoint
    app.registerHandler(
        "/api/health/database",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto result = checkDatabase();

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // LDAP health check endpoint
    app.registerHandler(
        "/api/health/ldap",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto result = checkLdap();

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // Upload LDIF file endpoint
    app.registerHandler(
        "/api/upload/ldif",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/upload/ldif - LDIF file upload");

            try {
                // Parse multipart form data
                drogon::MultiPartParser parser;
                if (parser.parse(req) != 0) {
                    Json::Value error;
                    error["success"] = false;
                    error["message"] = "Invalid multipart form data";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto& files = parser.getFiles();
                if (files.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["message"] = "No file uploaded";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto& file = files[0];
                std::string fileName = file.getFileName();
                std::string content = std::string(file.fileData(), file.fileLength());
                std::vector<uint8_t> contentBytes(content.begin(), content.end());
                int64_t fileSize = static_cast<int64_t>(content.size());

                // Compute file hash
                std::string fileHash = computeFileHash(contentBytes);

                // Connect to database
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;

                PGconn* conn = PQconnectdb(conninfo.c_str());
                if (PQstatus(conn) != CONNECTION_OK) {
                    std::string error = PQerrorMessage(conn);
                    PQfinish(conn);
                    Json::Value errorResp;
                    errorResp["success"] = false;
                    errorResp["message"] = "Database connection failed: " + error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(errorResp);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Save upload record
                std::string uploadId = saveUploadRecord(conn, fileName, fileSize, "LDIF", fileHash);
                PQfinish(conn);

                // Start async processing
                processLdifFileAsync(uploadId, contentBytes);

                // Return success response
                Json::Value result;
                result["success"] = true;
                result["message"] = "LDIF file uploaded successfully. Processing started.";

                Json::Value data;
                data["uploadId"] = uploadId;
                data["fileName"] = fileName;
                data["fileSize"] = static_cast<Json::Int64>(fileSize);
                data["status"] = "PROCESSING";
                data["createdAt"] = trantor::Date::now().toFormattedString(false);

                result["data"] = data;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k201Created);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("LDIF upload failed: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["message"] = std::string("Upload failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Post}
    );

    // Upload Master List file endpoint
    app.registerHandler(
        "/api/upload/masterlist",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/upload/masterlist - Master List file upload");

            try {
                // Parse multipart form data
                drogon::MultiPartParser parser;
                if (parser.parse(req) != 0) {
                    Json::Value error;
                    error["success"] = false;
                    error["message"] = "Invalid multipart form data";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto& files = parser.getFiles();
                if (files.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["message"] = "No file uploaded";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                auto& file = files[0];
                std::string fileName = file.getFileName();
                std::string content = std::string(file.fileData(), file.fileLength());
                std::vector<uint8_t> contentBytes(content.begin(), content.end());
                int64_t fileSize = static_cast<int64_t>(content.size());

                // Compute file hash
                std::string fileHash = computeFileHash(contentBytes);

                // Connect to database
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;

                PGconn* conn = PQconnectdb(conninfo.c_str());
                if (PQstatus(conn) != CONNECTION_OK) {
                    std::string error = PQerrorMessage(conn);
                    PQfinish(conn);
                    Json::Value errorResp;
                    errorResp["success"] = false;
                    errorResp["message"] = "Database connection failed: " + error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(errorResp);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Save upload record
                std::string uploadId = saveUploadRecord(conn, fileName, fileSize, "ML", fileHash);
                PQfinish(conn);

                // Start async processing
                processMasterListFileAsync(uploadId, contentBytes);

                // Return success response
                Json::Value result;
                result["success"] = true;
                result["message"] = "Master List file uploaded successfully. Processing started.";

                Json::Value data;
                data["uploadId"] = uploadId;
                data["fileName"] = fileName;
                data["fileSize"] = static_cast<Json::Int64>(fileSize);
                data["status"] = "PROCESSING";
                data["createdAt"] = trantor::Date::now().toFormattedString(false);

                result["data"] = data;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k201Created);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Master List upload failed: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["message"] = std::string("Upload failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Post}
    );

    // Upload statistics endpoint - returns UploadStatisticsOverview format
    app.registerHandler(
        "/api/upload/statistics",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/statistics");

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            Json::Value result;

            if (PQstatus(conn) == CONNECTION_OK) {
                // Get total uploads
                PGresult* res = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file");
                result["totalUploads"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get successful uploads
                res = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file WHERE status = 'COMPLETED'");
                result["successfulUploads"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get failed uploads
                res = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file WHERE status = 'FAILED'");
                result["failedUploads"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get total certificates (sum of certificate_count from all uploads)
                res = PQexec(conn, "SELECT COALESCE(SUM(csca_count + dsc_count), 0) FROM uploaded_file");
                result["totalCertificates"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get CRL count (sum of crl_count from all uploads)
                res = PQexec(conn, "SELECT COALESCE(SUM(crl_count), 0) FROM uploaded_file");
                result["crlCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get CSCA count from certificate table
                res = PQexec(conn, "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA'");
                result["cscaCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get DSC count from certificate table
                res = PQexec(conn, "SELECT COUNT(*) FROM certificate WHERE certificate_type IN ('DSC', 'DSC_NC')");
                result["dscCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get distinct country count from certificate table
                res = PQexec(conn, "SELECT COUNT(DISTINCT country_code) FROM certificate");
                result["countriesCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

            } else {
                result["totalUploads"] = 0;
                result["successfulUploads"] = 0;
                result["failedUploads"] = 0;
                result["totalCertificates"] = 0;
                result["cscaCount"] = 0;
                result["dscCount"] = 0;
                result["crlCount"] = 0;
                result["countriesCount"] = 0;
            }

            PQfinish(conn);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Upload history endpoint - returns PageResponse format
    app.registerHandler(
        "/api/upload/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/history");

            // Get query parameters
            int page = 0;
            int size = 20;
            if (auto p = req->getParameter("page"); !p.empty()) {
                page = std::stoi(p);
            }
            if (auto s = req->getParameter("size"); !s.empty()) {
                size = std::stoi(s);
            }

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            Json::Value result;
            result["content"] = Json::Value(Json::arrayValue);
            result["page"] = page;
            result["size"] = size;

            if (PQstatus(conn) == CONNECTION_OK) {
                // Get total count
                PGresult* countRes = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file");
                int totalElements = (PQntuples(countRes) > 0) ? std::stoi(PQgetvalue(countRes, 0, 0)) : 0;
                PQclear(countRes);

                result["totalElements"] = totalElements;
                result["totalPages"] = (totalElements + size - 1) / size;
                result["first"] = (page == 0);
                result["last"] = (page >= result["totalPages"].asInt() - 1);

                // Get paginated results
                int offset = page * size;
                std::string query = "SELECT id, file_name, file_format, file_size, status, "
                                   "(csca_count + dsc_count) as certificate_count, crl_count, error_message, upload_timestamp, completed_timestamp "
                                   "FROM uploaded_file ORDER BY upload_timestamp DESC "
                                   "LIMIT " + std::to_string(size) + " OFFSET " + std::to_string(offset);

                PGresult* res = PQexec(conn, query.c_str());
                if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                    for (int i = 0; i < PQntuples(res); i++) {
                        Json::Value item;
                        item["id"] = PQgetvalue(res, i, 0);
                        item["fileName"] = PQgetvalue(res, i, 1);
                        item["fileFormat"] = PQgetvalue(res, i, 2);
                        item["fileSize"] = static_cast<Json::Int64>(std::stoll(PQgetvalue(res, i, 3)));
                        item["status"] = PQgetvalue(res, i, 4);
                        item["certificateCount"] = std::stoi(PQgetvalue(res, i, 5));
                        item["crlCount"] = std::stoi(PQgetvalue(res, i, 6));
                        item["errorMessage"] = PQgetvalue(res, i, 7) ? PQgetvalue(res, i, 7) : "";
                        item["createdAt"] = PQgetvalue(res, i, 8);
                        item["updatedAt"] = PQgetvalue(res, i, 9);
                        result["content"].append(item);
                    }
                }
                PQclear(res);
            } else {
                result["totalElements"] = 0;
                result["totalPages"] = 0;
                result["first"] = true;
                result["last"] = true;
            }

            PQfinish(conn);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // PA statistics endpoint - returns PAStatisticsOverview format
    app.registerHandler(
        "/api/pa/statistics",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/statistics");

            // Return PAStatisticsOverview format matching frontend expectations
            Json::Value result;
            result["totalVerifications"] = 0;
            result["validCount"] = 0;
            result["invalidCount"] = 0;
            result["errorCount"] = 0;
            result["averageProcessingTimeMs"] = 0;
            result["countriesVerified"] = 0;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
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

            // Mock response for PA verification
            Json::Value result;
            result["success"] = true;

            Json::Value data;
            data["id"] = "pa-" + std::to_string(std::time(nullptr));
            data["status"] = "VALID";
            data["overallValid"] = true;
            data["verifiedAt"] = trantor::Date::now().toFormattedString(false);
            data["processingTimeMs"] = 150;

            // Step results
            Json::Value sodParsing;
            sodParsing["step"] = "SOD_PARSING";
            sodParsing["status"] = "SUCCESS";
            sodParsing["message"] = "SOD 파싱 완료";
            data["sodParsing"] = sodParsing;

            Json::Value dscExtraction;
            dscExtraction["step"] = "DSC_EXTRACTION";
            dscExtraction["status"] = "SUCCESS";
            dscExtraction["message"] = "DSC 인증서 추출 완료";
            data["dscExtraction"] = dscExtraction;

            Json::Value cscaLookup;
            cscaLookup["step"] = "CSCA_LOOKUP";
            cscaLookup["status"] = "SUCCESS";
            cscaLookup["message"] = "CSCA 인증서 조회 완료";
            data["cscaLookup"] = cscaLookup;

            Json::Value trustChainValidation;
            trustChainValidation["step"] = "TRUST_CHAIN_VALIDATION";
            trustChainValidation["status"] = "SUCCESS";
            trustChainValidation["message"] = "Trust Chain 검증 완료";
            data["trustChainValidation"] = trustChainValidation;

            Json::Value sodSignatureValidation;
            sodSignatureValidation["step"] = "SOD_SIGNATURE_VALIDATION";
            sodSignatureValidation["status"] = "SUCCESS";
            sodSignatureValidation["message"] = "SOD 서명 검증 완료";
            data["sodSignatureValidation"] = sodSignatureValidation;

            Json::Value dataGroupHashValidation;
            dataGroupHashValidation["step"] = "DATA_GROUP_HASH_VALIDATION";
            dataGroupHashValidation["status"] = "SUCCESS";
            dataGroupHashValidation["message"] = "Data Group 해시 검증 완료";
            data["dataGroupHashValidation"] = dataGroupHashValidation;

            Json::Value crlCheck;
            crlCheck["step"] = "CRL_CHECK";
            crlCheck["status"] = "SUCCESS";
            crlCheck["message"] = "CRL 확인 완료 - 인증서 유효";
            data["crlCheck"] = crlCheck;

            result["data"] = data;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            resp->setStatusCode(drogon::k200OK);
            callback(resp);
        },
        {drogon::Post}
    );

    // LDAP health check endpoint (for frontend Dashboard)
    app.registerHandler(
        "/api/ldap/health",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/ldap/health");
            auto result = checkLdap();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // PA history endpoint - returns PageResponse format
    app.registerHandler(
        "/api/pa/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/history");

            // Get query parameters
            int page = 0;
            int size = 20;
            if (auto p = req->getParameter("page"); !p.empty()) {
                page = std::stoi(p);
            }
            if (auto s = req->getParameter("size"); !s.empty()) {
                size = std::stoi(s);
            }

            // Return PageResponse format matching frontend expectations
            Json::Value result;
            result["content"] = Json::Value(Json::arrayValue);
            result["page"] = page;
            result["size"] = size;
            result["totalElements"] = 0;
            result["totalPages"] = 0;
            result["first"] = true;
            result["last"] = true;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Root endpoint
    app.registerHandler(
        "/",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["name"] = "ICAO Local PKD";
            result["description"] = "ICAO Local PKD Management and Passive Authentication System";
            result["version"] = "1.0.0";
            result["endpoints"]["health"] = "/api/health";
            result["endpoints"]["upload"] = "/api/upload";
            result["endpoints"]["pa"] = "/api/pa";
            result["endpoints"]["ldap"] = "/api/ldap";

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // API info endpoint
    app.registerHandler(
        "/api",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["api"] = "ICAO Local PKD REST API";
            result["version"] = "v1";

            Json::Value endpoints(Json::arrayValue);

            Json::Value health;
            health["method"] = "GET";
            health["path"] = "/api/health";
            health["description"] = "Health check endpoint";
            endpoints.append(health);

            Json::Value healthDb;
            healthDb["method"] = "GET";
            healthDb["path"] = "/api/health/database";
            healthDb["description"] = "Database health check";
            endpoints.append(healthDb);

            Json::Value healthLdap;
            healthLdap["method"] = "GET";
            healthLdap["path"] = "/api/health/ldap";
            healthLdap["description"] = "LDAP health check";
            endpoints.append(healthLdap);

            Json::Value uploadLdif;
            uploadLdif["method"] = "POST";
            uploadLdif["path"] = "/api/upload/ldif";
            uploadLdif["description"] = "Upload LDIF file";
            endpoints.append(uploadLdif);

            Json::Value uploadMl;
            uploadMl["method"] = "POST";
            uploadMl["path"] = "/api/upload/masterlist";
            uploadMl["description"] = "Upload Master List file";
            endpoints.append(uploadMl);

            Json::Value uploadHistory;
            uploadHistory["method"] = "GET";
            uploadHistory["path"] = "/api/upload/history";
            uploadHistory["description"] = "Get upload history";
            endpoints.append(uploadHistory);

            Json::Value uploadStats;
            uploadStats["method"] = "GET";
            uploadStats["path"] = "/api/upload/statistics";
            uploadStats["description"] = "Get upload statistics";
            endpoints.append(uploadStats);

            Json::Value paVerify;
            paVerify["method"] = "POST";
            paVerify["path"] = "/api/pa/verify";
            paVerify["description"] = "Perform Passive Authentication";
            endpoints.append(paVerify);

            Json::Value paHistory;
            paHistory["method"] = "GET";
            paHistory["path"] = "/api/pa/history";
            paHistory["description"] = "Get PA verification history";
            endpoints.append(paHistory);

            Json::Value paStats;
            paStats["method"] = "GET";
            paStats["path"] = "/api/pa/statistics";
            paStats["description"] = "Get PA verification statistics";
            endpoints.append(paStats);

            result["endpoints"] = endpoints;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    spdlog::info("API routes registered");
}

} // anonymous namespace

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    // Print banner
    printBanner();

    // Initialize logging
    initializeLogging();

    // Load configuration from environment
    appConfig = AppConfig::fromEnvironment();

    spdlog::info("Starting ICAO Local PKD Application...");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP: {}:{}", appConfig.ldapHost, appConfig.ldapPort);

    try {
        auto& app = drogon::app();

        // Server settings
        app.setLogPath("logs")
           .setLogLevel(trantor::Logger::kInfo)
           .addListener("0.0.0.0", appConfig.serverPort)
           .setThreadNum(appConfig.threadNum)
           .enableGzip(true)
           .setClientMaxBodySize(100 * 1024 * 1024)  // 100MB max upload
           .setUploadPath("./uploads")
           .setDocumentRoot("./static");

        // Enable CORS for React.js frontend
        app.registerPreSendingAdvice([](const drogon::HttpRequestPtr& req,
                                         const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-User-Id");
        });

        // Handle OPTIONS requests for CORS preflight
        app.registerHandler(
            "/{path}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& path) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                callback(resp);
            },
            {drogon::Options}
        );

        // Register routes
        registerRoutes();

        spdlog::info("Server starting on http://0.0.0.0:{}", appConfig.serverPort);
        spdlog::info("Press Ctrl+C to stop the server");

        // Run the server
        app.run();

    } catch (const std::exception& e) {
        spdlog::error("Application error: {}", e.what());
        return 1;
    }

    spdlog::info("Server stopped");
    return 0;
}

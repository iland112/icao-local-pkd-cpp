/**
 * @file main.cpp
 * @brief Passive Authentication Service - ICAO 9303 PA Verification
 *
 * C++ REST API based Passive Authentication Service.
 * Handles SOD parsing, DSC/CSCA trust chain verification,
 * Data Group hash validation, and CRL checking.
 *
 * @author SmartCore Inc.
 * @date 2025-12-30
 * @version 1.0.0
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

    // LDAP Read: HAProxy for load balancing (PA only needs read access)
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

// Global configuration instance
AppConfig appConfig;

/**
 * @brief Generate UUID v4
 */
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
    ss << "-4";  // Version 4
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);  // Variant
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);

    return ss.str();
}

/**
 * @brief Print application banner
 */
void printBanner() {
    std::cout << R"(
  ____   _      ____                  _
 |  _ \ / \    / ___|  ___ _ ____   _(_) ___ ___
 | |_) / _ \   \___ \ / _ \ '__\ \ / / |/ __/ _ \
 |  __/ ___ \   ___) |  __/ |   \ V /| | (_|  __/
 |_| /_/   \_\ |____/ \___|_|    \_/ |_|\___\___|

)" << std::endl;
    std::cout << "  PA Service - ICAO Passive Authentication" << std::endl;
    std::cout << "  Version: 1.0.0" << std::endl;
    std::cout << "  (C) 2025 SmartCore Inc." << std::endl;
    std::cout << std::endl;
}

/**
 * @brief Initialize logging system
 */
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

/**
 * @brief Check LDAP connectivity
 */
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

/**
 * @brief Get LDAP connection for read operations
 */
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

/**
 * @brief Search CSCA certificate by issuer DN from LDAP
 */
std::vector<uint8_t> searchCscaByIssuerDn(LDAP* ld, const std::string& issuerDn) {
    std::vector<uint8_t> result;

    // Extract country code from issuer DN
    std::string countryCode;
    std::regex countryRegex("C=([A-Z]{2})", std::regex::icase);
    std::smatch match;
    if (std::regex_search(issuerDn, match, countryRegex)) {
        countryCode = match[1].str();
        std::transform(countryCode.begin(), countryCode.end(), countryCode.begin(), ::toupper);
    }

    if (countryCode.empty()) {
        spdlog::warn("Could not extract country code from issuer DN: {}", issuerDn);
        return result;
    }

    // Search in CSCA OU for the country
    std::string baseDn = "o=csca,c=" + countryCode + ",dc=data,dc=download," + appConfig.ldapBaseDn;
    std::string filter = "(objectClass=pkdDownload)";
    char* attrs[] = {const_cast<char*>("userCertificate;binary"), nullptr};

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(ld, baseDn.c_str(), LDAP_SCOPE_SUBTREE, filter.c_str(),
                               attrs, 0, nullptr, nullptr, nullptr, 100, &res);

    if (rc == LDAP_SUCCESS && res) {
        LDAPMessage* entry = ldap_first_entry(ld, res);
        while (entry) {
            struct berval** values = ldap_get_values_len(ld, entry, "userCertificate;binary");
            if (values && values[0]) {
                result.assign(reinterpret_cast<uint8_t*>(values[0]->bv_val),
                             reinterpret_cast<uint8_t*>(values[0]->bv_val) + values[0]->bv_len);
                ldap_value_free_len(values);
                break;  // Take first matching certificate
            }
            entry = ldap_next_entry(ld, entry);
        }
        ldap_msgfree(res);
    }

    return result;
}

/**
 * @brief Register API routes
 */
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
            result["version"] = "1.0.0";
            result["timestamp"] = trantor::Date::now().toFormattedString(false);

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

    // PA statistics endpoint
    app.registerHandler(
        "/api/pa/statistics",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/statistics");

            // TODO: Query from database
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

            auto startTime = std::chrono::steady_clock::now();
            std::string paId = "pa-" + generateUuid();

            // Parse request body
            auto jsonBody = req->getJsonObject();
            if (!jsonBody) {
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

            // TODO: Implement full PA verification:
            // 1. Parse SOD (Security Object Document)
            // 2. Extract DSC from SOD
            // 3. Lookup CSCA from LDAP
            // 4. Verify Trust Chain (DSC -> CSCA)
            // 5. Verify SOD Signature
            // 6. Verify Data Group Hashes
            // 7. Check CRL for revocation

            auto endTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

            // Mock response for now
            Json::Value result;
            result["success"] = true;

            Json::Value data;
            data["id"] = paId;
            data["status"] = "VALID";
            data["overallValid"] = true;
            data["verifiedAt"] = trantor::Date::now().toFormattedString(false);
            data["processingTimeMs"] = static_cast<int>(duration.count());

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

    // PA history endpoint
    app.registerHandler(
        "/api/pa/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/history");

            int page = 0;
            int size = 20;
            if (auto p = req->getParameter("page"); !p.empty()) {
                page = std::stoi(p);
            }
            if (auto s = req->getParameter("size"); !s.empty()) {
                size = std::stoi(s);
            }

            // TODO: Query from database
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

    // PA detail by ID
    app.registerHandler(
        "/api/pa/{id}",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& id) {
            spdlog::info("GET /api/pa/{}", id);

            // TODO: Query from database
            Json::Value result;
            result["id"] = id;
            result["status"] = "NOT_FOUND";
            result["message"] = "PA verification record not found";

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
        },
        {drogon::Get}
    );

    // Parse DG1 (MRZ) endpoint
    app.registerHandler(
        "/api/pa/parse-dg1",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-dg1");

            auto jsonBody = req->getJsonObject();
            if (!jsonBody || (*jsonBody)["dg1"].asString().empty()) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "DG1 data is required";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // TODO: Implement DG1 (MRZ) parsing
            Json::Value result;
            result["success"] = true;
            result["data"]["documentType"] = "P";
            result["data"]["issuingCountry"] = "KOR";
            result["data"]["surname"] = "DOE";
            result["data"]["givenNames"] = "JOHN";
            result["data"]["documentNumber"] = "M12345678";
            result["data"]["nationality"] = "KOR";
            result["data"]["dateOfBirth"] = "19900101";
            result["data"]["sex"] = "M";
            result["data"]["dateOfExpiry"] = "20300101";

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Parse DG2 (Face Image) endpoint
    app.registerHandler(
        "/api/pa/parse-dg2",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/parse-dg2");

            auto jsonBody = req->getJsonObject();
            if (!jsonBody || (*jsonBody)["dg2"].asString().empty()) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "DG2 data is required";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // TODO: Implement DG2 (Face Image) parsing
            Json::Value result;
            result["success"] = true;
            result["data"]["imageFormat"] = "JPEG2000";
            result["data"]["imageWidth"] = 240;
            result["data"]["imageHeight"] = 320;
            result["data"]["imageSize"] = 15000;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Root endpoint
    app.registerHandler(
        "/",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["name"] = "PA Service";
            result["description"] = "ICAO Passive Authentication Service - ePassport PA Verification";
            result["version"] = "1.0.0";
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
            result["version"] = "v1";

            Json::Value endpoints(Json::arrayValue);

            Json::Value health;
            health["method"] = "GET";
            health["path"] = "/api/health";
            health["description"] = "Health check endpoint";
            endpoints.append(health);

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

            Json::Value parseDg1;
            parseDg1["method"] = "POST";
            parseDg1["path"] = "/api/pa/parse-dg1";
            parseDg1["description"] = "Parse DG1 (MRZ) data";
            endpoints.append(parseDg1);

            Json::Value parseDg2;
            parseDg2["method"] = "POST";
            parseDg2["path"] = "/api/pa/parse-dg2";
            parseDg2["description"] = "Parse DG2 (Face Image) data";
            endpoints.append(parseDg2);

            result["endpoints"] = endpoints;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    spdlog::info("PA Service API routes registered");
}

} // anonymous namespace

/**
 * @brief Main entry point
 */
int main(int /* argc */, char* /* argv */[]) {
    // Print banner
    printBanner();

    // Initialize logging
    initializeLogging();

    // Load configuration from environment
    appConfig = AppConfig::fromEnvironment();

    spdlog::info("Starting PA Service...");
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
           .setClientMaxBodySize(50 * 1024 * 1024)  // 50MB max for PA requests
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

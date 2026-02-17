/**
 * @file main.cpp
 * @brief PA Service - ICAO 9303 Passive Authentication Entry Point
 */

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

// PostgreSQL header for checkDatabase()
#include <libpq-fe.h>

// OpenLDAP header for checkLdap()
#include <ldap.h>

// Project headers
#include "infrastructure/app_config.h"
#include "infrastructure/service_container.h"
#include "i_query_executor.h"
#include "handlers/health_handler.h"
#include "handlers/pa_handler.h"
#include "handlers/info_handler.h"

namespace {

AppConfig appConfig;
infrastructure::ServiceContainer* g_services = nullptr;

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

void printBanner() {
    std::cout << R"(
  ____   _      ____                  _
 |  _ \ / \    / ___|  ___ _ ____   _(_) ___ ___
 | |_) / _ \   \___ \ / _ \ '__\ \ / / |/ __/ _ \
 |  __/ ___ \   ___) |  __/ |   \ V /| | (_|  __/
 |_| /_/   \_\ |____/ \___|_|    \_/ |_|\___\___|

)" << std::endl;
    std::cout << "  PA Service - ICAO Passive Authentication" << std::endl;
    std::cout << "  Version: 2.1.0" << std::endl;
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

Json::Value checkDatabase() {
    Json::Value result;
    result["name"] = "database";

    if (!g_services || !g_services->queryExecutor()) {
        result["status"] = "DOWN";
        result["error"] = "Query executor not initialized";
        return result;
    }

    auto start = std::chrono::steady_clock::now();

    try {
        std::string dbType = g_services->queryExecutor()->getDatabaseType();
        std::string versionQuery = (dbType == "oracle")
            ? "SELECT banner AS version FROM v$version WHERE ROWNUM = 1"
            : "SELECT version()";

        auto rows = g_services->queryExecutor()->executeQuery(versionQuery);

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result["status"] = "UP";
        result["responseTimeMs"] = static_cast<int>(duration.count());
        result["type"] = (dbType == "oracle") ? "Oracle" : "PostgreSQL";
        if (rows.size() > 0 && rows[0].isMember("version")) {
            result["version"] = rows[0]["version"].asString();
        }
    } catch (const std::exception& e) {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        result["status"] = "DOWN";
        result["error"] = e.what();
        result["responseTimeMs"] = static_cast<int>(duration.count());
    }

    return result;
}

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

        struct timeval tv = {3, 0};
        ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv);

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

void registerRoutes() {
    auto& app = drogon::app();

    // HealthHandler
    static handlers::HealthHandler healthHandler(
        checkDatabase,
        checkLdap,
        getCurrentTimestamp
    );
    healthHandler.registerRoutes(app);

    // PaHandler
    static handlers::PaHandler paHandler(
        g_services->paVerificationService(),
        g_services->dataGroupRepository(),
        g_services->sodParser(),
        g_services->dgParser()
    );
    paHandler.registerRoutes(app);

    // InfoHandler
    static handlers::InfoHandler infoHandler;
    infoHandler.registerRoutes(app);

    spdlog::info("PA Service API routes registered (16 endpoints via 3 handlers)");
}

} // anonymous namespace

int main(int /* argc */, char* /* argv */[]) {
    printBanner();
    initializeLogging();

    appConfig = AppConfig::fromEnvironment();

    try {
        appConfig.validateRequiredCredentials();
    } catch (const std::exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    }

    spdlog::info("Starting PA Service v2.13.0...");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP: {}:{}", appConfig.ldapHost, appConfig.ldapPort);

    // Initialize ServiceContainer
    g_services = new infrastructure::ServiceContainer();
    if (!g_services->initialize(appConfig)) {
        spdlog::critical("ServiceContainer initialization failed");
        delete g_services;
        g_services = nullptr;
        return 1;
    }

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
        app.registerPreSendingAdvice([](const drogon::HttpRequestPtr&,
                                         const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-User-Id");
        });

        // Handle OPTIONS requests for CORS preflight
        app.registerHandler(
            "/{path}",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string&) {
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

        // Cleanup
        spdlog::info("Shutting down ServiceContainer...");
        delete g_services;
        g_services = nullptr;

    } catch (const std::exception& e) {
        spdlog::error("Application error: {}", e.what());
        delete g_services;
        g_services = nullptr;
        return 1;
    }

    spdlog::info("Server stopped");
    return 0;
}

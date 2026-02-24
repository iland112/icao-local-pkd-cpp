/**
 * @file main.cpp
 * @brief ICAO Local PKD Application Entry Point
 *
 * C++ REST API based ICAO Local PKD Management and
 * Passive Authentication (PA) Verification System.
 */

#include <drogon/drogon.h>
#include <trantor/utils/Date.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <iostream>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

// PostgreSQL header for checkDatabase()
#include <libpq-fe.h>

// OpenLDAP header for checkLdap()
#include <ldap.h>

// Project headers
#include <icao/audit/audit_log.h>
#include "common/progress_manager.h"

// Infrastructure
#include "infrastructure/service_container.h"
#include "infrastructure/app_config.h"

// Handler includes for route registration
#include "handlers/auth_handler.h"
#include "handlers/upload_handler.h"
#include "handlers/upload_stats_handler.h"
#include "handlers/certificate_handler.h"
#include "handlers/misc_handler.h"
#include "handlers/icao_handler.h"
#include "handlers/code_master_handler.h"

// Authentication middleware
#include "middleware/auth_middleware.h"

// Services (for route registration)
#include "services/audit_service.h"
#include "services/validation_service.h"
#include "services/icao_sync_service.h"

// Global service container (accessed by processing functions and route handlers)
infrastructure::ServiceContainer* g_services = nullptr;

namespace {

// Global configuration (AppConfig struct defined in infrastructure/app_config.h)
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
 * @brief Check database connectivity using QueryExecutor
 */
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

/**
 * @brief Check LDAP connectivity using LDAP C API (no system() calls)
 */
Json::Value checkLdap() {
    Json::Value result;
    result["name"] = "ldap";

    try {
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
            result["host"] = appConfig.ldapHost;
            result["port"] = appConfig.ldapPort;
        } else {
            result["status"] = "DOWN";
            result["error"] = std::string("LDAP connection failed: ") + ldap_err2string(rc);
        }
    } catch (const std::exception& e) {
        result["status"] = "DOWN";
        result["error"] = e.what();
    }

    return result;
}

void registerRoutes() {
    auto& app = drogon::app();

    // --- Register Authentication Middleware (Global) ---
    // Note: Authentication is DISABLED by default for backward compatibility
    // Enable by setting: AUTH_ENABLED=true in environment
    //
    // IMPORTANT: AuthMiddleware uses HttpFilterBase (not HttpFilter<T>) for manual
    // instantiation with parameters. It cannot be registered globally via registerFilter().
    // Instead, apply it to individual routes using .addFilter() method.

    // --- Authentication Routes ---
    // Note: AuthMiddleware is applied globally via registerPreHandlingAdvice()
    // (see initialization section at end of main())
    if (g_services && g_services->authHandler()) {
        g_services->authHandler()->registerRoutes(app);
    }

    // --- Upload Routes (extracted to UploadHandler) ---
    if (g_services && g_services->uploadHandler()) {
        g_services->uploadHandler()->registerRoutes(app);
    }

    // --- Upload Stats Routes (extracted to UploadStatsHandler) ---
    if (g_services && g_services->uploadStatsHandler()) {
        g_services->uploadStatsHandler()->registerRoutes(app);
    }

    // --- Certificate Routes (extracted to CertificateHandler) ---
    if (g_services && g_services->certificateHandler()) {
        g_services->certificateHandler()->registerRoutes(app);
    }

    // --- Misc Routes (extracted to MiscHandler) ---
    // Health checks, audit log, validation revalidate, PA mocks, info endpoints, OpenAPI
    static handlers::MiscHandler miscHandler(
        g_services->auditService(),
        g_services->validationService(),
        checkDatabase,
        checkLdap
    );
    miscHandler.registerRoutes(app);

    // --- Code Master Routes ---
    if (g_services && g_services->codeMasterHandler()) {
        g_services->codeMasterHandler()->registerRoutes(app);
    }

    // --- API Client Routes ---
    if (g_services && g_services->apiClientHandler()) {
        g_services->apiClientHandler()->registerRoutes(app);
    }

    // --- ICAO Routes ---
    if (g_services && g_services->icaoHandler()) {
        g_services->icaoHandler()->registerRoutes(app);
    }


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

    // Validate required credentials
    try {
        appConfig.validateRequiredCredentials();
    } catch (const std::exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    }

    spdlog::info("====== ICAO Local PKD Management Service ======");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP: {}:{}", appConfig.ldapHost, appConfig.ldapPort);

    // Initialize ServiceContainer (all pools, repos, services, handlers)
    g_services = new infrastructure::ServiceContainer();
    if (!g_services->initialize(appConfig)) {
        spdlog::critical("ServiceContainer initialization failed");
        delete g_services;
        g_services = nullptr;
        return 1;
    }

    try {
        auto& app = drogon::app();

        // Server settings
        app.setLogPath("logs")
           .setLogLevel(trantor::Logger::kInfo)
           .addListener("0.0.0.0", appConfig.serverPort)
           .setThreadNum(appConfig.threadNum)
           .enableGzip(true)
           .setClientMaxBodySize(100 * 1024 * 1024)  // 100MB max upload
           .setUploadPath("/app/uploads")  // Absolute path for security
           .setDocumentRoot("./static");

        // Enable CORS for React.js frontend
        app.registerPreSendingAdvice([](const drogon::HttpRequestPtr& req,
                                         const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-User-Id");
        });

        // Register AuthMiddleware globally for JWT authentication
        spdlog::info("Registering AuthMiddleware globally...");
        try {
            auto authMiddleware = std::make_shared<middleware::AuthMiddleware>();

            app.registerPreHandlingAdvice([authMiddleware](const drogon::HttpRequestPtr& req,
                                                           drogon::AdviceCallback&& callback,
                                                           drogon::AdviceChainCallback&& chainCallback) {
                // AuthMiddleware will validate JWT and set session for non-public endpoints
                authMiddleware->doFilter(
                    req,
                    [callback = std::move(callback)](const drogon::HttpResponsePtr& resp) mutable {
                        // Authentication failed - return error response
                        callback(resp);
                    },
                    [chainCallback = std::move(chainCallback)]() mutable {
                        // Authentication succeeded or public endpoint - continue to handler
                        chainCallback();
                    }
                );
            });

            spdlog::info("AuthMiddleware registered globally - JWT authentication enabled");
        } catch (const std::exception& e) {
            spdlog::error("Failed to register AuthMiddleware: {}", e.what());
            spdlog::warn("Server will start WITHOUT authentication!");
        }

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

        // ICAO Auto Version Check Scheduler
        if (appConfig.icaoSchedulerEnabled) {
            spdlog::info("[IcaoScheduler] Setting up daily version check at {:02d}:00",
                        appConfig.icaoCheckScheduleHour);

            // Calculate seconds until next target hour
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            struct tm tm_now;
            localtime_r(&time_t_now, &tm_now);

            int currentSeconds = tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec;
            int targetSeconds = appConfig.icaoCheckScheduleHour * 3600;
            int delaySeconds = targetSeconds - currentSeconds;
            if (delaySeconds <= 0) {
                delaySeconds += 86400;  // Next day
            }

            spdlog::info("[IcaoScheduler] First check scheduled in {} seconds ({:.1f} hours)",
                        delaySeconds, delaySeconds / 3600.0);

            // Capture raw pointer - ServiceContainer outlives all timers
            auto* scheduledIcaoSvc = g_services->icaoSyncService();

            app.getLoop()->runAfter(static_cast<double>(delaySeconds), [scheduledIcaoSvc, &app]() {
                spdlog::info("[IcaoScheduler] Running scheduled ICAO version check");
                try {
                    auto result = scheduledIcaoSvc->checkForUpdates();
                    spdlog::info("[IcaoScheduler] Check complete: {} (new versions: {})",
                                result.message, result.newVersionCount);
                } catch (const std::exception& e) {
                    spdlog::error("[IcaoScheduler] Exception during scheduled check: {}", e.what());
                }

                // Register recurring 24-hour timer
                app.getLoop()->runEvery(86400.0, [scheduledIcaoSvc]() {
                    spdlog::info("[IcaoScheduler] Running daily ICAO version check");
                    try {
                        auto result = scheduledIcaoSvc->checkForUpdates();
                        spdlog::info("[IcaoScheduler] Check complete: {} (new versions: {})",
                                    result.message, result.newVersionCount);
                    } catch (const std::exception& e) {
                        spdlog::error("[IcaoScheduler] Exception during daily check: {}", e.what());
                    }
                });
            });

            spdlog::info("[IcaoScheduler] Scheduler enabled (daily at {:02d}:00)",
                        appConfig.icaoCheckScheduleHour);
        } else {
            spdlog::info("[IcaoScheduler] Scheduler disabled (ICAO_SCHEDULER_ENABLED=false)");
        }

        spdlog::info("Server starting on http://0.0.0.0:{}", appConfig.serverPort);
        spdlog::info("Press Ctrl+C to stop the server");

        // Run the server
        app.run();

        // Cleanup - Release all resources via ServiceContainer
        spdlog::info("Shutting down ServiceContainer resources...");
        delete g_services;
        g_services = nullptr;
        spdlog::info("ServiceContainer resources cleaned up");

    } catch (const std::exception& e) {
        spdlog::error("Application error: {}", e.what());

        // Cleanup on error
        delete g_services;
        g_services = nullptr;

        return 1;
    }

    spdlog::info("Server stopped");
    return 0;
}

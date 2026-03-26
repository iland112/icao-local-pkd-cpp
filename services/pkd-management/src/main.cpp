/**
 * @file main.cpp
 * @brief ICAO Local PKD Application Entry Point
 * @version v2.33.1
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
#include <sstream>
#include <iomanip>

// OpenLDAP header for checkLdap()
#include <ldap.h>

// Pool stats for /internal/metrics
#include "db_connection_interface.h"
#include "ldap_connection_pool.h"

// Project headers
#include <icao/audit/audit_log.h>
// progress_manager removed — moved to pkd-relay (v2.41.0)

// Infrastructure
#include "infrastructure/service_container.h"
#include "infrastructure/app_config.h"

// Handler includes for route registration
#include "handlers/auth_handler.h"
#include "handlers/upload_handler.h"
#include "handlers/certificate_handler.h"
#include "handlers/misc_handler.h"
#include "handlers/code_master_handler.h"
#include "handlers/api_client_handler.h"
#include "handlers/api_client_request_handler.h"
#include "handlers/csr_handler.h"

// Sync module (moved from pkd-relay)
#include "i_query_executor.h"
#include "sync/handlers/sync_handler.h"
#include "sync/handlers/reconciliation_handler.h"
#include "sync/handlers/notification_handler.h"
#include "sync/services/validation_service.h"
#include "sync/common/config.h"
#include "sync/infrastructure/sync_scheduler.h"
#include "sync/infrastructure/relay_operations.h"
#include "sync/common/notification_manager.h"

// Authentication middleware
#include "middleware/auth_middleware.h"

// Services (for route registration)
#include "services/audit_service.h"
#include "services/validation_service.h"
// icao_sync_service removed — moved to pkd-relay (v2.41.0)

// Global service container (accessed by processing functions and route handlers)
infrastructure::ServiceContainer* g_services = nullptr;

// Sync module globals (moved from pkd-relay)
common::IQueryExecutor* g_queryExecutor = nullptr;  // For Config::loadFromDatabase() compatibility
std::unique_ptr<handlers::SyncHandler> g_syncHandler;
std::unique_ptr<handlers::ReconciliationHandler> g_reconciliationHandler;
std::unique_ptr<handlers::NotificationHandler> g_notificationHandler;

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
    std::cout << "  (C) 2025 SMARTCORE Inc." << std::endl;
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

    // Upload Stats moved to pkd-relay (v2.41.0)

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

    // --- API Client Request Routes (public submit + admin approval) ---
    if (g_services && g_services->apiClientRequestHandler()) {
        g_services->apiClientRequestHandler()->registerRoutes(app);
    }

    // --- CSR Management Routes ---
    if (g_services && g_services->csrHandler()) {
        g_services->csrHandler()->registerRoutes(app);
    }

    // ICAO Routes moved to pkd-relay (v2.41.0)

    // --- Sync Routes (moved from pkd-relay) ---
    if (g_syncHandler) {
        app.registerHandler("/api/sync/status",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleSyncStatus(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/history",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleSyncHistory(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/check",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleSyncCheck(req, std::move(cb));
            }, {drogon::Post});
        app.registerHandler("/api/sync/discrepancies",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleDiscrepancies(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/config",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleSyncConfig(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/config",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleUpdateSyncConfig(req, std::move(cb));
            }, {drogon::Put});
        app.registerHandler("/api/sync/stats",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleSyncStats(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/revalidate",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleRevalidate(req, std::move(cb));
            }, {drogon::Post});
        app.registerHandler("/api/sync/revalidation-history",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleRevalidationHistory(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/trigger-daily",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_syncHandler->handleTriggerDailySync(req, std::move(cb));
            }, {drogon::Post});
        spdlog::info("Sync routes registered (10 endpoints)");
    }

    if (g_reconciliationHandler) {
        app.registerHandler("/api/sync/reconcile",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_reconciliationHandler->handleReconcile(req, std::move(cb));
            }, {drogon::Post});
        app.registerHandler("/api/sync/reconcile/history",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_reconciliationHandler->handleReconciliationHistory(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/reconcile/{id}",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_reconciliationHandler->handleReconciliationDetails(req, std::move(cb));
            }, {drogon::Get});
        app.registerHandler("/api/sync/reconcile/stats",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_reconciliationHandler->handleReconciliationStats(req, std::move(cb));
            }, {drogon::Get});
        spdlog::info("Reconciliation routes registered (4 endpoints)");
    }

    if (g_notificationHandler) {
        app.registerHandler("/api/sync/notifications/stream",
            [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                g_notificationHandler->handleStream(req, std::move(cb));
            }, {drogon::Get});
        spdlog::info("Notification SSE stream registered");
    }

    // --- Internal Metrics (monitoring service only) ---
    app.registerHandler("/internal/metrics",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["service"] = "pkd-management";
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            std::tm tm_buf{};
            localtime_r(&time_t, &tm_buf);
            ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
            result["timestamp"] = ss.str();

            if (g_services && g_services->dbPool()) {
                auto stats = g_services->dbPool()->getStats();
                result["dbPool"]["available"] = static_cast<Json::UInt>(stats.availableConnections);
                result["dbPool"]["total"] = static_cast<Json::UInt>(stats.totalConnections);
                result["dbPool"]["max"] = static_cast<Json::UInt>(stats.maxConnections);
            }
            if (g_services && g_services->ldapPool()) {
                auto stats = g_services->ldapPool()->getStats();
                result["ldapPool"]["available"] = static_cast<Json::UInt>(stats.availableConnections);
                result["ldapPool"]["total"] = static_cast<Json::UInt>(stats.totalConnections);
                result["ldapPool"]["max"] = static_cast<Json::UInt>(stats.maxConnections);
            }
            callback(drogon::HttpResponse::newHttpJsonResponse(result));
        }, {drogon::Get});

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
           .setClientMaxBodySize(static_cast<size_t>(appConfig.maxBodySizeMB) * 1024 * 1024)
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

        // Create sync handler instances (moved from pkd-relay)
        g_queryExecutor = g_services->queryExecutor();
        if (g_services->syncService()) {
            g_syncHandler = std::make_unique<handlers::SyncHandler>(
                g_services->syncService(),
                g_services->syncValidationService(),
                g_services->queryExecutor(),
                g_services->ldapPool(),
                g_services->syncConfig(),
                *g_services->syncScheduler());

            g_reconciliationHandler = std::make_unique<handlers::ReconciliationHandler>(
                g_services->reconciliationService(),
                g_services->queryExecutor(),
                g_services->ldapPool(),
                g_services->syncConfig(),
                g_services->syncStatusRepository());

            g_notificationHandler = std::make_unique<handlers::NotificationHandler>();

            // Start SSE heartbeat for sync notifications
            icao::relay::notification::NotificationManager::getInstance().startHeartbeat();

            // Configure and start sync scheduler
            auto& syncCfg = g_services->syncConfig();
            g_services->syncScheduler()->configure(
                syncCfg.dailySyncEnabled, syncCfg.dailySyncHour,
                syncCfg.dailySyncMinute, syncCfg.revalidateCertsOnSync,
                syncCfg.autoReconcile);

            g_services->syncScheduler()->setSyncCheckFn([&]() {
                auto result = infrastructure::performSyncCheck(
                    g_services->queryExecutor(),
                    g_services->ldapPool(),
                    g_services->syncConfig(),
                    g_services->syncStatusRepository());
            });

            g_services->syncScheduler()->setRevalidateFn([&]() {
                g_services->syncValidationService()->revalidateAll();
            });

            g_services->syncScheduler()->start();
            spdlog::info("Sync module handlers and scheduler initialized");
        }

        // Register routes
        registerRoutes();

        // ICAO Version Check Scheduler moved to pkd-relay (v2.41.0)

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

/**
 * @file main.cpp
 * @brief PKD Relay Service entry point
 * @version v2.33.1
 *
 * Minimal orchestration layer: config, logging, ServiceContainer,
 * handler registration, scheduler, and server startup.
 *
 * @date 2026-02-17 - Refactored from 1,644 lines to ~300 lines
 */

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <iostream>
#include <memory>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <chrono>

// Config
#include "relay/sync/common/config.h"

// Infrastructure
#include "infrastructure/service_container.h"
#include "db_connection_interface.h"
#include "ldap_connection_pool.h"

// Handlers
#include "handlers/health_handler.h"
#include "handlers/notification_handler.h"
#include "handlers/icao_ldap_handler.h"

// Upload module (moved from pkd-management)
#include "upload/upload_services.h"
#include "upload/handlers/upload_handler.h"
#include "upload/handlers/upload_stats_handler.h"
#include "upload/handlers/icao_handler.h"
#include "upload/common/upload_config.h"
#include "upload/common/progress_manager.h"
#include "upload/services/icao_sync_service.h"
#include "upload/repositories/icao_version_repository.h"
#include "upload/infrastructure/http/http_client.h"

// Notification
#include "common/notification_manager.h"

// Factory functions — defined in upload/*.cpp (avoids namespace ambiguity with using namespace icao::relay)
std::unique_ptr<handlers::IcaoHandler> createIcaoHandler(common::IQueryExecutor* qe);
void initLdapStorageService(infrastructure::UploadServiceContainer* sc,
    const std::string& writeHost, int writePort,
    const std::string& bindDn, const std::string& bindPassword,
    const std::string& baseDn,
    const std::string& dataContainer, const std::string& ncDataContainer);

using namespace drogon;
using namespace icao::relay;

// --- Globals ---
Config g_config;
common::IQueryExecutor* g_queryExecutor = nullptr;  // For Config::loadFromDatabase() compatibility

std::unique_ptr<infrastructure::ServiceContainer> g_services;

// Handler instances (must outlive Drogon server)
std::unique_ptr<handlers::HealthHandler> g_healthHandler;
std::unique_ptr<handlers::NotificationHandler> g_notificationHandler;
std::unique_ptr<icao::relay::IcaoLdapHandler> g_icaoLdapHandler;

// Upload module (moved from pkd-management)
std::unique_ptr<infrastructure::UploadServiceContainer> g_uploadSC;
std::unique_ptr<handlers::UploadHandler> g_uploadHandler;
std::unique_ptr<handlers::UploadStatsHandler> g_uploadStatsHandler;
std::unique_ptr<handlers::IcaoHandler> g_icaoHandler;

// --- Logging Setup ---
void setupLogging() {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);

        std::shared_ptr<spdlog::logger> logger;

        try {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "/app/logs/sync-service.log", 1024 * 1024 * 10, 5);
            file_sink->set_level(spdlog::level::debug);

            logger = std::make_shared<spdlog::logger>("sync",
                spdlog::sinks_init_list{console_sink, file_sink});
        } catch (...) {
            logger = std::make_shared<spdlog::logger>("sync", console_sink);
            std::cerr << "Warning: Could not create log file, using console only" << std::endl;
        }

        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    } catch (const std::exception& e) {
        std::cerr << "Logging setup failed: " << e.what() << std::endl;
    }
}

// --- Route Registration ---
void registerRoutes() {
    // Health check
    app().registerHandler("/api/sync/health",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_healthHandler->handle(req, std::move(cb));
        }, {Get});

    // Notification SSE stream (ICAO LDAP sync progress broadcasts)
    // Start SSE heartbeat (30s interval to prevent HTTP/2 idle connection timeout)
    notification::NotificationManager::getInstance().startHeartbeat();

    g_notificationHandler = std::make_unique<handlers::NotificationHandler>();
    app().registerHandler("/api/sync/notifications/stream",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_notificationHandler->handleStream(req, std::move(cb));
        }, {Get});

    // --- Upload Routes (moved from pkd-management) ---
    if (g_uploadHandler) {
        g_uploadHandler->registerRoutes(app());
        spdlog::info("Upload handler routes registered");
    }
    if (g_uploadStatsHandler) {
        g_uploadStatsHandler->registerRoutes(app());
        spdlog::info("Upload stats handler routes registered");
    }
    if (g_icaoHandler) {
        g_icaoHandler->registerRoutes(app());
        spdlog::info("ICAO version handler routes registered");
    }

    // OpenAPI specification endpoint
    app().registerHandler("/api/openapi.yaml",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/openapi.yaml");

            std::string spec = R"(openapi: 3.0.3
info:
  title: PKD Relay Service API
  description: |
    Data Relay Layer for ICAO Local PKD System.
    Handles ICAO portal monitoring, LDIF upload/parsing, and DB-LDAP synchronization.

    ## Changelog
    - v2.1.0 (2026-01-26): MLSC (Master List Signer Certificate) sync support
    - v2.0.5 (2026-01-25): CRL reconciliation support
    - v2.0.0 (2026-01-20): Service reorganization - data relay layer separation
    - v1.4.0 (2026-01-14): Modularized code, Auto Reconcile implementation
    - v1.3.0 (2026-01-13): User-configurable settings UI
    - v1.2.0 (2026-01-07): Daily scheduler only
    - v1.1.0 (2026-01-06): Daily scheduler, certificate re-validation
    - v1.0.0 (2026-01-03): Initial release
  version: 2.1.0
servers:
  - url: /
tags:
  - name: Health
    description: Health check
  - name: Sync
    description: Synchronization operations
  - name: Revalidation
    description: Certificate re-validation operations
  - name: Config
    description: Configuration
paths:
  /api/sync/health:
    get:
      tags: [Health]
      summary: Service health check
      responses:
        '200':
          description: Health status
  /api/sync/status:
    get:
      tags: [Sync]
      summary: Get sync status
      description: Returns DB and LDAP statistics
      responses:
        '200':
          description: Sync status
  /api/sync/check:
    post:
      tags: [Sync]
      summary: Trigger sync check
      responses:
        '200':
          description: Check result
  /api/sync/discrepancies:
    get:
      tags: [Sync]
      summary: Get discrepancies
      parameters:
        - name: type
          in: query
          schema:
            type: string
        - name: limit
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: Discrepancy list
  /api/sync/reconcile:
    post:
      tags: [Sync]
      summary: Reconcile discrepancies
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                mode:
                  type: string
                dryRun:
                  type: boolean
      responses:
        '200':
          description: Reconciliation result
  /api/sync/history:
    get:
      tags: [Sync]
      summary: Get sync history
      parameters:
        - name: limit
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: Sync history
  /api/sync/config:
    get:
      tags: [Config]
      summary: Get configuration
      responses:
        '200':
          description: Current configuration
  /api/sync/revalidate:
    post:
      tags: [Revalidation]
      summary: Trigger certificate re-validation
      description: Re-check all certificates for expiration and update validation status
      responses:
        '200':
          description: Re-validation result
  /api/sync/revalidation-history:
    get:
      tags: [Revalidation]
      summary: Get re-validation history
      parameters:
        - name: limit
          in: query
          schema:
            type: integer
            default: 10
      responses:
        '200':
          description: Re-validation history
  /api/sync/trigger-daily:
    post:
      tags: [Sync]
      summary: Trigger daily sync manually
      description: Manually trigger the daily sync process including certificate re-validation
      responses:
        '200':
          description: Daily sync triggered
)";

            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(spec);
            resp->setContentTypeCode(CT_TEXT_PLAIN);
            resp->addHeader("Content-Type", "application/x-yaml");
            callback(resp);
        }, {Get});

    // --- Internal Metrics (monitoring service only) ---
    app().registerHandler("/internal/metrics",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["service"] = "pkd-relay";
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
            callback(HttpResponse::newHttpJsonResponse(result));
        }, {Get});

    // Swagger UI redirect
    app().registerHandler("/api/docs",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto resp = HttpResponse::newRedirectionResponse("/swagger-ui/index.html");
            callback(resp);
        }, {Get});

    // Enable CORS
    app().registerPostHandlingAdvice([](const HttpRequestPtr&, const HttpResponsePtr& resp) {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    });
}

// --- Main ---
int main() {
    // Load configuration from environment
    g_config.loadFromEnv();

    // Setup logging
    setupLogging();

    // Validate required credentials
    try {
        g_config.validateRequiredCredentials();
    } catch (const std::exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    }

    spdlog::info("=================================================");
    spdlog::info("  ICAO Local PKD - PKD Relay Service v2.13.0");
    spdlog::info("=================================================");
    spdlog::info("Server port: {}", g_config.serverPort);
    spdlog::info("Database: {}:{}/{}", g_config.dbHost, g_config.dbPort, g_config.dbName);
    spdlog::info("LDAP (read): {} (Software Load Balancing)", g_config.ldapReadHosts);
    spdlog::info("LDAP (write): {}:{}", g_config.ldapWriteHost, g_config.ldapWritePort);

    // Initialize ServiceContainer (replaces initializeServices())
    g_services = std::make_unique<infrastructure::ServiceContainer>();
    if (!g_services->initialize(g_config)) {
        spdlog::critical("Service initialization failed");
        return 1;
    }

    // Set global QueryExecutor pointer for Config::loadFromDatabase() compatibility
    g_queryExecutor = g_services->queryExecutor();

    // Load user-configurable settings from database (requires QueryExecutor)
    spdlog::info("Loading configuration from database...");
    g_config.loadFromDatabase();

    // Note: Daily sync/reconciliation/revalidation scheduler moved to pkd-management (v2.41.0)

    // Create handler instances
    g_healthHandler = std::make_unique<handlers::HealthHandler>(
        g_services->queryExecutor());

    // ICAO LDAP Sync Handler (optional)
    if (g_services->icaoLdapSyncService()) {
        g_icaoLdapHandler = std::make_unique<icao::relay::IcaoLdapHandler>(
            g_services->icaoLdapSyncService());
        g_icaoLdapHandler->registerRoutes(app());
        spdlog::info("ICAO LDAP sync endpoints registered");
    }

    // --- Upload Module Initialization (moved from pkd-management) ---
    try {
        g_uploadSC = std::make_unique<infrastructure::UploadServiceContainer>();
        if (g_uploadSC->initialize(g_services->queryExecutor(), g_services->ldapPool(), g_config.ldapBaseDn)) {
            // Set the global pointer for upload code
            g_uploadServices = g_uploadSC.get();

            // Create upload handlers
            handlers::UploadHandler::LdapConfig ldapCfg;
            ldapCfg.writeHost = g_config.ldapWriteHost;
            ldapCfg.writePort = g_config.ldapWritePort;
            ldapCfg.bindDn = g_config.ldapBindDn;
            ldapCfg.bindPassword = g_config.ldapBindPassword;
            ldapCfg.baseDn = g_config.ldapBaseDn;

            g_uploadHandler = std::make_unique<handlers::UploadHandler>(
                g_uploadSC->uploadService(),
                g_uploadSC->validationService(),
                g_uploadSC->ldifStructureService(),
                g_uploadSC->uploadRepository(),
                g_uploadSC->certificateRepository(),
                g_uploadSC->crlRepository(),
                g_uploadSC->validationRepository(),
                g_uploadSC->queryExecutor(),
                ldapCfg);

            g_uploadStatsHandler = std::make_unique<handlers::UploadStatsHandler>(
                g_uploadSC->uploadService(),
                g_uploadSC->uploadRepository(),
                g_uploadSC->certificateRepository(),
                g_uploadSC->validationRepository(),
                g_uploadSC->queryExecutor());

            // Wire upload handler back to UploadServiceContainer (for processLdifAsync delegation)
            g_uploadSC->setUploadHandler(g_uploadHandler.get());

            // Create LdapStorageService with relay config
            initLdapStorageService(g_uploadSC.get(),
                g_config.ldapWriteHost, g_config.ldapWritePort,
                g_config.ldapBindDn, g_config.ldapBindPassword,
                g_config.ldapBaseDn, g_config.ldapDataContainer, g_config.ldapNcDataContainer);
            spdlog::info("LdapStorageService initialized for upload module");

            // ICAO Version Detection handler
            g_icaoHandler = createIcaoHandler(g_uploadSC->queryExecutor());

            spdlog::info("Upload module initialized (upload + stats + ICAO version)");
        }
    } catch (const std::exception& e) {
        spdlog::warn("Upload module initialization failed: {} (non-fatal)", e.what());
    }

    // Register HTTP routes
    registerRoutes();

    // Start HTTP server
    int threadNum = 4;
    if (auto* v = std::getenv("THREAD_NUM")) {
        try { threadNum = std::max(1, std::min(256, std::stoi(v))); } catch (...) { spdlog::warn("Invalid THREAD_NUM '{}', using default {}", v, threadNum); }
    }
    spdlog::info("Starting HTTP server on port {} with {} threads...", g_config.serverPort, threadNum);
    // Set max body size for LDIF/ML uploads (default 1MB is too small)
    int maxBodySizeMB = 100;
    if (auto* v = std::getenv("MAX_BODY_SIZE_MB")) {
        try { maxBodySizeMB = std::max(1, std::min(500, std::stoi(v))); } catch (...) {}
    }
    app().setClientMaxBodySize(static_cast<size_t>(maxBodySizeMB) * 1024 * 1024);
    spdlog::info("Client max body size: {}MB", maxBodySizeMB);

    app().addListener("0.0.0.0", g_config.serverPort)
        .setThreadNum(threadNum)
        .run();

    // Cleanup
    g_healthHandler.reset();
    g_uploadHandler.reset();
    g_uploadStatsHandler.reset();
    g_icaoHandler.reset();
    g_uploadSC.reset();
    g_uploadServices = nullptr;
    g_queryExecutor = nullptr;
    g_services->shutdown();
    g_services.reset();

    return 0;
}

/**
 * @file main.cpp
 * @brief PKD Relay Service entry point
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

// Config
#include "relay/sync/common/config.h"

// Infrastructure
#include "infrastructure/service_container.h"
#include "infrastructure/sync_scheduler.h"
#include "infrastructure/relay_operations.h"
#include "services/validation_service.h"

// Handlers
#include "handlers/health_handler.h"
#include "handlers/sync_handler.h"
#include "handlers/reconciliation_handler.h"

// Reconciliation engine (for scheduler daily sync callback)
#include "relay/sync/reconciliation_engine.h"

using namespace drogon;
using namespace icao::relay;

// --- Globals ---
Config g_config;
common::IQueryExecutor* g_queryExecutor = nullptr;  // For Config::loadFromDatabase() compatibility

std::unique_ptr<infrastructure::ServiceContainer> g_services;
infrastructure::SyncScheduler g_scheduler;

// Handler instances (must outlive Drogon server)
std::unique_ptr<handlers::HealthHandler> g_healthHandler;
std::unique_ptr<handlers::SyncHandler> g_syncHandler;
std::unique_ptr<handlers::ReconciliationHandler> g_reconciliationHandler;

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

    // Sync endpoints
    app().registerHandler("/api/sync/status",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleSyncStatus(req, std::move(cb));
        }, {Get});

    app().registerHandler("/api/sync/history",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleSyncHistory(req, std::move(cb));
        }, {Get});

    app().registerHandler("/api/sync/check",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleSyncCheck(req, std::move(cb));
        }, {Post});

    app().registerHandler("/api/sync/discrepancies",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleDiscrepancies(req, std::move(cb));
        }, {Get});

    app().registerHandler("/api/sync/config",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleSyncConfig(req, std::move(cb));
        }, {Get});

    app().registerHandler("/api/sync/config",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleUpdateSyncConfig(req, std::move(cb));
        }, {Put});

    app().registerHandler("/api/sync/stats",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleSyncStats(req, std::move(cb));
        }, {Get});

    // Reconciliation endpoints
    app().registerHandler("/api/sync/reconcile",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_reconciliationHandler->handleReconcile(req, std::move(cb));
        }, {Post});

    app().registerHandler("/api/sync/reconcile/history",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_reconciliationHandler->handleReconciliationHistory(req, std::move(cb));
        }, {Get});

    app().registerHandler("/api/sync/reconcile/{id}",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_reconciliationHandler->handleReconciliationDetails(req, std::move(cb));
        }, {Get});

    app().registerHandler("/api/sync/reconcile/stats",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_reconciliationHandler->handleReconciliationStats(req, std::move(cb));
        }, {Get});

    // Re-validation endpoints
    app().registerHandler("/api/sync/revalidate",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleRevalidate(req, std::move(cb));
        }, {Post});

    app().registerHandler("/api/sync/revalidation-history",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleRevalidationHistory(req, std::move(cb));
        }, {Get});

    app().registerHandler("/api/sync/trigger-daily",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            g_syncHandler->handleTriggerDailySync(req, std::move(cb));
        }, {Post});

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

    spdlog::info("Daily sync: {} at {}", g_config.dailySyncEnabled ? "enabled" : "disabled",
                 infrastructure::formatScheduledTime(g_config.dailySyncHour, g_config.dailySyncMinute));
    spdlog::info("Certificate re-validation on sync: {}", g_config.revalidateCertsOnSync ? "enabled" : "disabled");
    spdlog::info("Auto reconcile: {}", g_config.autoReconcile ? "enabled" : "disabled");

    // Create handler instances
    g_healthHandler = std::make_unique<handlers::HealthHandler>(
        g_services->queryExecutor());

    g_syncHandler = std::make_unique<handlers::SyncHandler>(
        g_services->syncService(),
        g_services->validationService(),
        g_services->queryExecutor(),
        g_services->ldapPool(),
        g_config,
        g_scheduler);

    g_reconciliationHandler = std::make_unique<handlers::ReconciliationHandler>(
        g_services->reconciliationService(),
        g_services->queryExecutor(),
        g_services->ldapPool(),
        g_config);

    // Register HTTP routes
    registerRoutes();

    // Configure and start scheduler
    g_scheduler.configure(g_config.dailySyncEnabled, g_config.dailySyncHour,
                          g_config.dailySyncMinute, g_config.revalidateCertsOnSync,
                          g_config.autoReconcile);

    // Set scheduler callbacks
    g_scheduler.setSyncCheckFn([&]() {
        infrastructure::performSyncCheck(
            g_services->queryExecutor(),
            g_services->ldapPool(),
            g_config,
            g_services->syncStatusRepository());
    });

    g_scheduler.setRevalidateFn([&]() {
        Json::Value revalResult = g_services->validationService()->revalidateAll();
        if (revalResult.get("success", false).asBool()) {
            spdlog::info("[Daily] Re-validation completed successfully");
        } else {
            spdlog::warn("[Daily] Re-validation had issues: {}",
                        revalResult.get("error", "unknown").asString());
        }
    });

    g_scheduler.setReconcileFn([&](int syncStatusId) {
        // Only reconcile if there are discrepancies
        auto latestResult = infrastructure::performSyncCheck(
            g_services->queryExecutor(),
            g_services->ldapPool(),
            g_config,
            g_services->syncStatusRepository());

        if (latestResult.totalDiscrepancy > 0) {
            spdlog::info("[Daily] Auto reconcile triggered (discrepancy: {})",
                        latestResult.totalDiscrepancy);
            ReconciliationEngine engine(g_config, g_services->ldapPool(),
                                       g_services->queryExecutor());
            ReconciliationResult reconResult = engine.performReconciliation(
                false, "DAILY_SYNC", syncStatusId);

            if (reconResult.success) {
                spdlog::info("[Daily] Auto reconcile completed: {} processed, {} succeeded, {} failed",
                           reconResult.totalProcessed, reconResult.successCount,
                           reconResult.failedCount);
            } else {
                spdlog::error("[Daily] Auto reconcile failed: {}", reconResult.errorMessage);
            }
        } else {
            spdlog::info("[Daily] No discrepancies detected, skipping auto reconcile");
        }
    });

    g_scheduler.start();

    // Start HTTP server
    spdlog::info("Starting HTTP server on port {}...", g_config.serverPort);
    app().addListener("0.0.0.0", g_config.serverPort)
        .setThreadNum(4)
        .run();

    // Cleanup
    g_scheduler.stop();
    g_healthHandler.reset();
    g_syncHandler.reset();
    g_reconciliationHandler.reset();
    g_queryExecutor = nullptr;
    g_services->shutdown();
    g_services.reset();

    return 0;
}

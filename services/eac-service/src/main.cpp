/**
 * @file main.cpp
 * @brief ICAO Local PKD - EAC Service (BSI TR-03110)
 *
 * CVC certificate management microservice for Extended Access Control.
 */

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <memory>
#include <iostream>

#include "infrastructure/app_config.h"
#include "infrastructure/service_container.h"
#include "handlers/eac_upload_handler.h"
#include "handlers/eac_certificate_handler.h"
#include "handlers/eac_statistics_handler.h"

using namespace drogon;

// --- Logging Setup ---
static void setupLogging() {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);

        std::shared_ptr<spdlog::logger> logger;
        try {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "/app/logs/eac-service.log", 1024 * 1024 * 10, 5);
            file_sink->set_level(spdlog::level::debug);
            logger = std::make_shared<spdlog::logger>("eac",
                spdlog::sinks_init_list{console_sink, file_sink});
        } catch (...) {
            logger = std::make_shared<spdlog::logger>("eac", console_sink);
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
static void registerRoutes(
    HttpAppFramework& app,
    eac::handlers::EacUploadHandler& uploadHandler,
    eac::handlers::EacCertificateHandler& certHandler,
    eac::handlers::EacStatisticsHandler& statsHandler) {

    // Health
    app.registerHandler(
        "/api/eac/health",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
            Json::Value resp;
            resp["status"] = "healthy";
            resp["service"] = "eac-service";
            callback(HttpResponse::newHttpJsonResponse(resp));
        },
        {Get});

    // Upload
    app.registerHandler(
        "/api/eac/upload",
        [&uploadHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            uploadHandler.handleUpload(req, std::move(cb));
        },
        {Post});

    app.registerHandler(
        "/api/eac/upload/preview",
        [&uploadHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            uploadHandler.handlePreview(req, std::move(cb));
        },
        {Post});

    // Certificate search & detail
    app.registerHandler(
        "/api/eac/certificates",
        [&certHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            certHandler.handleSearch(req, std::move(cb));
        },
        {Get});

    app.registerHandler(
        "/api/eac/certificates/{id}",
        [&certHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb,
                        const std::string& id) {
            certHandler.handleDetail(req, std::move(cb), id);
        },
        {Get});

    // Delete
    app.registerHandler(
        "/api/eac/certificates/{id}",
        [&certHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb,
                        const std::string& id) {
            certHandler.handleDelete(req, std::move(cb), id);
        },
        {Delete});

    // Trust chain
    app.registerHandler(
        "/api/eac/certificates/{id}/chain",
        [&certHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb,
                        const std::string& id) {
            certHandler.handleChain(req, std::move(cb), id);
        },
        {Get});

    // Statistics & countries
    app.registerHandler(
        "/api/eac/statistics",
        [&statsHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            statsHandler.handleStatistics(req, std::move(cb));
        },
        {Get});

    app.registerHandler(
        "/api/eac/countries",
        [&statsHandler](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
            statsHandler.handleCountries(req, std::move(cb));
        },
        {Get});
}

// --- Main ---
int main() {
    auto config = eac::AppConfig::fromEnv();
    setupLogging();

    spdlog::info("===========================================");
    spdlog::info("  ICAO Local PKD - EAC Service v1.0.0");
    spdlog::info("===========================================");
    spdlog::info("Database: {} ({}:{})", config.dbType, config.dbHost, config.dbPort);
    spdlog::info("Server port: {}", config.serverPort);
    spdlog::info("Threads: {}", config.threadNum);

    // Initialize DI container
    eac::infrastructure::ServiceContainer services;
    if (!services.initialize(config)) {
        spdlog::critical("Failed to initialize ServiceContainer — exiting");
        return 1;
    }

    // Create handlers
    eac::handlers::EacUploadHandler uploadHandler(&services);
    eac::handlers::EacCertificateHandler certHandler(&services);
    eac::handlers::EacStatisticsHandler statsHandler(&services);

    // Register routes
    registerRoutes(app(), uploadHandler, certHandler, statsHandler);

    // CORS
    app().registerPostHandlingAdvice([](const HttpRequestPtr&, const HttpResponsePtr& resp) {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    });

    // Start server
    spdlog::info("Starting HTTP server on port {}...", config.serverPort);
    app().addListener("0.0.0.0", config.serverPort)
        .setThreadNum(config.threadNum)
        .run();

    spdlog::info("Server stopped");
    services.shutdown();

    return 0;
}

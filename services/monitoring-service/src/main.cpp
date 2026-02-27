/**
 * @file main.cpp
 * @brief ICAO Local PKD Monitoring Service
 *
 * System resource and service health monitoring (DB-independent).
 */

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <curl/curl.h>

#include <memory>
#include <iostream>
#include <cstdlib>

#include "handlers/monitoring_handler.h"

using namespace drogon;

// --- Logging Setup ---
void setupLogging() {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);

        std::shared_ptr<spdlog::logger> logger;

        try {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "/app/logs/monitoring-service.log", 1024 * 1024 * 10, 5);
            file_sink->set_level(spdlog::level::debug);

            logger = std::make_shared<spdlog::logger>("monitoring",
                spdlog::sinks_init_list{console_sink, file_sink});
        } catch (...) {
            logger = std::make_shared<spdlog::logger>("monitoring", console_sink);
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

// --- Main ---
int main() {
    // Initialize CURL library (must be done before any threads)
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Load configuration
    handlers::MonitoringConfig config;
    config.loadFromEnv();

    // Setup logging
    setupLogging();

    spdlog::info("===========================================");
    spdlog::info("  ICAO Local PKD - Monitoring Service v1.1.0");
    spdlog::info("===========================================");
    spdlog::info("Server port: {}", config.serverPort);
    spdlog::info("Mode: On-demand metrics (no database dependency)");

    // Create handler and register routes
    handlers::MonitoringHandler monitoringHandler(&config);
    monitoringHandler.registerRoutes(app());

    // Enable CORS
    app().registerPostHandlingAdvice([](const HttpRequestPtr&, const HttpResponsePtr& resp) {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    });

    // Start server
    spdlog::info("Starting HTTP server on port {}...", config.serverPort);

    int threadNum = 4;
    if (auto* v = std::getenv("THREAD_NUM")) threadNum = std::stoi(v);
    spdlog::info("Using {} threads", threadNum);
    app().addListener("0.0.0.0", config.serverPort)
        .setThreadNum(threadNum)
        .run();

    spdlog::info("Server stopped");

    // Cleanup CURL library
    curl_global_cleanup();

    return 0;
}

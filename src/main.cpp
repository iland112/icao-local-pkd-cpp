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
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <iostream>
#include <memory>

namespace {

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
            result["timestamp"] = drogon::trantor::Date::now().toFormattedString(false);

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
            result["endpoints"]["statistics"] = "/api/statistics";

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

    spdlog::info("Starting ICAO Local PKD Application...");

    try {
        auto& app = drogon::app();

        // Load configuration
        // app.loadConfigFile("config/config.json");

        // Server settings
        app.setLogPath("logs")
           .setLogLevel(trantor::Logger::kInfo)
           .addListener("0.0.0.0", 8081)
           .setThreadNum(std::thread::hardware_concurrency())
           .enableGzip(true)
           .setClientMaxBodySize(100 * 1024 * 1024)  // 100MB max upload
           .setUploadPath("./uploads")
           .setDocumentRoot("./static");

        // Enable CORS for React.js frontend
        app.registerPreSendingAdvice([](const drogon::HttpRequestPtr& req,
                                         const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
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

        spdlog::info("Server starting on http://0.0.0.0:8081");
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

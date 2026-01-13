// =============================================================================
// ICAO Local PKD - Monitoring Service
// =============================================================================
// Version: 1.0.0
// Description: System resource and service health monitoring
// =============================================================================
// Changelog:
//   v1.0.0 (2026-01-13): Initial release
// =============================================================================

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <json/json.h>
#include <libpq-fe.h>
#include <curl/curl.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>

using namespace drogon;

// =============================================================================
// Global Configuration
// =============================================================================
struct Config {
    // Server
    int serverPort = 8084;

    // Database
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "pkd";
    std::string dbUser = "pkd";
    std::string dbPassword = "pkd123";

    // Monitoring settings
    int systemMetricsInterval = 5;     // seconds
    int serviceHealthInterval = 30;    // seconds
    int logAnalysisInterval = 60;      // seconds
    int metricsRetentionDays = 7;

    // Service endpoints
    std::map<std::string, std::string> serviceEndpoints = {
        {"pkd-management", "http://pkd-management:8081/api/health"},
        {"pa-service", "http://pa-service:8082/api/pa/health"},
        {"sync-service", "http://sync-service:8083/api/sync/health"},
    };

    void loadFromEnv() {
        if (auto e = std::getenv("SERVER_PORT")) serverPort = std::stoi(e);
        if (auto e = std::getenv("DB_HOST")) dbHost = e;
        if (auto e = std::getenv("DB_PORT")) dbPort = std::stoi(e);
        if (auto e = std::getenv("DB_NAME")) dbName = e;
        if (auto e = std::getenv("DB_USER")) dbUser = e;
        if (auto e = std::getenv("DB_PASSWORD")) dbPassword = e;
        if (auto e = std::getenv("SYSTEM_METRICS_INTERVAL")) systemMetricsInterval = std::stoi(e);
        if (auto e = std::getenv("SERVICE_HEALTH_INTERVAL")) serviceHealthInterval = std::stoi(e);
        if (auto e = std::getenv("LOG_ANALYSIS_INTERVAL")) logAnalysisInterval = std::stoi(e);
        if (auto e = std::getenv("METRICS_RETENTION_DAYS")) metricsRetentionDays = std::stoi(e);
    }
};

Config g_config;

// =============================================================================
// System Metrics Structures
// =============================================================================
struct CpuMetrics {
    float usagePercent = 0.0f;
    float load1min = 0.0f;
    float load5min = 0.0f;
    float load15min = 0.0f;
};

struct MemoryMetrics {
    uint64_t totalMb = 0;
    uint64_t usedMb = 0;
    uint64_t freeMb = 0;
    float usagePercent = 0.0f;
};

struct DiskMetrics {
    uint64_t totalGb = 0;
    uint64_t usedGb = 0;
    uint64_t freeGb = 0;
    float usagePercent = 0.0f;
};

struct NetworkMetrics {
    uint64_t bytesSent = 0;
    uint64_t bytesRecv = 0;
    uint64_t packetsSent = 0;
    uint64_t packetsRecv = 0;
};

struct SystemMetrics {
    std::chrono::system_clock::time_point timestamp;
    CpuMetrics cpu;
    MemoryMetrics memory;
    DiskMetrics disk;
    NetworkMetrics network;
};

// =============================================================================
// Service Health Structures
// =============================================================================
enum class ServiceStatus {
    UP,
    DEGRADED,
    DOWN,
    UNKNOWN
};

struct ServiceHealth {
    std::string serviceName;
    ServiceStatus status;
    int responseTimeMs;
    std::string errorMessage;
    std::chrono::system_clock::time_point checkedAt;
};

// =============================================================================
// PostgreSQL Connection
// =============================================================================
class PgConnection {
public:
    PgConnection() : conn_(nullptr) {}
    ~PgConnection() { disconnect(); }

    bool connect() {
        std::string connStr = "host=" + g_config.dbHost +
                              " port=" + std::to_string(g_config.dbPort) +
                              " dbname=" + g_config.dbName +
                              " user=" + g_config.dbUser +
                              " password=" + g_config.dbPassword;

        conn_ = PQconnectdb(connStr.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            spdlog::error("Database connection failed: {}", PQerrorMessage(conn_));
            return false;
        }
        return true;
    }

    void disconnect() {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    PGconn* get() { return conn_; }
    bool isConnected() const { return conn_ && PQstatus(conn_) == CONNECTION_OK; }

private:
    PGconn* conn_;
};

// =============================================================================
// System Metrics Collector
// =============================================================================
class SystemMetricsCollector {
public:
    SystemMetrics collect() {
        SystemMetrics metrics;
        metrics.timestamp = std::chrono::system_clock::now();
        metrics.cpu = collectCpuMetrics();
        metrics.memory = collectMemoryMetrics();
        metrics.disk = collectDiskMetrics();
        metrics.network = collectNetworkMetrics();
        return metrics;
    }

private:
    CpuMetrics collectCpuMetrics() {
        CpuMetrics metrics;
        // TODO: Parse /proc/stat for CPU usage
        // TODO: Parse /proc/loadavg for load averages
        metrics.usagePercent = 0.0f;  // Placeholder
        metrics.load1min = 0.0f;
        metrics.load5min = 0.0f;
        metrics.load15min = 0.0f;
        return metrics;
    }

    MemoryMetrics collectMemoryMetrics() {
        MemoryMetrics metrics;
        // TODO: Parse /proc/meminfo
        std::ifstream meminfo("/proc/meminfo");
        if (!meminfo.is_open()) {
            return metrics;
        }

        std::string line;
        while (std::getline(meminfo, line)) {
            std::istringstream iss(line);
            std::string key;
            uint64_t value;
            std::string unit;

            if (iss >> key >> value >> unit) {
                if (key == "MemTotal:") {
                    metrics.totalMb = value / 1024;
                } else if (key == "MemFree:") {
                    metrics.freeMb = value / 1024;
                } else if (key == "MemAvailable:") {
                    metrics.usedMb = metrics.totalMb - (value / 1024);
                }
            }
        }

        if (metrics.totalMb > 0) {
            metrics.usagePercent = (float)metrics.usedMb / metrics.totalMb * 100.0f;
        }

        return metrics;
    }

    DiskMetrics collectDiskMetrics() {
        DiskMetrics metrics;
        // TODO: Parse /proc/diskstats or use statvfs()
        metrics.totalGb = 0;
        metrics.usedGb = 0;
        metrics.freeGb = 0;
        metrics.usagePercent = 0.0f;
        return metrics;
    }

    NetworkMetrics collectNetworkMetrics() {
        NetworkMetrics metrics;
        // TODO: Parse /proc/net/dev
        metrics.bytesSent = 0;
        metrics.bytesRecv = 0;
        metrics.packetsSent = 0;
        metrics.packetsRecv = 0;
        return metrics;
    }
};

// =============================================================================
// Service Health Checker
// =============================================================================
class ServiceHealthChecker {
public:
    ServiceHealthChecker() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~ServiceHealthChecker() {
        curl_global_cleanup();
    }

    ServiceHealth checkService(const std::string& name, const std::string& url) {
        ServiceHealth health;
        health.serviceName = name;
        health.checkedAt = std::chrono::system_clock::now();

        CURL* curl = curl_easy_init();
        if (!curl) {
            health.status = ServiceStatus::UNKNOWN;
            health.errorMessage = "Failed to initialize CURL";
            return health;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);

        CURLcode res = curl_easy_perform(curl);
        long responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        auto endTime = std::chrono::high_resolution_clock::now();
        health.responseTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        if (res == CURLE_OK && responseCode == 200) {
            health.status = ServiceStatus::UP;
        } else if (res == CURLE_OK && responseCode >= 500) {
            health.status = ServiceStatus::DEGRADED;
            health.errorMessage = "HTTP " + std::to_string(responseCode);
        } else {
            health.status = ServiceStatus::DOWN;
            health.errorMessage = curl_easy_strerror(res);
        }

        curl_easy_cleanup(curl);
        return health;
    }
};

// =============================================================================
// HTTP Handlers
// =============================================================================

// Health check
void handleHealth(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value response;
    response["status"] = "UP";
    response["service"] = "monitoring-service";
    response["version"] = "1.0.0";
    response["timestamp"] = trantor::Date::now().toFormattedString(false);

    // Check DB connection
    PgConnection conn;
    if (conn.connect()) {
        response["database"] = "UP";
    } else {
        response["database"] = "DOWN";
        response["status"] = "DEGRADED";
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

// Get system overview
void handleSystemOverview(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    SystemMetricsCollector collector;
    SystemMetrics metrics = collector.collect();

    Json::Value response;

    // CPU
    response["cpu"]["usagePercent"] = metrics.cpu.usagePercent;
    response["cpu"]["load1min"] = metrics.cpu.load1min;
    response["cpu"]["load5min"] = metrics.cpu.load5min;
    response["cpu"]["load15min"] = metrics.cpu.load15min;

    // Memory
    response["memory"]["totalMb"] = (Json::Value::UInt64)metrics.memory.totalMb;
    response["memory"]["usedMb"] = (Json::Value::UInt64)metrics.memory.usedMb;
    response["memory"]["freeMb"] = (Json::Value::UInt64)metrics.memory.freeMb;
    response["memory"]["usagePercent"] = metrics.memory.usagePercent;

    // Disk
    response["disk"]["totalGb"] = (Json::Value::UInt64)metrics.disk.totalGb;
    response["disk"]["usedGb"] = (Json::Value::UInt64)metrics.disk.usedGb;
    response["disk"]["freeGb"] = (Json::Value::UInt64)metrics.disk.freeGb;
    response["disk"]["usagePercent"] = metrics.disk.usagePercent;

    // Network
    response["network"]["bytesSent"] = (Json::Value::UInt64)metrics.network.bytesSent;
    response["network"]["bytesRecv"] = (Json::Value::UInt64)metrics.network.bytesRecv;
    response["network"]["packetsSent"] = (Json::Value::UInt64)metrics.network.packetsSent;
    response["network"]["packetsRecv"] = (Json::Value::UInt64)metrics.network.packetsRecv;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

// Get all services health
void handleServicesHealth(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    ServiceHealthChecker checker;
    Json::Value services(Json::arrayValue);

    for (const auto& [name, url] : g_config.serviceEndpoints) {
        ServiceHealth health = checker.checkService(name, url);

        Json::Value serviceJson;
        serviceJson["name"] = health.serviceName;

        switch (health.status) {
            case ServiceStatus::UP:
                serviceJson["status"] = "UP";
                break;
            case ServiceStatus::DEGRADED:
                serviceJson["status"] = "DEGRADED";
                break;
            case ServiceStatus::DOWN:
                serviceJson["status"] = "DOWN";
                break;
            default:
                serviceJson["status"] = "UNKNOWN";
        }

        serviceJson["responseTimeMs"] = health.responseTimeMs;
        if (!health.errorMessage.empty()) {
            serviceJson["errorMessage"] = health.errorMessage;
        }

        auto timeT = std::chrono::system_clock::to_time_t(health.checkedAt);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&timeT), "%Y-%m-%d %H:%M:%S");
        serviceJson["checkedAt"] = oss.str();

        services.append(serviceJson);
    }

    Json::Value response;
    response["services"] = services;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

// =============================================================================
// Logging Setup
// =============================================================================
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

// =============================================================================
// Main
// =============================================================================
int main() {
    // Load configuration
    g_config.loadFromEnv();

    // Setup logging
    setupLogging();

    spdlog::info("===========================================");
    spdlog::info("  ICAO Local PKD - Monitoring Service v1.0.0");
    spdlog::info("===========================================");
    spdlog::info("Server port: {}", g_config.serverPort);
    spdlog::info("Database: {}:{}/{}", g_config.dbHost, g_config.dbPort, g_config.dbName);
    spdlog::info("System metrics interval: {}s", g_config.systemMetricsInterval);
    spdlog::info("Service health interval: {}s", g_config.serviceHealthInterval);

    // Register HTTP handlers
    app().registerHandler("/api/monitoring/health",
        &handleHealth, {Get});
    app().registerHandler("/api/monitoring/system/overview",
        &handleSystemOverview, {Get});
    app().registerHandler("/api/monitoring/services",
        &handleServicesHealth, {Get});

    // Enable CORS
    app().registerPostHandlingAdvice([](const HttpRequestPtr&, const HttpResponsePtr& resp) {
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    });

    // Start server
    spdlog::info("Starting HTTP server on port {}...", g_config.serverPort);
    app().addListener("0.0.0.0", g_config.serverPort)
        .setThreadNum(2)
        .run();

    return 0;
}

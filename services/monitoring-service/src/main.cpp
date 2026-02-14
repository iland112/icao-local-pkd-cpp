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
#include <json/json.h>
#include <curl/curl.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/statvfs.h>

using namespace drogon;

// --- Global Configuration ---
struct Config {
    // Server
    int serverPort = 8084;

    // Monitoring settings
    int systemMetricsInterval = 5;     // seconds
    int serviceHealthInterval = 30;    // seconds

    // Service endpoints
    std::map<std::string, std::string> serviceEndpoints = {
        {"pkd-management", "http://pkd-management:8081/api/health"},
        {"pa-service", "http://pa-service:8082/api/pa/health"},
        {"pkd-relay", "http://pkd-relay:8083/api/sync/health"},
    };

    void loadFromEnv() {
        if (auto e = std::getenv("SERVER_PORT")) serverPort = std::stoi(e);
        if (auto e = std::getenv("SYSTEM_METRICS_INTERVAL")) systemMetricsInterval = std::stoi(e);
        if (auto e = std::getenv("SERVICE_HEALTH_INTERVAL")) serviceHealthInterval = std::stoi(e);

        // Load service endpoints
        if (auto e = std::getenv("SERVICE_PKD_MANAGEMENT")) serviceEndpoints["pkd-management"] = e;
        if (auto e = std::getenv("SERVICE_PA_SERVICE")) serviceEndpoints["pa-service"] = e;
        if (auto e = std::getenv("SERVICE_SYNC_SERVICE")) serviceEndpoints["pkd-relay"] = e;
    }
};

Config g_config;

// --- System Metrics Structures ---
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

// --- Service Health Structures ---
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

// --- System Metrics Collector ---
class SystemMetricsCollector {
public:
    SystemMetricsCollector() {
        // Initialize previous CPU stats
        updateCpuStats();
    }

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
    // CPU stat tracking
    struct CpuStat {
        uint64_t user = 0;
        uint64_t nice = 0;
        uint64_t system = 0;
        uint64_t idle = 0;
        uint64_t iowait = 0;
        uint64_t irq = 0;
        uint64_t softirq = 0;
        uint64_t steal = 0;

        uint64_t total() const {
            return user + nice + system + idle + iowait + irq + softirq + steal;
        }

        uint64_t active() const {
            return user + nice + system + irq + softirq + steal;
        }
    };

    CpuStat prevCpuStat_;
    std::mutex cpuMutex_;

    void updateCpuStats() {
        std::ifstream statFile("/proc/stat");
        if (!statFile.is_open()) {
            return;
        }

        std::string line;
        if (std::getline(statFile, line)) {
            std::istringstream iss(line);
            std::string cpu;
            CpuStat stat;

            iss >> cpu >> stat.user >> stat.nice >> stat.system >> stat.idle
                >> stat.iowait >> stat.irq >> stat.softirq >> stat.steal;

            std::lock_guard<std::mutex> lock(cpuMutex_);
            prevCpuStat_ = stat;
        }
    }

    CpuMetrics collectCpuMetrics() {
        CpuMetrics metrics;

        try {
            // Parse /proc/stat for CPU usage
            std::ifstream statFile("/proc/stat");
            if (statFile.is_open()) {
                std::string line;
                if (std::getline(statFile, line)) {
                    std::istringstream iss(line);
                    std::string cpu;
                    CpuStat currentStat;

                    iss >> cpu >> currentStat.user >> currentStat.nice >> currentStat.system
                        >> currentStat.idle >> currentStat.iowait >> currentStat.irq
                        >> currentStat.softirq >> currentStat.steal;

                    // Calculate CPU usage percentage
                    std::lock_guard<std::mutex> lock(cpuMutex_);

                    uint64_t totalDiff = currentStat.total() - prevCpuStat_.total();
                    uint64_t activeDiff = currentStat.active() - prevCpuStat_.active();

                    if (totalDiff > 0) {
                        metrics.usagePercent = (float)activeDiff / totalDiff * 100.0f;
                    }

                    prevCpuStat_ = currentStat;
                }
            }

            // Parse /proc/loadavg for load averages
            std::ifstream loadavgFile("/proc/loadavg");
            if (loadavgFile.is_open()) {
                loadavgFile >> metrics.load1min >> metrics.load5min >> metrics.load15min;
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to collect CPU metrics: {}", e.what());
        }

        return metrics;
    }

    MemoryMetrics collectMemoryMetrics() {
        MemoryMetrics metrics;

        try {
            std::ifstream meminfo("/proc/meminfo");
            if (!meminfo.is_open()) {
                return metrics;
            }

            std::string line;
            while (std::getline(meminfo, line)) {
                std::istringstream iss(line);
                std::string key;
                uint64_t value = 0;
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
        } catch (const std::exception& e) {
            spdlog::warn("Failed to collect memory metrics: {}", e.what());
        }

        return metrics;
    }

    DiskMetrics collectDiskMetrics() {
        DiskMetrics metrics;

        try {
            // Use statvfs to get disk usage for root partition
            struct statvfs stat;
            if (statvfs("/", &stat) == 0) {
                uint64_t blockSize = stat.f_frsize;
                uint64_t totalBlocks = stat.f_blocks;
                uint64_t freeBlocks = stat.f_bfree;
                uint64_t availBlocks = stat.f_bavail;

                uint64_t totalBytes = totalBlocks * blockSize;
                uint64_t freeBytes = freeBlocks * blockSize;
                uint64_t usedBytes = totalBytes - freeBytes;

                metrics.totalGb = totalBytes / (1024 * 1024 * 1024);
                metrics.freeGb = freeBytes / (1024 * 1024 * 1024);
                metrics.usedGb = usedBytes / (1024 * 1024 * 1024);

                if (totalBytes > 0) {
                    metrics.usagePercent = (float)usedBytes / totalBytes * 100.0f;
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to collect disk metrics: {}", e.what());
        }

        return metrics;
    }

    NetworkMetrics collectNetworkMetrics() {
        NetworkMetrics metrics;

        try {
            // Parse /proc/net/dev
            std::ifstream netFile("/proc/net/dev");
            if (!netFile.is_open()) {
                return metrics;
            }

            std::string line;
            // Skip first two header lines
            if (!std::getline(netFile, line)) return metrics;
            if (!std::getline(netFile, line)) return metrics;

            while (std::getline(netFile, line)) {
                try {
                    // Remove leading/trailing whitespace
                    size_t start = line.find_first_not_of(" \t");
                    if (start == std::string::npos) continue;

                    std::istringstream iss(line.substr(start));
                    std::string interface;
                    uint64_t bytesRecv = 0, packetsRecv = 0, errsRecv = 0, dropRecv = 0;
                    uint64_t bytesSent = 0, packetsSent = 0, errsSent = 0, dropSent = 0;
                    uint64_t dummy = 0;

                    // Parse interface name (ends with ':')
                    if (!std::getline(iss, interface, ':')) continue;

                    // Skip loopback interface
                    if (interface == "lo") continue;

                    // Parse receive stats
                    if (!(iss >> bytesRecv >> packetsRecv >> errsRecv >> dropRecv)) continue;

                    // Skip 4 fields (fifo, frame, compressed, multicast)
                    for (int i = 0; i < 4; i++) {
                        if (!(iss >> dummy)) break;
                    }

                    // Parse transmit stats
                    if (!(iss >> bytesSent >> packetsSent >> errsSent >> dropSent)) continue;

                    // Aggregate all non-loopback interfaces
                    metrics.bytesRecv += bytesRecv;
                    metrics.packetsRecv += packetsRecv;
                    metrics.bytesSent += bytesSent;
                    metrics.packetsSent += packetsSent;
                } catch (const std::exception& e) {
                    // Skip this interface on parse error
                    continue;
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to collect network metrics: {}", e.what());
        }

        return metrics;
    }
};

// --- Service Health Checker ---

// No-op write callback to discard curl response body
static size_t discardWriteCallback(void* /*contents*/, size_t size, size_t nmemb, void* /*userp*/) {
    return size * nmemb;
}

class ServiceHealthChecker {
public:
    ServiceHealthChecker() = default;
    ~ServiceHealthChecker() = default;

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
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardWriteCallback);
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

// --- HTTP Handlers ---

// Health check
void handleHealth(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value response;
    response["status"] = "UP";
    response["service"] = "monitoring-service";
    response["version"] = "1.1.0";
    response["timestamp"] = trantor::Date::now().toFormattedString(false);

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

// Get system overview
void handleSystemOverview(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& callback) {
    SystemMetricsCollector collector;
    SystemMetrics metrics = collector.collect();

    Json::Value response;

    // Timestamp (ISO 8601 format)
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time_t);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm);
    response["timestamp"] = timestamp;

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
    Json::Value services(Json::arrayValue);

    try {
        ServiceHealthChecker checker;

        for (const auto& [name, url] : g_config.serviceEndpoints) {
            try {
                ServiceHealth health = checker.checkService(name, url);

                Json::Value serviceJson;
                serviceJson["serviceName"] = health.serviceName;

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
            } catch (const std::exception& e) {
                spdlog::error("Failed to check service {}: {}", name, e.what());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Service health check failed: {}", e.what());
    }

    auto resp = HttpResponse::newHttpJsonResponse(services);
    callback(resp);
}

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
    g_config.loadFromEnv();

    // Setup logging
    setupLogging();

    spdlog::info("===========================================");
    spdlog::info("  ICAO Local PKD - Monitoring Service v1.1.0");
    spdlog::info("===========================================");
    spdlog::info("Server port: {}", g_config.serverPort);
    spdlog::info("Mode: On-demand metrics (no database dependency)");

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
        .setThreadNum(4)
        .run();

    spdlog::info("Server stopped");

    // Cleanup CURL library
    curl_global_cleanup();

    return 0;
}

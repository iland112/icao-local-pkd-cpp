#pragma once

#include <drogon/HttpController.h>
#include <json/json.h>
#include <curl/curl.h>

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/statvfs.h>

namespace handlers {

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
    SystemMetricsCollector();

    SystemMetrics collect();

private:
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

    void updateCpuStats();
    CpuMetrics collectCpuMetrics();
    MemoryMetrics collectMemoryMetrics();
    DiskMetrics collectDiskMetrics();
    NetworkMetrics collectNetworkMetrics();
};

// --- Service Health Checker ---

class ServiceHealthChecker {
public:
    ServiceHealthChecker() = default;
    ~ServiceHealthChecker() = default;

    ServiceHealth checkService(const std::string& name, const std::string& url);
};

// --- Global Configuration ---

struct MonitoringConfig {
    int serverPort = 8084;
    int systemMetricsInterval = 5;     // seconds
    int serviceHealthInterval = 30;    // seconds

    std::map<std::string, std::string> serviceEndpoints = {
        {"pkd-management", "http://pkd-management:8081/api/health"},
        {"pa-service", "http://pa-service:8082/api/pa/health"},
        {"pkd-relay", "http://pkd-relay:8083/api/sync/health"},
    };

    void loadFromEnv();
};

/**
 * @brief Monitoring endpoints handler
 *
 * Provides system monitoring and service health API endpoints:
 * - GET /api/monitoring/health - Health check
 * - GET /api/monitoring/system/overview - System metrics (CPU, memory, disk, network)
 * - GET /api/monitoring/services - All services health status
 *
 * DB-independent: no database dependency required.
 */
class MonitoringHandler {
public:
    /**
     * @brief Construct MonitoringHandler
     *
     * @param config Monitoring configuration (non-owning pointer)
     */
    explicit MonitoringHandler(MonitoringConfig* config);

    /**
     * @brief Register monitoring routes
     *
     * Registers all monitoring endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    MonitoringConfig* config_;

    /**
     * @brief GET /api/monitoring/health
     *
     * Response:
     * {
     *   "status": "UP",
     *   "service": "monitoring-service",
     *   "version": "1.1.0",
     *   "timestamp": "2026-02-17T12:00:00"
     * }
     */
    void handleHealth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/monitoring/system/overview
     *
     * Returns current system metrics including CPU, memory, disk, and network.
     *
     * Response:
     * {
     *   "timestamp": "2026-02-17T12:00:00",
     *   "cpu": { "usagePercent": 25.3, "load1min": 0.5, ... },
     *   "memory": { "totalMb": 8192, "usedMb": 4096, ... },
     *   "disk": { "totalGb": 100, "usedGb": 50, ... },
     *   "network": { "bytesSent": 1234567, "bytesRecv": 7654321, ... }
     * }
     */
    void handleSystemOverview(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/monitoring/services
     *
     * Checks health of all configured backend services.
     *
     * Response:
     * [
     *   {
     *     "serviceName": "pkd-management",
     *     "status": "UP",
     *     "responseTimeMs": 12,
     *     "checkedAt": "2026-02-17 12:00:00"
     *   },
     *   ...
     * ]
     */
    void handleServicesHealth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace handlers

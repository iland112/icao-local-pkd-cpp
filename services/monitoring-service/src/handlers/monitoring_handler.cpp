/** @file monitoring_handler.cpp
 *  @brief MonitoringHandler implementation
 */

#include "monitoring_handler.h"
#include "../collectors/metrics_collector.h"
#include <spdlog/spdlog.h>

namespace handlers {

// --- No-op write callback to discard curl response body ---
static size_t discardWriteCallback(void* /*contents*/, size_t size, size_t nmemb, void* /*userp*/) {
    return size * nmemb;
}

// --- MonitoringConfig ---

void MonitoringConfig::loadFromEnv() {
    if (auto e = std::getenv("SERVER_PORT")) serverPort = std::stoi(e);
    if (auto e = std::getenv("SYSTEM_METRICS_INTERVAL")) systemMetricsInterval = std::stoi(e);
    if (auto e = std::getenv("SERVICE_HEALTH_INTERVAL")) serviceHealthInterval = std::stoi(e);

    // Load service endpoints
    if (auto e = std::getenv("SERVICE_PKD_MANAGEMENT")) serviceEndpoints["pkd-management"] = e;
    if (auto e = std::getenv("SERVICE_PA_SERVICE")) serviceEndpoints["pa-service"] = e;
    if (auto e = std::getenv("SERVICE_SYNC_SERVICE")) serviceEndpoints["pkd-relay"] = e;
}

// --- SystemMetricsCollector ---

SystemMetricsCollector::SystemMetricsCollector() {
    // Initialize previous CPU stats
    updateCpuStats();
}

SystemMetrics SystemMetricsCollector::collect() {
    SystemMetrics metrics;
    metrics.timestamp = std::chrono::system_clock::now();
    metrics.cpu = collectCpuMetrics();
    metrics.memory = collectMemoryMetrics();
    metrics.disk = collectDiskMetrics();
    metrics.network = collectNetworkMetrics();
    return metrics;
}

void SystemMetricsCollector::updateCpuStats() {
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

CpuMetrics SystemMetricsCollector::collectCpuMetrics() {
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

MemoryMetrics SystemMetricsCollector::collectMemoryMetrics() {
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

DiskMetrics SystemMetricsCollector::collectDiskMetrics() {
    DiskMetrics metrics;

    try {
        // Use statvfs to get disk usage for root partition
        struct statvfs stat;
        if (statvfs("/", &stat) == 0) {
            uint64_t blockSize = stat.f_frsize;
            uint64_t totalBlocks = stat.f_blocks;
            uint64_t freeBlocks = stat.f_bfree;

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

NetworkMetrics SystemMetricsCollector::collectNetworkMetrics() {
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

// --- ServiceHealthChecker ---

ServiceHealth ServiceHealthChecker::checkService(const std::string& name, const std::string& url) {
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

// --- MonitoringHandler ---

MonitoringHandler::MonitoringHandler(MonitoringConfig* config, MetricsCollector* collector)
    : config_(config), collector_(collector) {

    if (!config_) {
        throw std::invalid_argument("MonitoringHandler: config cannot be nullptr");
    }

    spdlog::info("[MonitoringHandler] Initialized (collector={})", collector_ ? "yes" : "no");
}

void MonitoringHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // GET /api/monitoring/health
    app.registerHandler(
        "/api/monitoring/health",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleHealth(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/monitoring/system/overview
    app.registerHandler(
        "/api/monitoring/system/overview",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleSystemOverview(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/monitoring/services
    app.registerHandler(
        "/api/monitoring/services",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleServicesHealth(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/monitoring/load
    app.registerHandler(
        "/api/monitoring/load",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLoadSnapshot(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/monitoring/load/history
    app.registerHandler(
        "/api/monitoring/load/history",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLoadHistory(req, std::move(callback));
        },
        {drogon::Get}
    );

    spdlog::info("[MonitoringHandler] Routes registered: "
                 "/api/monitoring/health, "
                 "/api/monitoring/system/overview, "
                 "/api/monitoring/services, "
                 "/api/monitoring/load, "
                 "/api/monitoring/load/history");
}

// --- Handler Implementations ---

void MonitoringHandler::handleHealth(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value response;
    response["status"] = "UP";
    response["service"] = "monitoring-service";
    response["version"] = "1.1.0";
    response["timestamp"] = trantor::Date::now().toFormattedString(false);

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

void MonitoringHandler::handleSystemOverview(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

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

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

void MonitoringHandler::handleServicesHealth(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    Json::Value services(Json::arrayValue);

    try {
        ServiceHealthChecker checker;

        for (const auto& [name, url] : config_->serviceEndpoints) {
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

    auto resp = drogon::HttpResponse::newHttpJsonResponse(services);
    callback(resp);
}

// --- Load Snapshot ---

static std::string formatTimestamp(std::chrono::system_clock::time_point tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&time_t));
    return buf;
}

void MonitoringHandler::handleLoadSnapshot(
    const drogon::HttpRequestPtr&,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    if (!collector_ || !collector_->hasData()) {
        Json::Value err;
        err["error"] = "No metrics data collected yet";
        err["message"] = "Metrics collection starts after service initialization. Please retry in 10 seconds.";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        callback(resp);
        return;
    }

    auto snapshot = collector_->getLatestSnapshot();
    Json::Value response;
    response["timestamp"] = formatTimestamp(snapshot.timestamp);

    // nginx
    response["nginx"]["activeConnections"] = snapshot.nginx.activeConnections;
    response["nginx"]["totalRequests"] = (Json::Value::UInt64)snapshot.nginx.totalRequests;
    response["nginx"]["requestsPerSecond"] = snapshot.requestsPerSecond;
    response["nginx"]["reading"] = snapshot.nginx.reading;
    response["nginx"]["writing"] = snapshot.nginx.writing;
    response["nginx"]["waiting"] = snapshot.nginx.waiting;

    // services
    Json::Value servicesArr(Json::arrayValue);
    for (const auto& svc : snapshot.services) {
        Json::Value svcJson;
        svcJson["name"] = svc.serviceName;
        svcJson["status"] = svc.status;
        svcJson["responseTimeMs"] = svc.responseTimeMs;

        if (svc.hasDbPool) {
            svcJson["dbPool"]["available"] = (Json::UInt)svc.dbPool.available;
            svcJson["dbPool"]["total"] = (Json::UInt)svc.dbPool.total;
            svcJson["dbPool"]["max"] = (Json::UInt)svc.dbPool.max;
        }
        if (svc.hasLdapPool) {
            svcJson["ldapPool"]["available"] = (Json::UInt)svc.ldapPool.available;
            svcJson["ldapPool"]["total"] = (Json::UInt)svc.ldapPool.total;
            svcJson["ldapPool"]["max"] = (Json::UInt)svc.ldapPool.max;
        }

        servicesArr.append(svcJson);
    }
    response["services"] = servicesArr;

    // system
    response["system"]["cpuPercent"] = snapshot.cpuPercent;
    response["system"]["memoryPercent"] = snapshot.memoryPercent;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

// --- Load History ---

void MonitoringHandler::handleLoadHistory(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    if (!collector_ || !collector_->hasData()) {
        Json::Value result;
        result["intervalSeconds"] = 10;
        result["totalPoints"] = 0;
        result["data"] = Json::Value(Json::arrayValue);
        callback(drogon::HttpResponse::newHttpJsonResponse(result));
        return;
    }

    int minutes = 30;
    auto minutesParam = req->getParameter("minutes");
    if (!minutesParam.empty()) {
        try { minutes = std::stoi(minutesParam); }
        catch (...) {}
    }

    auto history = collector_->getHistory(minutes);

    Json::Value result;
    result["intervalSeconds"] = 10;
    result["totalPoints"] = (Json::UInt)history.size();

    Json::Value dataArr(Json::arrayValue);
    for (const auto& snap : history) {
        Json::Value point;
        point["timestamp"] = formatTimestamp(snap.timestamp);

        point["nginx"]["activeConnections"] = snap.nginx.activeConnections;
        point["nginx"]["requestsPerSecond"] = snap.requestsPerSecond;

        // Per-service latency
        Json::Value latency;
        for (const auto& svc : snap.services) {
            latency[svc.serviceName] = svc.responseTimeMs;
        }
        point["latency"] = latency;

        point["system"]["cpuPercent"] = snap.cpuPercent;
        point["system"]["memoryPercent"] = snap.memoryPercent;

        dataArr.append(point);
    }
    result["data"] = dataArr;

    callback(drogon::HttpResponse::newHttpJsonResponse(result));
}

} // namespace handlers

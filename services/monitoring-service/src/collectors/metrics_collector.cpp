/** @file metrics_collector.cpp
 *  @brief MetricsCollector + RingBuffer implementation
 */

#include "metrics_collector.h"
#include <spdlog/spdlog.h>

#include <sstream>
#include <algorithm>

namespace handlers {

// =============================================================
// RingBuffer
// =============================================================

void RingBuffer::push(const LoadSnapshot& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_[head_] = item;
    head_ = (head_ + 1) % RING_BUFFER_SIZE;
    if (count_ < RING_BUFFER_SIZE) count_++;
}

std::vector<LoadSnapshot> RingBuffer::getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LoadSnapshot> result;
    result.reserve(count_);

    if (count_ == 0) return result;

    // Start from oldest entry
    size_t start = (count_ < RING_BUFFER_SIZE) ? 0 : head_;
    for (size_t i = 0; i < count_; i++) {
        size_t idx = (start + i) % RING_BUFFER_SIZE;
        result.push_back(buffer_[idx]);
    }
    return result;
}

size_t RingBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

LoadSnapshot RingBuffer::latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == 0) return {};
    size_t idx = (head_ == 0) ? RING_BUFFER_SIZE - 1 : head_ - 1;
    return buffer_[idx];
}

bool RingBuffer::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ == 0;
}

// =============================================================
// curl helper
// =============================================================

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* out) {
    size_t totalSize = size * nmemb;
    out->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string MetricsCollector::curlGet(const std::string& url, int timeoutSec) {
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) return response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSec));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        spdlog::debug("curlGet {} failed: {}", url, curl_easy_strerror(res));
        response.clear();
    }

    curl_easy_cleanup(curl);
    return response;
}

// =============================================================
// MetricsCollector
// =============================================================

MetricsCollector::MetricsCollector(MonitoringConfig* config)
    : config_(config) {
    prevCollectTime_ = std::chrono::system_clock::now();
}

void MetricsCollector::collectOnce() {
    auto now = std::chrono::system_clock::now();
    LoadSnapshot snapshot;
    snapshot.timestamp = now;

    // 1. Collect system metrics (CPU, memory)
    auto sysMetrics = systemCollector_.collect();
    snapshot.cpuPercent = sysMetrics.cpu.usagePercent;
    snapshot.memoryPercent = sysMetrics.memory.usagePercent;

    // 2. Fetch nginx stub_status
    snapshot.nginx = fetchNginxStatus();

    // 3. Calculate request rate
    if (!firstCollection_ && snapshot.nginx.totalRequests > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - prevCollectTime_).count();
        if (elapsed > 0) {
            double elapsedSec = static_cast<double>(elapsed) / 1000.0;
            int64_t reqDiff = static_cast<int64_t>(snapshot.nginx.totalRequests) -
                              static_cast<int64_t>(prevTotalRequests_);
            if (reqDiff >= 0) {
                snapshot.requestsPerSecond = static_cast<double>(reqDiff) / elapsedSec;
            }
        }
    }
    prevTotalRequests_ = snapshot.nginx.totalRequests;
    prevCollectTime_ = now;
    firstCollection_ = false;

    // 4. Fetch per-service metrics (pool stats + health)
    // Service endpoints for internal metrics
    std::map<std::string, std::string> metricsEndpoints = {
        {"pkd-management", "http://pkd-management:8081/internal/metrics"},
        {"pa-service", "http://pa-service:8082/internal/metrics"},
        {"pkd-relay", "http://pkd-relay:8083/internal/metrics"},
        {"ai-analysis", "http://ai-analysis:8085/api/ai/internal/metrics"},
    };

    // Override from environment
    if (auto e = std::getenv("METRICS_ENDPOINT_PKD_MANAGEMENT")) metricsEndpoints["pkd-management"] = e;
    if (auto e = std::getenv("METRICS_ENDPOINT_PA_SERVICE")) metricsEndpoints["pa-service"] = e;
    if (auto e = std::getenv("METRICS_ENDPOINT_PKD_RELAY")) metricsEndpoints["pkd-relay"] = e;
    if (auto e = std::getenv("METRICS_ENDPOINT_AI_ANALYSIS")) metricsEndpoints["ai-analysis"] = e;

    for (const auto& [name, url] : metricsEndpoints) {
        auto svcMetrics = fetchServiceMetrics(name, url);

        // Also get health check response time
        auto healthIt = config_->serviceEndpoints.find(name);
        if (healthIt != config_->serviceEndpoints.end()) {
            auto health = healthChecker_.checkService(name, healthIt->second);
            svcMetrics.responseTimeMs = health.responseTimeMs;
            switch (health.status) {
                case ServiceStatus::UP: svcMetrics.status = "UP"; break;
                case ServiceStatus::DEGRADED: svcMetrics.status = "DEGRADED"; break;
                case ServiceStatus::DOWN: svcMetrics.status = "DOWN"; break;
                default: svcMetrics.status = "UNKNOWN";
            }
        }

        snapshot.services.push_back(std::move(svcMetrics));
    }

    // 5. Store in ring buffer
    history_.push(snapshot);
    dataCollected_ = true;

    spdlog::debug("Metrics collected: nginx active={}, rps={:.1f}, services={}",
                  snapshot.nginx.activeConnections, snapshot.requestsPerSecond,
                  snapshot.services.size());
}

LoadSnapshot MetricsCollector::getLatestSnapshot() const {
    return history_.latest();
}

std::vector<LoadSnapshot> MetricsCollector::getHistory(int minutes) const {
    auto all = history_.getAll();
    if (minutes <= 0 || minutes >= 30) return all;

    // Filter to last N minutes
    auto cutoff = std::chrono::system_clock::now() - std::chrono::minutes(minutes);
    std::vector<LoadSnapshot> filtered;
    for (const auto& snap : all) {
        if (snap.timestamp >= cutoff) {
            filtered.push_back(snap);
        }
    }
    return filtered;
}

// =============================================================
// nginx stub_status
// =============================================================

NginxStatus MetricsCollector::fetchNginxStatus() {
    std::string url = "http://api-gateway:8080/nginx_status";
    if (auto e = std::getenv("NGINX_STATUS_URL")) url = e;

    std::string body = curlGet(url, 2);
    if (body.empty()) return {};

    return parseNginxStubStatus(body);
}

NginxStatus MetricsCollector::parseNginxStubStatus(const std::string& body) {
    NginxStatus status;

    // Format:
    // Active connections: 15
    // server accepts handled requests
    //  76 76 243
    // Reading: 0 Writing: 1 Waiting: 14

    std::istringstream iss(body);
    std::string line;

    // Line 1: Active connections: N
    if (std::getline(iss, line)) {
        auto pos = line.find(':');
        if (pos != std::string::npos) {
            try { status.activeConnections = std::stoi(line.substr(pos + 1)); }
            catch (...) {}
        }
    }

    // Line 2: "server accepts handled requests" (skip)
    std::getline(iss, line);

    // Line 3: " 76 76 243"
    if (std::getline(iss, line)) {
        std::istringstream numLine(line);
        numLine >> status.totalAccepts >> status.totalHandled >> status.totalRequests;
    }

    // Line 4: "Reading: 0 Writing: 1 Waiting: 14"
    if (std::getline(iss, line)) {
        // Parse Reading/Writing/Waiting values
        std::istringstream rww(line);
        std::string label;
        int value;
        while (rww >> label >> value) {
            // Remove trailing colon from label
            if (!label.empty() && label.back() == ':') {
                label.pop_back();
            }
            if (label == "Reading") status.reading = value;
            else if (label == "Writing") status.writing = value;
            else if (label == "Waiting") status.waiting = value;
        }
    }

    return status;
}

// =============================================================
// Per-service metrics
// =============================================================

ServiceMetrics MetricsCollector::fetchServiceMetrics(const std::string& name, const std::string& url) {
    ServiceMetrics metrics;
    metrics.serviceName = name;

    std::string body = curlGet(url, 3);
    if (body.empty()) return metrics;

    Json::Value json;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(body);
    if (!Json::parseFromStream(builder, stream, &json, &errors)) {
        spdlog::debug("Failed to parse metrics from {}: {}", name, errors);
        return metrics;
    }

    if (json.isMember("dbPool")) {
        metrics.dbPool = parsePoolStats(json["dbPool"]);
        metrics.hasDbPool = true;
    }
    if (json.isMember("ldapPool")) {
        metrics.ldapPool = parsePoolStats(json["ldapPool"]);
        metrics.hasLdapPool = true;
    }

    return metrics;
}

PoolStats MetricsCollector::parsePoolStats(const Json::Value& json) {
    PoolStats stats;
    if (json.isMember("available")) stats.available = json["available"].asUInt();
    if (json.isMember("total")) stats.total = json["total"].asUInt();
    if (json.isMember("max")) stats.max = json["max"].asUInt();
    return stats;
}

} // namespace handlers

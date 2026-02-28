#pragma once

/**
 * @file metrics_collector.h
 * @brief Background metrics collector with ring buffer for time-series history
 *
 * Collects nginx stub_status, per-service pool stats, and system metrics
 * at configurable intervals. Stores in a fixed-size ring buffer for
 * historical trend charts on the monitoring dashboard.
 */

#include <json/json.h>
#include <curl/curl.h>

#include <string>
#include <vector>
#include <map>
#include <array>
#include <mutex>
#include <chrono>
#include <atomic>

#include "../handlers/monitoring_handler.h"

namespace handlers {

// --- nginx stub_status ---

struct NginxStatus {
    int activeConnections = 0;
    uint64_t totalAccepts = 0;
    uint64_t totalHandled = 0;
    uint64_t totalRequests = 0;
    int reading = 0;
    int writing = 0;
    int waiting = 0;
};

// --- Per-service pool stats ---

struct PoolStats {
    size_t available = 0;
    size_t total = 0;
    size_t max = 0;
};

struct ServiceMetrics {
    std::string serviceName;
    std::string status = "UNKNOWN";
    int responseTimeMs = 0;
    PoolStats dbPool;
    PoolStats ldapPool;
    bool hasDbPool = false;
    bool hasLdapPool = false;
};

// --- Load snapshot (one point in time) ---

struct LoadSnapshot {
    std::chrono::system_clock::time_point timestamp;
    NginxStatus nginx;
    std::vector<ServiceMetrics> services;
    float cpuPercent = 0.0f;
    float memoryPercent = 0.0f;
    double requestsPerSecond = 0.0;
};

// --- Ring buffer for time-series ---

static constexpr size_t RING_BUFFER_SIZE = 180; // 30 min at 10s intervals

class RingBuffer {
public:
    void push(const LoadSnapshot& item);
    std::vector<LoadSnapshot> getAll() const;
    size_t size() const;
    LoadSnapshot latest() const;
    bool empty() const;

private:
    std::array<LoadSnapshot, RING_BUFFER_SIZE> buffer_;
    size_t head_ = 0;
    size_t count_ = 0;
    mutable std::mutex mutex_;
};

// --- Metrics collector ---

class MetricsCollector {
public:
    explicit MetricsCollector(MonitoringConfig* config);

    /// Perform one collection cycle (called by timer)
    void collectOnce();

    /// Get latest snapshot
    LoadSnapshot getLatestSnapshot() const;

    /// Get history (last N minutes)
    std::vector<LoadSnapshot> getHistory(int minutes = 30) const;

    /// Check if any data has been collected
    bool hasData() const { return dataCollected_.load(); }

private:
    /// Fetch and parse nginx stub_status
    NginxStatus fetchNginxStatus();

    /// Parse nginx stub_status response body
    NginxStatus parseNginxStubStatus(const std::string& body);

    /// Fetch service /internal/metrics via curl
    ServiceMetrics fetchServiceMetrics(const std::string& name, const std::string& url);

    /// Parse JSON pool stats from service response
    PoolStats parsePoolStats(const Json::Value& json);

    /// Helper: curl GET with timeout, returns response body
    std::string curlGet(const std::string& url, int timeoutSec = 3);

    MonitoringConfig* config_;
    SystemMetricsCollector systemCollector_;
    ServiceHealthChecker healthChecker_;

    RingBuffer history_;

    // For request rate calculation
    uint64_t prevTotalRequests_ = 0;
    std::chrono::system_clock::time_point prevCollectTime_;
    bool firstCollection_ = true;

    std::atomic<bool> dataCollected_{false};
};

} // namespace handlers

#include "common/notification_manager.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

namespace icao::relay::notification {

NotificationManager& NotificationManager::getInstance() {
    static NotificationManager instance;
    return instance;
}

NotificationManager::~NotificationManager() {
    stopHeartbeat();
}

void NotificationManager::startHeartbeat() {
    if (heartbeatRunning_.exchange(true)) {
        return; // Already running
    }
    heartbeatThread_ = std::thread([this]() {
        spdlog::info("[Notification] Heartbeat thread started (30s interval)");
        while (heartbeatRunning_.load()) {
            // Sleep in 1-second intervals for responsive shutdown
            for (int i = 0; i < 30 && heartbeatRunning_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (heartbeatRunning_.load()) {
                sendHeartbeat();
            }
        }
        spdlog::info("[Notification] Heartbeat thread stopped");
    });
}

void NotificationManager::stopHeartbeat() {
    heartbeatRunning_.store(false);
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
}

void NotificationManager::sendHeartbeat() {
    // SSE comment line — keeps connection alive without triggering events
    const std::string heartbeat = ": heartbeat\n\n";

    std::vector<std::pair<std::string, std::function<void(const std::string&)>>> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (clients_.empty()) return;
        callbacksCopy.reserve(clients_.size());
        for (const auto& [id, cb] : clients_) {
            callbacksCopy.emplace_back(id, cb);
        }
    }

    std::vector<std::string> failedClients;
    for (const auto& [id, cb] : callbacksCopy) {
        try {
            cb(heartbeat);
        } catch (...) {
            failedClients.push_back(id);
        }
    }

    if (!failedClients.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& id : failedClients) {
            clients_.erase(id);
        }
        spdlog::debug("[Notification] Heartbeat removed {} disconnected client(s)", failedClients.size());
    }
}

std::string NotificationManager::registerClient(
    std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Safety: limit max SSE clients (prevent accumulation from leaked connections)
    constexpr size_t MAX_CLIENTS = 50;
    if (clients_.size() >= MAX_CLIENTS) {
        // Remove oldest clients (lowest client IDs)
        size_t toRemove = clients_.size() - MAX_CLIENTS + 1;
        auto it = clients_.begin();
        for (size_t i = 0; i < toRemove && it != clients_.end(); ++i) {
            spdlog::warn("[Notification] Evicting stale client: {}", it->first);
            it = clients_.erase(it);
        }
    }

    std::string clientId = "client_" + std::to_string(++clientCounter_);
    clients_[clientId] = std::move(callback);
    spdlog::info("[Notification] Client registered: {} (total: {})",
                 clientId, clients_.size());
    return clientId;
}

void NotificationManager::unregisterClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto erased = clients_.erase(clientId);
    if (erased > 0) {
        spdlog::info("[Notification] Client unregistered: {} (total: {})",
                     clientId, clients_.size());
    }
}

void NotificationManager::broadcast(
    const std::string& type,
    const std::string& title,
    const std::string& message,
    const Json::Value& data) {

    // Build notification JSON
    Json::Value notification;
    notification["type"] = type;
    notification["title"] = title;
    notification["message"] = message;

    // ISO 8601 timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&time_t, &tm_buf);
    std::ostringstream ts;
    ts << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    notification["timestamp"] = ts.str();

    if (!data.isNull()) {
        notification["data"] = data;
    }

    // Single-line JSON for SSE
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    std::string jsonStr = Json::writeString(writer, notification);

    // SSE format: event: notification\ndata: {json}\n\n
    std::string sseData = "event: notification\ndata: " + jsonStr + "\n\n";

    // Copy-release-execute pattern (deadlock prevention)
    std::vector<std::pair<std::string, std::function<void(const std::string&)>>> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacksCopy.reserve(clients_.size());
        for (const auto& [id, cb] : clients_) {
            callbacksCopy.emplace_back(id, cb);
        }
    }

    if (callbacksCopy.empty()) {
        spdlog::debug("[Notification] No connected clients, skipping broadcast: {}", type);
        return;
    }

    spdlog::info("[Notification] Broadcasting '{}' to {} client(s)",
                 type, callbacksCopy.size());

    // Execute callbacks without holding lock
    std::vector<std::string> failedClients;
    for (const auto& [id, cb] : callbacksCopy) {
        try {
            cb(sseData);
        } catch (const std::exception& e) {
            spdlog::warn("[Notification] Failed to send to {}: {}", id, e.what());
            failedClients.push_back(id);
        } catch (...) {
            spdlog::warn("[Notification] Failed to send to {}: unknown error", id);
            failedClients.push_back(id);
        }
    }

    // Cleanup failed clients
    if (!failedClients.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& id : failedClients) {
            clients_.erase(id);
        }
        spdlog::info("[Notification] Removed {} disconnected client(s)", failedClients.size());
    }
}

size_t NotificationManager::clientCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.size();
}

} // namespace icao::relay::notification

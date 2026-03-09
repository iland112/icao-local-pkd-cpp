#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <json/json.h>

namespace icao::relay::notification {

/**
 * NotificationManager — Thread-safe SSE broadcast singleton
 *
 * Manages SSE client connections and broadcasts notifications to all
 * connected frontend clients. Follows the same copy-release-execute
 * pattern as pkd-management's ProgressManager for deadlock prevention.
 *
 * Notification types:
 * - SYNC_CHECK_COMPLETE: DB-LDAP sync check finished
 * - REVALIDATION_COMPLETE: Certificate revalidation finished
 * - RECONCILE_COMPLETE: Reconciliation finished
 * - DAILY_SYNC_COMPLETE: Full daily sync workflow finished
 */
class NotificationManager {
public:
    static NotificationManager& getInstance();

    // Delete copy/move
    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;

    /**
     * Register an SSE client callback
     * @param callback Function that sends SSE-formatted string to the client
     * @return Unique client ID for unregistration
     */
    std::string registerClient(std::function<void(const std::string&)> callback);

    /**
     * Unregister an SSE client
     * @param clientId Client ID returned by registerClient()
     */
    void unregisterClient(const std::string& clientId);

    /**
     * Broadcast a notification to all connected clients
     * Thread-safe: copies callbacks before releasing lock
     *
     * @param type Notification type (e.g., "REVALIDATION_COMPLETE")
     * @param title Human-readable title (Korean)
     * @param message Summary message
     * @param data Optional JSON payload with detailed results
     */
    void broadcast(const std::string& type, const std::string& title,
                   const std::string& message, const Json::Value& data = Json::nullValue);

    /** Get current connected client count */
    size_t clientCount() const;

    /** Start heartbeat thread (called once at startup) */
    void startHeartbeat();

    /** Stop heartbeat thread (called at shutdown) */
    void stopHeartbeat();

private:
    NotificationManager() = default;
    ~NotificationManager();

    /** Send heartbeat comment to all connected clients */
    void sendHeartbeat();

    mutable std::mutex mutex_;
    std::map<std::string, std::function<void(const std::string&)>> clients_;
    int clientCounter_ = 0;

    // Heartbeat thread
    std::thread heartbeatThread_;
    std::atomic<bool> heartbeatRunning_{false};
};

} // namespace icao::relay::notification

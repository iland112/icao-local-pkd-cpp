#pragma once

/**
 * @file sync_scheduler.h
 * @brief Daily sync scheduler for PKD Relay Service
 *
 * Manages scheduled and manual sync checks, certificate revalidation,
 * and auto-reconciliation. Extracted from main.cpp SyncScheduler class.
 *
 * @date 2026-02-17
 */

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <functional>

namespace infrastructure {

/**
 * @brief Scheduler for periodic DB-LDAP sync operations
 *
 * Supports:
 * - Initial sync check on startup (10s delay)
 * - Daily scheduled sync at configurable time
 * - Manual trigger via API
 * - Certificate revalidation after sync
 * - Auto-reconciliation when discrepancies detected
 */
class SyncScheduler {
public:
    using SyncCheckFn = std::function<void()>;
    using RevalidateFn = std::function<void()>;
    using ReconcileFn = std::function<void(int syncStatusId)>;

    SyncScheduler();

    /**
     * @brief Configure scheduler parameters
     * @param dailySyncEnabled Whether daily sync is enabled
     * @param dailySyncHour Hour for daily sync (0-23)
     * @param dailySyncMinute Minute for daily sync (0-59)
     * @param revalidateCertsOnSync Run certificate revalidation after sync
     * @param autoReconcile Auto-reconcile when discrepancies found
     */
    void configure(bool dailySyncEnabled, int dailySyncHour, int dailySyncMinute,
                   bool revalidateCertsOnSync, bool autoReconcile);

    /** @brief Set callback for sync check operation */
    void setSyncCheckFn(SyncCheckFn fn);

    /** @brief Set callback for certificate revalidation operation */
    void setRevalidateFn(RevalidateFn fn);

    /** @brief Set callback for reconciliation operation */
    void setReconcileFn(ReconcileFn fn);

    /** @brief Start the scheduler threads */
    void start();

    /** @brief Stop the scheduler and join threads */
    void stop();

    /** @brief Trigger daily sync manually (from API) */
    void triggerDailySync();

private:
    /** @brief Get current date as YYYY-MM-DD string */
    std::string getCurrentDateString();

    std::atomic<bool> running_;
    std::string lastDailySyncDate_;
    bool forceDailySync_ = false;

    // Config
    bool dailySyncEnabled_ = true;
    int dailySyncHour_ = 0;
    int dailySyncMinute_ = 0;
    bool revalidateCertsOnSync_ = true;
    bool autoReconcile_ = true;

    // Callbacks
    SyncCheckFn syncCheckFn_;
    RevalidateFn revalidateFn_;
    ReconcileFn reconcileFn_;

    // Threading
    std::thread dailyThread_;
    std::mutex dailyMutex_;
    std::condition_variable dailyCv_;
};

} // namespace infrastructure

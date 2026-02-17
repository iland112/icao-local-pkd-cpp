/**
 * @file sync_scheduler.cpp
 * @brief SyncScheduler implementation - daily sync, revalidation, auto-reconciliation
 *
 * Extracted from main.cpp lines 554-721 (SyncScheduler class, helper functions).
 * Uses callback functions instead of directly referencing globals.
 */

#include "sync_scheduler.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

/**
 * @brief Calculate seconds until next scheduled time
 * @param targetHour Target hour (0-23)
 * @param targetMinute Target minute (0-59)
 * @return Seconds until the next occurrence of the target time
 */
int secondsUntilScheduledTime(int targetHour, int targetMinute) {
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm* localTm = std::localtime(&nowTime);

    // Create target time for today
    std::tm targetTm = *localTm;
    targetTm.tm_hour = targetHour;
    targetTm.tm_min = targetMinute;
    targetTm.tm_sec = 0;

    std::time_t targetTime = std::mktime(&targetTm);

    // If target time has passed today, schedule for tomorrow
    if (targetTime <= nowTime) {
        targetTime += 24 * 60 * 60;  // Add 24 hours
    }

    return static_cast<int>(targetTime - nowTime);
}

/**
 * @brief Format scheduled time as HH:MM string
 * @param targetHour Hour (0-23)
 * @param targetMinute Minute (0-59)
 * @return Formatted time string (e.g., "00:00")
 */
std::string formatScheduledTime(int targetHour, int targetMinute) {
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << targetHour << ":"
       << std::setfill('0') << std::setw(2) << targetMinute;
    return ss.str();
}

} // anonymous namespace

namespace infrastructure {

SyncScheduler::SyncScheduler() : running_(false), lastDailySyncDate_("") {}

void SyncScheduler::configure(bool dailySyncEnabled, int dailySyncHour, int dailySyncMinute,
                               bool revalidateCertsOnSync, bool autoReconcile) {
    dailySyncEnabled_ = dailySyncEnabled;
    dailySyncHour_ = dailySyncHour;
    dailySyncMinute_ = dailySyncMinute;
    revalidateCertsOnSync_ = revalidateCertsOnSync;
    autoReconcile_ = autoReconcile;
}

void SyncScheduler::setSyncCheckFn(SyncCheckFn fn) {
    syncCheckFn_ = std::move(fn);
}

void SyncScheduler::setRevalidateFn(RevalidateFn fn) {
    revalidateFn_ = std::move(fn);
}

void SyncScheduler::setReconcileFn(ReconcileFn fn) {
    reconcileFn_ = std::move(fn);
}

void SyncScheduler::start() {
    running_ = true;

    // Perform initial sync check after startup delay
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (running_ && syncCheckFn_) {
            spdlog::info("Performing initial sync check after startup...");
            try {
                syncCheckFn_();
            } catch (const std::exception& e) {
                spdlog::error("Initial sync check failed: {}", e.what());
            }
        }
    }).detach();

    // Start daily sync thread
    if (dailySyncEnabled_) {
        dailyThread_ = std::thread([this]() {
            std::string scheduledTime = formatScheduledTime(dailySyncHour_, dailySyncMinute_);
            spdlog::info("Daily sync scheduler started (scheduled at {} daily)", scheduledTime);

            while (running_) {
                // Calculate time until next scheduled run
                int waitSeconds = secondsUntilScheduledTime(dailySyncHour_, dailySyncMinute_);
                spdlog::info("Next daily sync in {} seconds ({} hours {} minutes)",
                             waitSeconds, waitSeconds / 3600, (waitSeconds % 3600) / 60);

                // Wait until scheduled time
                std::unique_lock<std::mutex> lock(dailyMutex_);
                dailyCv_.wait_for(lock, std::chrono::seconds(waitSeconds),
                            [this]() { return !running_ || forceDailySync_; });

                if (!running_) break;

                // Check if we should run (either scheduled time reached or forced)
                std::string today = getCurrentDateString();
                if (forceDailySync_ || lastDailySyncDate_ != today) {
                    forceDailySync_ = false;
                    lastDailySyncDate_ = today;

                    spdlog::info("=== Starting Daily Sync Tasks ===");

                    try {
                        // 1. Perform sync check
                        spdlog::info("[Daily] Step 1: Performing sync check...");
                        if (syncCheckFn_) {
                            syncCheckFn_();
                        }

                        // 2. Re-validate certificates if enabled
                        if (revalidateCertsOnSync_ && revalidateFn_) {
                            spdlog::info("[Daily] Step 2: Performing certificate re-validation...");
                            revalidateFn_();
                        }

                        // 3. Auto reconcile if enabled
                        if (autoReconcile_ && reconcileFn_) {
                            spdlog::info("[Daily] Step 3: Checking for reconciliation...");
                            reconcileFn_(0);  // syncStatusId=0, engine will determine discrepancies
                        }

                        spdlog::info("=== Daily Sync Tasks Completed ===");
                    } catch (const std::exception& e) {
                        spdlog::error("Daily sync failed: {}", e.what());
                    }
                }
            }

            spdlog::info("Daily sync scheduler stopped");
        });
    }
}

void SyncScheduler::stop() {
    running_ = false;
    dailyCv_.notify_all();

    if (dailyThread_.joinable()) {
        dailyThread_.join();
    }
}

void SyncScheduler::triggerDailySync() {
    {
        std::lock_guard<std::mutex> lock(dailyMutex_);
        forceDailySync_ = true;
    }
    dailyCv_.notify_all();
}

std::string SyncScheduler::getCurrentDateString() {
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm* localTm = std::localtime(&nowTime);

    std::ostringstream ss;
    ss << std::put_time(localTm, "%Y-%m-%d");
    return ss.str();
}

} // namespace infrastructure

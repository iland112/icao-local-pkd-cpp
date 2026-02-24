/**
 * @file api_rate_limiter.cpp
 * @brief In-memory sliding window rate limiter implementation
 */

#include "api_rate_limiter.h"
#include <spdlog/spdlog.h>

namespace middleware {

ApiRateLimiter::ApiRateLimiter() {
    spdlog::info("[ApiRateLimiter] Initialized");
}

ApiRateLimiter::~ApiRateLimiter() = default;

void ApiRateLimiter::resetIfExpired(Window& w, std::chrono::seconds duration) {
    auto now = std::chrono::steady_clock::now();
    if (now - w.start >= duration) {
        w.count = 0;
        w.start = now;
    }
}

RateLimitInfo ApiRateLimiter::checkAndIncrement(
    const std::string& clientId,
    int limitPerMin, int limitPerHour, int limitPerDay) {

    std::unique_lock lock(mutex_);

    auto& cw = windows_[clientId];
    auto now = std::chrono::steady_clock::now();

    // Initialize windows on first access
    if (cw.minute.start == std::chrono::steady_clock::time_point{}) {
        cw.minute = {0, now};
        cw.hour = {0, now};
        cw.day = {0, now};
    }

    // Reset expired windows
    resetIfExpired(cw.minute, std::chrono::seconds(60));
    resetIfExpired(cw.hour, std::chrono::seconds(3600));
    resetIfExpired(cw.day, std::chrono::seconds(86400));

    // Check limits (most restrictive first)
    if (limitPerMin > 0 && cw.minute.count >= limitPerMin) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cw.minute.start);
        int64_t resetAt = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now() + (std::chrono::seconds(60) - elapsed));
        return {false, limitPerMin, 0, resetAt, "per_minute"};
    }

    if (limitPerHour > 0 && cw.hour.count >= limitPerHour) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cw.hour.start);
        int64_t resetAt = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now() + (std::chrono::seconds(3600) - elapsed));
        return {false, limitPerHour, 0, resetAt, "per_hour"};
    }

    if (limitPerDay > 0 && cw.day.count >= limitPerDay) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cw.day.start);
        int64_t resetAt = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now() + (std::chrono::seconds(86400) - elapsed));
        return {false, limitPerDay, 0, resetAt, "per_day"};
    }

    // Increment all counters
    cw.minute.count++;
    cw.hour.count++;
    cw.day.count++;

    int remaining = limitPerMin > 0 ? (limitPerMin - static_cast<int>(cw.minute.count)) : 999;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cw.minute.start);
    int64_t resetAt = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + (std::chrono::seconds(60) - elapsed));

    return {true, limitPerMin, remaining, resetAt, "per_minute"};
}

void ApiRateLimiter::cleanup() {
    std::unique_lock lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto it = windows_.begin(); it != windows_.end(); ) {
        auto& cw = it->second;
        // Remove entries idle for more than 24 hours
        if (now - cw.day.start > std::chrono::hours(24) && cw.day.count == 0) {
            it = windows_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace middleware

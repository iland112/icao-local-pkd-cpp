#pragma once

/**
 * @file api_rate_limiter.h
 * @brief In-memory sliding window rate limiter for API clients
 *
 * Thread-safe per-client rate limiting with minute/hour/day windows.
 */

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <atomic>

namespace middleware {

struct RateLimitInfo {
    bool allowed;
    int limit;
    int remaining;
    int64_t resetAt;  // Unix timestamp
    std::string window;  // "per_minute", "per_hour", "per_day"
};

class ApiRateLimiter {
public:
    ApiRateLimiter();
    ~ApiRateLimiter();

    /**
     * @brief Check if a request is allowed and increment counter
     * @param clientId Client UUID
     * @param limitPerMin Per-minute limit
     * @param limitPerHour Per-hour limit
     * @param limitPerDay Per-day limit
     * @return RateLimitInfo with allowed status and remaining quota
     */
    RateLimitInfo checkAndIncrement(
        const std::string& clientId,
        int limitPerMin, int limitPerHour, int limitPerDay);

    /**
     * @brief Clean up expired windows (call periodically)
     */
    void cleanup();

private:
    struct Window {
        int64_t count;
        std::chrono::steady_clock::time_point start;
    };

    struct ClientWindows {
        Window minute;
        Window hour;
        Window day;
    };

    std::unordered_map<std::string, ClientWindows> windows_;
    mutable std::shared_mutex mutex_;

    void resetIfExpired(Window& w, std::chrono::seconds duration);
};

} // namespace middleware

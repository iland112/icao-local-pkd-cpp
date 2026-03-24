/**
 * @file test_api_rate_limiter.cpp
 * @brief Unit tests for ApiRateLimiter — 3-tier sliding window rate limiting
 *
 * Tests cover:
 *  - Happy path: requests allowed under each tier limit
 *  - Exhaustion: per-minute, per-hour, per-day limits enforce correctly
 *  - Client isolation: separate clients have independent counters
 *  - Window semantics: window/tier reported in denied response
 *  - Unlimited tiers (limit <= 0): tier is skipped
 *  - Remaining counter: decrements correctly until 0
 *  - resetAt timestamp: in the future for both allowed and denied responses
 *  - Thread safety: concurrent increments from multiple threads
 *  - cleanup(): explicit eviction of idle windows
 *  - Auto-cleanup on 100th call: stale entries are removed
 *  - Idempotency: same client always produces consistent cumulative counts
 */

#include <gtest/gtest.h>
#include "../src/middleware/api_rate_limiter.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>

using middleware::ApiRateLimiter;
using middleware::RateLimitInfo;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t nowUnix() {
    return static_cast<int64_t>(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class ApiRateLimiterTest : public ::testing::Test {
protected:
    ApiRateLimiter limiter;

    // Convenience: fire N requests for a single client; return vector of results
    std::vector<RateLimitInfo> fireRequests(
        const std::string& clientId,
        int n,
        int limitPerMin = 10,
        int limitPerHour = 100,
        int limitPerDay = 1000)
    {
        std::vector<RateLimitInfo> results;
        results.reserve(n);
        for (int i = 0; i < n; i++) {
            results.push_back(
                limiter.checkAndIncrement(clientId, limitPerMin, limitPerHour, limitPerDay));
        }
        return results;
    }
};

// ===========================================================================
// Happy Path — Requests Allowed Under Limit
// ===========================================================================

TEST_F(ApiRateLimiterTest, FirstRequest_IsAllowed) {
    auto info = limiter.checkAndIncrement("client-1", 10, 100, 1000);
    EXPECT_TRUE(info.allowed);
}

TEST_F(ApiRateLimiterTest, AllowedResult_HasCorrectLimit) {
    auto info = limiter.checkAndIncrement("client-1", 5, 50, 500);
    EXPECT_TRUE(info.allowed);
    EXPECT_EQ(info.limit, 5);  // tightest window (per_minute) governs limit field
}

TEST_F(ApiRateLimiterTest, AllowedResult_RemainingDecrementsPerRequest) {
    // With per-minute limit of 5, each request decrements remaining by 1
    int limit = 5;
    for (int i = 1; i <= limit; i++) {
        auto info = limiter.checkAndIncrement("client-dec", limit, 100, 1000);
        EXPECT_TRUE(info.allowed);
        int expected = limit - i;
        EXPECT_EQ(info.remaining, expected)
            << "After request " << i << " remaining should be " << expected;
    }
}

TEST_F(ApiRateLimiterTest, AllowedResult_ResetAtInFuture) {
    auto info = limiter.checkAndIncrement("client-reset", 10, 100, 1000);
    EXPECT_TRUE(info.allowed);
    EXPECT_GE(info.resetAt, nowUnix())
        << "resetAt should be >= current time";
    // Reset for per-minute window is at most 60s in the future
    EXPECT_LE(info.resetAt, nowUnix() + 65)
        << "resetAt should be within the current minute window";
}

TEST_F(ApiRateLimiterTest, AllowedResult_WindowIsPerMinute) {
    auto info = limiter.checkAndIncrement("client-window", 10, 100, 1000);
    EXPECT_TRUE(info.allowed);
    EXPECT_EQ(info.window, "per_minute");
}

TEST_F(ApiRateLimiterTest, MultipleRequests_AllAllowedUnderLimit) {
    int limit = 8;
    auto results = fireRequests("client-multi", limit, limit, 1000, 10000);
    for (const auto& r : results) {
        EXPECT_TRUE(r.allowed) << "All " << limit << " requests should be allowed";
    }
}

// ===========================================================================
// Per-Minute Limit Enforcement
// ===========================================================================

TEST_F(ApiRateLimiterTest, PerMinute_ExactlyAtLimit_Denied) {
    int limit = 3;
    // Fill up the minute window
    for (int i = 0; i < limit; i++) {
        auto info = limiter.checkAndIncrement("client-min", limit, 1000, 10000);
        EXPECT_TRUE(info.allowed);
    }
    // The next request must be denied
    auto denied = limiter.checkAndIncrement("client-min", limit, 1000, 10000);
    EXPECT_FALSE(denied.allowed);
    EXPECT_EQ(denied.window, "per_minute");
    EXPECT_EQ(denied.limit, limit);
    EXPECT_EQ(denied.remaining, 0);
}

TEST_F(ApiRateLimiterTest, PerMinute_DeniedResult_ResetAtInFuture) {
    int limit = 2;
    limiter.checkAndIncrement("client-min-reset", limit, 100, 1000);
    limiter.checkAndIncrement("client-min-reset", limit, 100, 1000);
    auto denied = limiter.checkAndIncrement("client-min-reset", limit, 100, 1000);
    EXPECT_FALSE(denied.allowed);
    EXPECT_GE(denied.resetAt, nowUnix());
    EXPECT_LE(denied.resetAt, nowUnix() + 65);
}

TEST_F(ApiRateLimiterTest, PerMinute_StaysDeniedOnSubsequentCalls) {
    int limit = 1;
    limiter.checkAndIncrement("client-stay", limit, 100, 1000);  // allowed
    for (int i = 0; i < 5; i++) {
        auto denied = limiter.checkAndIncrement("client-stay", limit, 100, 1000);
        EXPECT_FALSE(denied.allowed)
            << "Request " << (i + 2) << " should still be denied";
        EXPECT_EQ(denied.window, "per_minute");
    }
}

// ===========================================================================
// Per-Hour Limit Enforcement
// ===========================================================================

TEST_F(ApiRateLimiterTest, PerHour_ExactlyAtLimit_Denied) {
    // Use a high per-minute limit so it does not trigger first
    int limitPerHour = 3;
    int limitPerMin  = 100;
    for (int i = 0; i < limitPerHour; i++) {
        auto info = limiter.checkAndIncrement("client-hour", limitPerMin, limitPerHour, 10000);
        EXPECT_TRUE(info.allowed);
    }
    auto denied = limiter.checkAndIncrement("client-hour", limitPerMin, limitPerHour, 10000);
    EXPECT_FALSE(denied.allowed);
    EXPECT_EQ(denied.window, "per_hour");
    EXPECT_EQ(denied.limit, limitPerHour);
    EXPECT_EQ(denied.remaining, 0);
}

TEST_F(ApiRateLimiterTest, PerHour_DeniedResult_ResetAtInFuture) {
    int limitPerHour = 2;
    limiter.checkAndIncrement("client-hour-reset", 100, limitPerHour, 10000);
    limiter.checkAndIncrement("client-hour-reset", 100, limitPerHour, 10000);
    auto denied = limiter.checkAndIncrement("client-hour-reset", 100, limitPerHour, 10000);
    EXPECT_FALSE(denied.allowed);
    EXPECT_GE(denied.resetAt, nowUnix());
    // Within the hour window: at most 3600 + 5s buffer
    EXPECT_LE(denied.resetAt, nowUnix() + 3605);
}

// ===========================================================================
// Per-Day Limit Enforcement
// ===========================================================================

TEST_F(ApiRateLimiterTest, PerDay_ExactlyAtLimit_Denied) {
    int limitPerDay = 2;
    for (int i = 0; i < limitPerDay; i++) {
        auto info = limiter.checkAndIncrement("client-day", 100, 1000, limitPerDay);
        EXPECT_TRUE(info.allowed);
    }
    auto denied = limiter.checkAndIncrement("client-day", 100, 1000, limitPerDay);
    EXPECT_FALSE(denied.allowed);
    EXPECT_EQ(denied.window, "per_day");
    EXPECT_EQ(denied.limit, limitPerDay);
    EXPECT_EQ(denied.remaining, 0);
}

TEST_F(ApiRateLimiterTest, PerDay_DeniedResult_ResetAtInFuture) {
    int limitPerDay = 1;
    limiter.checkAndIncrement("client-day-reset", 100, 1000, limitPerDay);
    auto denied = limiter.checkAndIncrement("client-day-reset", 100, 1000, limitPerDay);
    EXPECT_FALSE(denied.allowed);
    EXPECT_GE(denied.resetAt, nowUnix());
    EXPECT_LE(denied.resetAt, nowUnix() + 86405);
}

// ===========================================================================
// Most-Restrictive Tier Fires First
// ===========================================================================

TEST_F(ApiRateLimiterTest, PerMinute_TriggersBeforePerHour) {
    // per-minute limit is tighter: 2 vs per-hour limit of 100
    limiter.checkAndIncrement("client-order", 2, 100, 1000);
    limiter.checkAndIncrement("client-order", 2, 100, 1000);
    auto denied = limiter.checkAndIncrement("client-order", 2, 100, 1000);
    EXPECT_FALSE(denied.allowed);
    EXPECT_EQ(denied.window, "per_minute");
}

TEST_F(ApiRateLimiterTest, PerHour_TriggersBeforePerDay) {
    // per-hour limit is tighter: 2 vs per-day limit of 1000, per-minute is unlimited
    limiter.checkAndIncrement("client-order2", 0, 2, 1000);
    limiter.checkAndIncrement("client-order2", 0, 2, 1000);
    auto denied = limiter.checkAndIncrement("client-order2", 0, 2, 1000);
    EXPECT_FALSE(denied.allowed);
    EXPECT_EQ(denied.window, "per_hour");
}

// ===========================================================================
// Unlimited Tiers (limit <= 0 means disabled)
// ===========================================================================

TEST_F(ApiRateLimiterTest, ZeroLimit_PerMinute_DoesNotEnforce) {
    // limitPerMin = 0 disables the per-minute check; per-hour is the binding limit
    for (int i = 0; i < 50; i++) {
        auto info = limiter.checkAndIncrement("client-unlimited-min", 0, 100, 1000);
        EXPECT_TRUE(info.allowed) << "Request " << i << " should pass (no per-min limit)";
    }
}

TEST_F(ApiRateLimiterTest, ZeroLimit_PerHour_DoesNotEnforce) {
    // limitPerHour = 0 disables hourly check
    for (int i = 0; i < 5; i++) {
        auto info = limiter.checkAndIncrement("client-unlimited-hour", 100, 0, 1000);
        EXPECT_TRUE(info.allowed);
    }
}

TEST_F(ApiRateLimiterTest, ZeroLimit_PerDay_DoesNotEnforce) {
    for (int i = 0; i < 5; i++) {
        auto info = limiter.checkAndIncrement("client-unlimited-day", 100, 1000, 0);
        EXPECT_TRUE(info.allowed);
    }
}

TEST_F(ApiRateLimiterTest, AllZeroLimits_AlwaysAllowed) {
    for (int i = 0; i < 200; i++) {
        auto info = limiter.checkAndIncrement("client-all-zero", 0, 0, 0);
        EXPECT_TRUE(info.allowed);
    }
}

// ===========================================================================
// Client Isolation
// ===========================================================================

TEST_F(ApiRateLimiterTest, DifferentClients_HaveIndependentCounters) {
    int limit = 3;
    // Exhaust client-A
    for (int i = 0; i < limit; i++) {
        limiter.checkAndIncrement("client-A", limit, 1000, 10000);
    }
    auto deniedA = limiter.checkAndIncrement("client-A", limit, 1000, 10000);
    EXPECT_FALSE(deniedA.allowed);

    // client-B should still be allowed
    auto allowedB = limiter.checkAndIncrement("client-B", limit, 1000, 10000);
    EXPECT_TRUE(allowedB.allowed);
}

TEST_F(ApiRateLimiterTest, ManyClients_EachIndependent) {
    int limit = 2;
    const int numClients = 20;
    // Exhaust every client
    for (int c = 0; c < numClients; c++) {
        std::string id = "client-many-" + std::to_string(c);
        for (int i = 0; i < limit; i++) {
            limiter.checkAndIncrement(id, limit, 1000, 10000);
        }
        auto denied = limiter.checkAndIncrement(id, limit, 1000, 10000);
        EXPECT_FALSE(denied.allowed) << "client " << c << " should be denied";
    }
}

TEST_F(ApiRateLimiterTest, SameClientId_CountsAccumulate) {
    int limit = 4;
    // Two separate calls with the same ID share the counter
    limiter.checkAndIncrement("client-shared", limit, 100, 1000);
    limiter.checkAndIncrement("client-shared", limit, 100, 1000);
    limiter.checkAndIncrement("client-shared", limit, 100, 1000);
    limiter.checkAndIncrement("client-shared", limit, 100, 1000);
    auto denied = limiter.checkAndIncrement("client-shared", limit, 100, 1000);
    EXPECT_FALSE(denied.allowed);
}

// ===========================================================================
// Remaining Counter Accuracy
// ===========================================================================

TEST_F(ApiRateLimiterTest, Remaining_StartsAtLimitMinusOne) {
    auto info = limiter.checkAndIncrement("client-rem1", 10, 100, 1000);
    EXPECT_TRUE(info.allowed);
    EXPECT_EQ(info.remaining, 9);
}

TEST_F(ApiRateLimiterTest, Remaining_IsZeroOnLastAllowedRequest) {
    int limit = 3;
    RateLimitInfo last;
    for (int i = 0; i < limit; i++) {
        last = limiter.checkAndIncrement("client-rem-last", limit, 100, 1000);
    }
    EXPECT_TRUE(last.allowed);
    EXPECT_EQ(last.remaining, 0);
}

TEST_F(ApiRateLimiterTest, Remaining_IsZeroWhenDenied) {
    int limit = 2;
    limiter.checkAndIncrement("client-rem-zero", limit, 100, 1000);
    limiter.checkAndIncrement("client-rem-zero", limit, 100, 1000);
    auto denied = limiter.checkAndIncrement("client-rem-zero", limit, 100, 1000);
    EXPECT_FALSE(denied.allowed);
    EXPECT_EQ(denied.remaining, 0);
}

// ===========================================================================
// RateLimitInfo Fields on Allowed Responses
// ===========================================================================

TEST_F(ApiRateLimiterTest, AllowedResponse_ContainsAllFields) {
    auto info = limiter.checkAndIncrement("client-fields", 10, 100, 1000);
    EXPECT_TRUE(info.allowed);
    EXPECT_GT(info.limit, 0);
    EXPECT_GE(info.remaining, 0);
    EXPECT_GT(info.resetAt, 0);
    EXPECT_FALSE(info.window.empty());
}

TEST_F(ApiRateLimiterTest, DeniedResponse_ContainsAllFields) {
    limiter.checkAndIncrement("client-denied-fields", 1, 100, 1000);
    auto info = limiter.checkAndIncrement("client-denied-fields", 1, 100, 1000);
    EXPECT_FALSE(info.allowed);
    EXPECT_GT(info.limit, 0);
    EXPECT_EQ(info.remaining, 0);
    EXPECT_GT(info.resetAt, 0);
    EXPECT_FALSE(info.window.empty());
}

// ===========================================================================
// explicit cleanup() — evicts idle entries
// ===========================================================================

TEST_F(ApiRateLimiterTest, Cleanup_DoesNotThrow) {
    limiter.checkAndIncrement("client-cleanup", 10, 100, 1000);
    EXPECT_NO_THROW(limiter.cleanup());
}

TEST_F(ApiRateLimiterTest, Cleanup_ActiveWindowNotEvicted) {
    // An active window (count > 0, not 24h old) must survive cleanup()
    limiter.checkAndIncrement("client-active", 10, 100, 1000);
    limiter.cleanup();
    // After cleanup, the counter must still be active: the 2nd request increments
    // and remaining decrements from 9 to 8
    auto info = limiter.checkAndIncrement("client-active", 10, 100, 1000);
    EXPECT_TRUE(info.allowed);
    EXPECT_EQ(info.remaining, 8);
}

// ===========================================================================
// Auto-Cleanup at 100th Call
// ===========================================================================

TEST_F(ApiRateLimiterTest, AutoCleanup_FiresWithoutError_At100thCall) {
    // Insert 5 clients with one request each, then fire 100 requests for a
    // single client to trigger auto-cleanup without crashing.
    for (int i = 0; i < 5; i++) {
        limiter.checkAndIncrement("ghost-" + std::to_string(i), 10, 100, 1000);
    }
    EXPECT_NO_THROW({
        for (int i = 0; i < 100; i++) {
            limiter.checkAndIncrement("trigger-client", 200, 2000, 20000);
        }
    });
}

// ===========================================================================
// Thread Safety
// ===========================================================================

TEST_F(ApiRateLimiterTest, ConcurrentRequests_SameClient_NoRaceConditions) {
    // Fire 50 concurrent requests for the same client with a limit of 200
    // (all should be allowed). The test asserts no crash / data corruption.
    const int numThreads = 10;
    const int reqsPerThread = 5;
    std::atomic<int> allowedCount{0};

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < reqsPerThread; i++) {
                auto info = limiter.checkAndIncrement("concurrent-same", 200, 2000, 20000);
                if (info.allowed) allowedCount++;
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(allowedCount.load(), numThreads * reqsPerThread);
}

TEST_F(ApiRateLimiterTest, ConcurrentRequests_DifferentClients_NoRaceConditions) {
    const int numThreads = 20;
    std::atomic<int> totalAllowed{0};

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&, t]() {
            std::string clientId = "thread-client-" + std::to_string(t);
            // Each thread sends 3 requests; limit=3 so all should be allowed
            for (int i = 0; i < 3; i++) {
                auto info = limiter.checkAndIncrement(clientId, 3, 100, 1000);
                if (info.allowed) totalAllowed++;
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(totalAllowed.load(), numThreads * 3);
}

TEST_F(ApiRateLimiterTest, ConcurrentRequests_LimitEnforced) {
    // 20 threads each fire 1 request for the same client; limit is 10.
    // Exactly 10 should be allowed and 10 denied.
    const int numThreads = 20;
    const int limit = 10;
    std::atomic<int> allowed{0};
    std::atomic<int> denied{0};

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&]() {
            auto info = limiter.checkAndIncrement("limit-enforce", limit, 1000, 10000);
            if (info.allowed) allowed++;
            else denied++;
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(allowed.load(), limit);
    EXPECT_EQ(denied.load(), numThreads - limit);
}

// ===========================================================================
// Edge Cases
// ===========================================================================

TEST_F(ApiRateLimiterTest, EmptyClientId_HandledWithoutCrash) {
    EXPECT_NO_THROW({
        auto info = limiter.checkAndIncrement("", 10, 100, 1000);
        EXPECT_TRUE(info.allowed);  // First request for empty-string client should pass
    });
}

TEST_F(ApiRateLimiterTest, LimitOfOne_AllowsExactlyOneRequest) {
    auto allowed = limiter.checkAndIncrement("one-shot", 1, 100, 1000);
    EXPECT_TRUE(allowed.allowed);
    EXPECT_EQ(allowed.remaining, 0);

    auto denied = limiter.checkAndIncrement("one-shot", 1, 100, 1000);
    EXPECT_FALSE(denied.allowed);
}

TEST_F(ApiRateLimiterTest, VeryLargeLimit_DoesNotOverflow) {
    int bigLimit = 1'000'000;
    auto info = limiter.checkAndIncrement("big-limit", bigLimit, bigLimit, bigLimit);
    EXPECT_TRUE(info.allowed);
    EXPECT_EQ(info.remaining, bigLimit - 1);
}

TEST_F(ApiRateLimiterTest, LongClientId_HandledCorrectly) {
    std::string longId(512, 'x');
    EXPECT_NO_THROW({
        auto info = limiter.checkAndIncrement(longId, 5, 50, 500);
        EXPECT_TRUE(info.allowed);
    });
}

TEST_F(ApiRateLimiterTest, UnicodeClientId_HandledCorrectly) {
    // Korean UUID-like string (simulates API key prefix)
    std::string koreanId = "한국어-클라이언트-아이디-테스트";
    EXPECT_NO_THROW({
        auto info = limiter.checkAndIncrement(koreanId, 5, 50, 500);
        EXPECT_TRUE(info.allowed);
    });
}

// ===========================================================================
// Idempotency / No Cross-State Leakage Between Tests
// ===========================================================================

TEST_F(ApiRateLimiterTest, Idempotency_TwoLimitersAreIndependent) {
    // Each test fixture creates a new limiter, so this simply verifies that
    // a freshly constructed limiter has no state from any prior test.
    ApiRateLimiter freshLimiter;
    auto info = freshLimiter.checkAndIncrement("any-client", 1, 10, 100);
    EXPECT_TRUE(info.allowed)
        << "Fresh limiter must allow the very first request for any client";
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

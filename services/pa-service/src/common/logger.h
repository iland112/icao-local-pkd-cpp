/**
 * @file logger.h
 * @brief Enhanced logging with request context and structured format
 *
 * Provides:
 * - Request ID tracking for distributed tracing
 * - Structured logging with context
 * - Performance timing utilities
 * - Error logging with stack context
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#pragma once

#include <spdlog/spdlog.h>
#include <string>
#include <chrono>
#include <memory>
#include <json/json.h>

namespace common {

/**
 * @brief Request context for logging
 *
 * Captures context information for a single request
 */
class RequestContext {
private:
    std::string requestId_;
    std::string endpoint_;
    std::string method_;
    std::string clientIp_;
    std::chrono::steady_clock::time_point startTime_;

public:
    RequestContext(
        const std::string& requestId,
        const std::string& endpoint,
        const std::string& method = "",
        const std::string& clientIp = "")
        : requestId_(requestId)
        , endpoint_(endpoint)
        , method_(method)
        , clientIp_(clientIp)
        , startTime_(std::chrono::steady_clock::now()) {}

    /**
     * @brief Get request ID
     */
    const std::string& getRequestId() const {
        return requestId_;
    }

    /**
     * @brief Get endpoint
     */
    const std::string& getEndpoint() const {
        return endpoint_;
    }

    /**
     * @brief Get HTTP method
     */
    const std::string& getMethod() const {
        return method_;
    }

    /**
     * @brief Get client IP
     */
    const std::string& getClientIp() const {
        return clientIp_;
    }

    /**
     * @brief Get elapsed time since request start (milliseconds)
     */
    long long getElapsedMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    }

    /**
     * @brief Get context as JSON
     */
    Json::Value toJson() const {
        Json::Value json;
        json["requestId"] = requestId_;
        json["endpoint"] = endpoint_;
        json["method"] = method_;
        json["clientIp"] = clientIp_;
        json["elapsedMs"] = Json::Value::Int64(getElapsedMs());
        return json;
    }
};

/**
 * @brief Enhanced logger with request context
 */
class Logger {
public:
    /**
     * @brief Log with request context
     */
    static void logInfo(const RequestContext& ctx, const std::string& message) {
        spdlog::info("[{}] [{}] {} ({}ms)",
                     ctx.getRequestId(),
                     ctx.getEndpoint(),
                     message,
                     ctx.getElapsedMs());
    }

    static void logWarn(const RequestContext& ctx, const std::string& message) {
        spdlog::warn("[{}] [{}] {} ({}ms)",
                     ctx.getRequestId(),
                     ctx.getEndpoint(),
                     message,
                     ctx.getElapsedMs());
    }

    static void logError(const RequestContext& ctx, const std::string& message, const std::string& details = "") {
        if (details.empty()) {
            spdlog::error("[{}] [{}] {} ({}ms)",
                          ctx.getRequestId(),
                          ctx.getEndpoint(),
                          message,
                          ctx.getElapsedMs());
        } else {
            spdlog::error("[{}] [{}] {} - Details: {} ({}ms)",
                          ctx.getRequestId(),
                          ctx.getEndpoint(),
                          message,
                          details,
                          ctx.getElapsedMs());
        }
    }

    /**
     * @brief Log structured data as JSON
     */
    static void logJson(const RequestContext& ctx, const std::string& event, const Json::Value& data) {
        Json::Value log;
        log["requestId"] = ctx.getRequestId();
        log["endpoint"] = ctx.getEndpoint();
        log["event"] = event;
        log["elapsedMs"] = Json::Value::Int64(ctx.getElapsedMs());
        log["data"] = data;

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";  // Compact JSON
        std::string jsonStr = Json::writeString(builder, log);

        spdlog::info("{}", jsonStr);
    }

    /**
     * @brief Log request start
     */
    static void logRequestStart(const RequestContext& ctx) {
        spdlog::info("[{}] {} {} from {}",
                     ctx.getRequestId(),
                     ctx.getMethod(),
                     ctx.getEndpoint(),
                     ctx.getClientIp());
    }

    /**
     * @brief Log request completion
     */
    static void logRequestComplete(const RequestContext& ctx, int statusCode) {
        spdlog::info("[{}] {} {} completed with status {} ({}ms)",
                     ctx.getRequestId(),
                     ctx.getMethod(),
                     ctx.getEndpoint(),
                     statusCode,
                     ctx.getElapsedMs());
    }

    /**
     * @brief Log database query
     */
    static void logDbQuery(const RequestContext& ctx, const std::string& operation, const std::string& table) {
        spdlog::debug("[{}] DB Query: {} on table '{}' ({}ms)",
                      ctx.getRequestId(),
                      operation,
                      table,
                      ctx.getElapsedMs());
    }

    /**
     * @brief Log LDAP operation
     */
    static void logLdapOp(const RequestContext& ctx, const std::string& operation, const std::string& baseDn) {
        spdlog::debug("[{}] LDAP Op: {} on '{}' ({}ms)",
                      ctx.getRequestId(),
                      operation,
                      baseDn,
                      ctx.getElapsedMs());
    }
};

/**
 * @brief Performance timer for operation tracking
 */
class PerformanceTimer {
private:
    std::string operation_;
    std::chrono::steady_clock::time_point startTime_;
    const RequestContext* ctx_;

public:
    explicit PerformanceTimer(const std::string& operation, const RequestContext* ctx = nullptr)
        : operation_(operation)
        , startTime_(std::chrono::steady_clock::now())
        , ctx_(ctx) {}

    ~PerformanceTimer() {
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime_).count();

        if (ctx_) {
            spdlog::debug("[{}] Performance: {} took {}ms",
                          ctx_->getRequestId(),
                          operation_,
                          duration);
        } else {
            spdlog::debug("Performance: {} took {}ms", operation_, duration);
        }
    }

    /**
     * @brief Get elapsed time without destroying timer
     */
    long long getElapsedMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    }
};

/**
 * @brief Generate unique request ID
 */
inline std::string generateRequestId() {
    // Use timestamp + random component
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // Simple random component (not cryptographically secure, but sufficient for request IDs)
    unsigned int random = static_cast<unsigned int>(rand());

    return "REQ-" + std::to_string(timestamp) + "-" + std::to_string(random);
}

} // namespace common

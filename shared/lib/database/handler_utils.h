#pragma once

#include <string>
#include <algorithm>
#include <json/json.h>
#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

/**
 * @file handler_utils.h
 * @brief Handler-level utility functions for safe input parsing and error responses
 *
 * Provides:
 *   - safeStoi(): integer parsing with try-catch + bounds clamping
 *   - internalError(): sanitized 500 response (logs real error, returns generic message)
 *   - badRequest(): sanitized 400 response
 *
 * Prevents:
 *   - Exception message leakage to API clients (e.what() disclosure)
 *   - std::stoi() crash on invalid/out-of-range input
 *   - Unbounded pagination parameters
 *
 * @date 2026-03-01
 */

namespace common::handler {

/**
 * Safe string-to-int parsing with bounds clamping.
 * Returns defaultValue on empty/invalid input. Clamps to [minVal, maxVal].
 */
inline int safeStoi(const std::string& str, int defaultValue,
                    int minVal = 0, int maxVal = 100000) {
    if (str.empty()) return defaultValue;
    try {
        int val = std::stoi(str);
        return std::clamp(val, minVal, maxVal);
    } catch (...) {
        return defaultValue;
    }
}

/**
 * Create sanitized 500 Internal Server Error response.
 * Logs real exception details server-side; returns generic message to client.
 */
inline drogon::HttpResponsePtr internalError(
    const std::string& logContext, const std::exception& e) {
    spdlog::error("[{}] {}", logContext, e.what());
    Json::Value body;
    body["success"] = false;
    body["error"] = "Internal server error";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(drogon::k500InternalServerError);
    return resp;
}

/**
 * Create sanitized 400 Bad Request response.
 */
inline drogon::HttpResponsePtr badRequest(const std::string& publicMessage) {
    Json::Value body;
    body["success"] = false;
    body["error"] = publicMessage;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(drogon::k400BadRequest);
    return resp;
}

} // namespace common::handler

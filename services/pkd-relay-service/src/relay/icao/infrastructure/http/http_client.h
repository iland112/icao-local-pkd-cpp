/**
 * @file http_client.h
 * @brief HTTP client for fetching external resources
 */
#pragma once

#include <string>
#include <optional>
#include <drogon/HttpClient.h>
#include <trantor/net/EventLoop.h>

namespace infrastructure {
namespace http {

/**
 * @brief Simple HTTP client wrapper for fetching external resources
 *
 * Uses Drogon's built-in HttpClient for consistency with the framework.
 */
class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient() = default;

    /**
     * @brief Fetch HTML content from a URL (synchronous)
     *
     * @param url Full URL to fetch (e.g., "https://pkddownloadsg.icao.int/")
     * @param timeoutSeconds Timeout in seconds (default: 10)
     * @return HTML content if successful, std::nullopt on failure
     */
    std::optional<std::string> fetchHtml(const std::string& url, int timeoutSeconds = 10);

private:
    /**
     * @brief Extract host from URL for HttpClient creation
     */
    std::string extractHost(const std::string& url);

    /**
     * @brief Extract path from URL
     */
    std::string extractPath(const std::string& url);
};

} // namespace http
} // namespace infrastructure

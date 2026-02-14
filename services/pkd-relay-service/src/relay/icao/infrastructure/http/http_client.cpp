/**
 * @file http_client.cpp
 * @brief HTTP client implementation
 */
#include "http_client.h"
#include <spdlog/spdlog.h>
#include <regex>
#include <thread>
#include <chrono>
#include <future>

namespace infrastructure {
namespace http {

std::optional<std::string> HttpClient::fetchHtml(const std::string& url, int timeoutSeconds) {
    spdlog::info("[HttpClient] Fetching URL: {}", url);

    std::string host = extractHost(url);
    std::string path = extractPath(url);

    if (host.empty() || path.empty()) {
        spdlog::error("[HttpClient] Invalid URL: {}", url);
        return std::nullopt;
    }

    spdlog::debug("[HttpClient] Host: {}, Path: {}", host, path);

    // Create HTTP client
    auto client = drogon::HttpClient::newHttpClient(host);

    // Create request
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath(path);
    req->setMethod(drogon::Get);

    // Add headers to mimic browser behavior
    req->addHeader("User-Agent", "Mozilla/5.0 (compatible; ICAO-Local-PKD/1.7.0)");
    req->addHeader("Accept", "text/html,application/xhtml+xml");

    // Use promise/future for synchronous behavior
    std::promise<std::optional<std::string>> promise;
    auto future = promise.get_future();

    // Send request asynchronously
    client->sendRequest(req, [&promise](drogon::ReqResult result,
                                         const drogon::HttpResponsePtr& response) {
        if (result == drogon::ReqResult::Ok && response) {
            if (response->getStatusCode() == drogon::k200OK) {
                std::string html = std::string(response->getBody());
                spdlog::info("[HttpClient] Successfully fetched HTML ({} bytes)",
                           html.size());
                promise.set_value(html);
            } else {
                spdlog::error("[HttpClient] HTTP error: {}",
                            static_cast<int>(response->getStatusCode()));
                promise.set_value(std::nullopt);
            }
        } else {
            spdlog::error("[HttpClient] Request failed: {}",
                        static_cast<int>(result));
            promise.set_value(std::nullopt);
        }
    });

    // Wait for response with timeout
    if (future.wait_for(std::chrono::seconds(timeoutSeconds + 5)) == std::future_status::timeout) {
        spdlog::error("[HttpClient] Request timed out after {} seconds", timeoutSeconds);
        return std::nullopt;
    }

    return future.get();
}

std::string HttpClient::extractHost(const std::string& url) {
    // Regex to extract host from URL
    // Matches: http://host, https://host, http://host:port, https://host:port
    std::regex hostRegex(R"(^https?://([^/:]+(?::\d+)?))");
    std::smatch match;

    if (std::regex_search(url, match, hostRegex)) {
        return "https://" + match.str(1);  // Return full protocol + host
    }

    return "";
}

std::string HttpClient::extractPath(const std::string& url) {
    // Extract path from URL (everything after host:port)
    std::regex pathRegex(R"(^https?://[^/]+(/.*)?)");
    std::smatch match;

    if (std::regex_search(url, match, pathRegex)) {
        std::string path = match.str(1);
        return path.empty() ? "/" : path;
    }

    return "/";
}

} // namespace http
} // namespace infrastructure

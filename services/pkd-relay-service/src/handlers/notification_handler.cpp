#include "handlers/notification_handler.h"
#include "common/notification_manager.h"
#include <spdlog/spdlog.h>
#include <memory>

namespace handlers {

void NotificationHandler::handleStream(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/sync/notifications/stream - SSE notification stream");

    auto resp = drogon::HttpResponse::newAsyncStreamResponse(
        [](drogon::ResponseStreamPtr streamPtr) {
            // Convert unique_ptr to shared_ptr for lambda capture
            auto stream = std::shared_ptr<drogon::ResponseStream>(streamPtr.release());

            // Send initial connection event
            stream->send("event: connected\ndata: {\"message\":\"Notification stream connected\"}\n\n");

            // Register this client for broadcasts
            auto clientId = icao::relay::notification::NotificationManager::getInstance().registerClient(
                [stream, weak = std::weak_ptr<drogon::ResponseStream>(stream)](const std::string& data) {
                    // Check if stream is still alive
                    if (auto s = weak.lock()) {
                        try {
                            s->send(data);
                        } catch (...) {
                            // Stream closed — will be cleaned up by NotificationManager
                            throw;
                        }
                    }
                });

            spdlog::info("[SSE] Notification client connected: {}", clientId);
        });

    // SSE headers (same as pkd-management progress stream)
    resp->setContentTypeString("text/event-stream; charset=utf-8");
    resp->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    resp->addHeader("Connection", "keep-alive");
    resp->addHeader("X-Accel-Buffering", "no");
    resp->addHeader("Access-Control-Allow-Origin", "*");

    callback(resp);
}

} // namespace handlers

#pragma once

#include <drogon/HttpController.h>
#include <functional>

namespace handlers {

/**
 * NotificationHandler — SSE endpoint for real-time notifications
 *
 * Provides a persistent SSE connection that broadcasts system events
 * (sync check, revalidation, reconciliation, daily sync) to all
 * connected frontend clients.
 */
class NotificationHandler {
public:
    /**
     * GET /api/sync/notifications/stream
     * SSE endpoint — returns text/event-stream response
     */
    void handleStream(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace handlers

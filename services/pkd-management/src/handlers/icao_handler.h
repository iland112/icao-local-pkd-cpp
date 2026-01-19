#pragma once

#include <drogon/drogon.h>
#include <memory>
#include "../services/icao_sync_service.h"

namespace handlers {

/**
 * @brief HTTP handler for ICAO Auto Sync endpoints
 *
 * Registers API routes and delegates requests to IcaoSyncService.
 * Thin layer that converts HTTP requests/responses to/from domain objects.
 */
class IcaoHandler {
public:
    explicit IcaoHandler(std::shared_ptr<services::IcaoSyncService> service);
    ~IcaoHandler();

    /**
     * @brief Register all ICAO-related routes with Drogon app
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    std::shared_ptr<services::IcaoSyncService> service_;

    /**
     * @brief GET /api/icao/check-updates
     *
     * Manual trigger for version checking (also used by cron job).
     * Returns list of newly detected versions.
     */
    void handleCheckUpdates(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/icao/latest
     *
     * Get latest detected version for each collection type.
     */
    void handleGetLatest(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/icao/history?limit=N
     *
     * Get version detection history (most recent first).
     */
    void handleGetHistory(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        int limit);

    /**
     * @brief Helper to convert IcaoVersion to JSON
     */
    Json::Value versionToJson(const domain::models::IcaoVersion& version);
};

} // namespace handlers

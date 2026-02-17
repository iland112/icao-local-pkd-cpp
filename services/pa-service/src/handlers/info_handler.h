#pragma once

#include <drogon/HttpAppFramework.h>
#include <json/json.h>
#include <string>

namespace handlers {

/**
 * @brief Information and documentation endpoints handler
 *
 * Provides informational API endpoints:
 * - GET / - Root service info
 * - GET /api - API overview with endpoint listing
 * - GET /api/docs - Swagger UI redirect
 * - GET /api/openapi.yaml - OpenAPI specification
 *
 * No external dependencies required.
 */
class InfoHandler {
public:
    /**
     * @brief Construct InfoHandler
     *
     * No dependencies required.
     */
    InfoHandler();

    /**
     * @brief Register info routes
     *
     * Registers all informational endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    /**
     * @brief GET /
     *
     * Returns basic service information and available endpoints.
     *
     * Response:
     * {
     *   "name": "PA Service",
     *   "description": "ICAO Passive Authentication Service",
     *   "version": "2.1.1",
     *   "endpoints": {
     *     "health": "/api/health",
     *     "pa": "/api/pa"
     *   }
     * }
     */
    void handleRoot(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api
     *
     * Returns API overview with available endpoint listing.
     */
    void handleApiInfo(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/openapi.yaml
     *
     * Returns OpenAPI 3.0 specification in YAML format.
     */
    void handleOpenApiSpec(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/docs
     *
     * Redirects to Swagger UI.
     */
    void handleDocs(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace handlers

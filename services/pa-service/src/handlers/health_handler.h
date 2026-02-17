#pragma once

#include <drogon/HttpAppFramework.h>
#include <json/json.h>
#include <functional>

namespace handlers {

/**
 * @brief Health check endpoints handler
 *
 * Provides health-related API endpoints:
 * - GET /api/health - Application health check
 * - GET /api/health/database - Database connectivity check
 * - GET /api/health/ldap - LDAP connectivity check
 *
 * Health check functions are injected as std::function to decouple
 * from the global checkDatabase()/checkLdap() implementations.
 */
class HealthHandler {
public:
    /**
     * @brief Construct HealthHandler
     *
     * @param checkDatabase Function that returns database health as Json::Value
     * @param checkLdap Function that returns LDAP health as Json::Value
     * @param getCurrentTimestamp Function that returns current timestamp string
     */
    explicit HealthHandler(
        std::function<Json::Value()> checkDatabase,
        std::function<Json::Value()> checkLdap,
        std::function<std::string()> getCurrentTimestamp);

    /**
     * @brief Register health check routes
     *
     * Registers all health endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    std::function<Json::Value()> checkDatabase_;
    std::function<Json::Value()> checkLdap_;
    std::function<std::string()> getCurrentTimestamp_;

    /**
     * @brief GET /api/health
     *
     * Returns basic service health status.
     *
     * Response:
     * {
     *   "service": "pa-service",
     *   "status": "UP",
     *   "version": "2.1.1",
     *   "timestamp": "2026-02-17T10:00:00"
     * }
     */
    void handleHealth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/health/database
     *
     * Returns database connectivity status with response time.
     *
     * Response:
     * {
     *   "name": "database",
     *   "status": "UP",
     *   "responseTimeMs": 5,
     *   "version": "PostgreSQL 15.x"
     * }
     */
    void handleDatabaseHealth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/health/ldap
     *
     * Returns LDAP connectivity status with response time.
     *
     * Response:
     * {
     *   "name": "ldap",
     *   "status": "UP",
     *   "responseTimeMs": 3,
     *   "uri": "ldap://haproxy:389"
     * }
     */
    void handleLdapHealth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace handlers

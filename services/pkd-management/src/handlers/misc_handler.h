#pragma once

#include <drogon/HttpController.h>
#include "../services/audit_service.h"
#include "../services/validation_service.h"
#include <functional>
#include <json/json.h>

namespace handlers {

/**
 * @brief Miscellaneous endpoints handler
 *
 * Provides various API endpoints that don't belong to a specific domain:
 * - GET /api/audit/operations - List audit log entries
 * - GET /api/audit/operations/stats - Audit log statistics
 * - GET /api/health - Application health check
 * - GET /api/health/database - Database health check
 * - GET /api/health/ldap - LDAP health check
 * - POST /api/validation/revalidate - Re-validate DSC certificates
 * - GET /api/pa/statistics - PA statistics (mock)
 * - POST /api/pa/verify - PA verification (mock)
 * - GET /api/ldap/health - LDAP health (for frontend Dashboard)
 * - GET /api/pa/history - PA history (mock)
 * - GET / - Root info endpoint
 * - GET /api - API info endpoint
 * - GET /api/openapi.yaml - OpenAPI specification
 *
 * Uses non-owning pointers and function callbacks for dependencies.
 */
class MiscHandler {
public:
    /**
     * @brief Construct MiscHandler
     *
     * @param auditService Audit service (non-owning pointer)
     * @param validationService Validation service (non-owning pointer)
     * @param checkDatabase Function returning database health as Json::Value
     * @param checkLdap Function returning LDAP health as Json::Value
     */
    explicit MiscHandler(
        services::AuditService* auditService,
        services::ValidationService* validationService,
        std::function<Json::Value()> checkDatabase,
        std::function<Json::Value()> checkLdap);

    /**
     * @brief Register miscellaneous routes
     *
     * Registers all misc endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    services::AuditService* auditService_;
    services::ValidationService* validationService_;
    std::function<Json::Value()> checkDatabase_;
    std::function<Json::Value()> checkLdap_;

    /**
     * @brief GET /api/audit/operations - List audit log entries with filtering
     */
    void handleGetOperationLogs(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/audit/operations/stats - Audit log statistics
     */
    void handleGetOperationStats(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/health - Application health check
     */
    void handleHealth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/health/database - Database health check
     */
    void handleHealthDatabase(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/health/ldap - LDAP health check
     */
    void handleHealthLdap(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/validation/revalidate - Re-validate DSC certificates
     */
    void handleRevalidate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/pa/statistics - PA statistics (mock response)
     */
    void handlePaStatistics(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/pa/verify - PA verification (mock response)
     */
    void handlePaVerify(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/ldap/health - LDAP health check (for frontend Dashboard)
     */
    void handleLdapHealth(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/pa/history - PA history (mock response)
     */
    void handlePaHistory(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET / - Root info endpoint
     */
    void handleRoot(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api - API info endpoint
     */
    void handleApiInfo(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/openapi.yaml - OpenAPI specification
     */
    void handleOpenApiSpec(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace handlers

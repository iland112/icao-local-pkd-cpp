#pragma once

/**
 * @file health_handler.h
 * @brief Health check endpoint handler for PKD Relay Service
 *
 * @date 2026-02-17
 */

#include <drogon/drogon.h>
#include "i_query_executor.h"

namespace handlers {

/**
 * @brief Handler for service health check endpoint
 *
 * Checks database connectivity and returns service status.
 */
class HealthHandler {
public:
    /**
     * @brief Constructor with dependency injection
     * @param queryExecutor Database query executor (non-owning)
     */
    explicit HealthHandler(common::IQueryExecutor* queryExecutor);

    /**
     * @brief Handle GET /api/sync/health
     */
    void handle(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    common::IQueryExecutor* queryExecutor_;
};

} // namespace handlers

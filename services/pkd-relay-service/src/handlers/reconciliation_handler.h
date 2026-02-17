#pragma once

/**
 * @file reconciliation_handler.h
 * @brief Reconciliation endpoint handlers for PKD Relay Service
 *
 * Handles reconciliation trigger, history, and detail queries.
 *
 * @date 2026-02-17
 */

#include <drogon/drogon.h>
#include "i_query_executor.h"
#include <ldap_connection_pool.h>

// Forward declarations
namespace icao::relay {
    struct Config;
    namespace services {
        class ReconciliationService;
    }
}

namespace handlers {

/**
 * @brief Handler for reconciliation-related endpoints
 *
 * Manages DB-LDAP reconciliation operations including
 * triggering, history, and detail views.
 */
class ReconciliationHandler {
public:
    /**
     * @brief Constructor with dependency injection
     * @param reconciliationService Reconciliation service (non-owning)
     * @param queryExecutor Database query executor (non-owning)
     * @param ldapPool LDAP connection pool (non-owning)
     * @param config Service configuration reference
     */
    ReconciliationHandler(icao::relay::services::ReconciliationService* reconciliationService,
                          common::IQueryExecutor* queryExecutor,
                          common::LdapConnectionPool* ldapPool,
                          icao::relay::Config& config);

    /** @brief POST /api/sync/reconcile - Trigger reconciliation */
    void handleReconcile(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/reconcile/history - Get reconciliation history */
    void handleReconciliationHistory(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/reconcile/{id} - Get reconciliation details */
    void handleReconciliationDetails(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/reconcile/stats - Get reconciliation statistics */
    void handleReconciliationStats(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    icao::relay::services::ReconciliationService* reconciliationService_;
    common::IQueryExecutor* queryExecutor_;
    common::LdapConnectionPool* ldapPool_;
    icao::relay::Config& config_;
};

} // namespace handlers

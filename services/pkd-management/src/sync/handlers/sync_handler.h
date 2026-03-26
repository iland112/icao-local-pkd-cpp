#pragma once

/**
 * @file sync_handler.h
 * @brief Sync operation endpoint handlers for PKD Relay Service
 *
 * Handles sync status, history, check, discrepancies, config, and revalidation.
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
        class SyncService;
        class ValidationService;
    }
}

namespace infrastructure {
    class SyncScheduler;
}

namespace handlers {

/**
 * @brief Handler for sync-related endpoints
 *
 * Manages sync status queries, manual sync triggers,
 * config management, and certificate revalidation.
 */
class SyncHandler {
public:
    /**
     * @brief Constructor with dependency injection
     * @param syncService Sync service (non-owning)
     * @param validationService Validation service (non-owning)
     * @param queryExecutor Database query executor (non-owning)
     * @param ldapPool LDAP connection pool (non-owning)
     * @param config Service configuration reference
     * @param scheduler Sync scheduler reference
     */
    SyncHandler(icao::relay::services::SyncService* syncService,
                icao::relay::services::ValidationService* validationService,
                common::IQueryExecutor* queryExecutor,
                common::LdapConnectionPool* ldapPool,
                icao::relay::Config& config,
                infrastructure::SyncScheduler& scheduler);

    /** @brief GET /api/sync/status - Get latest sync status */
    void handleSyncStatus(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/history - Get sync history (paginated) */
    void handleSyncHistory(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief POST /api/sync/check - Trigger manual sync check */
    void handleSyncCheck(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/discrepancies - Get unresolved discrepancies */
    void handleDiscrepancies(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/config - Get sync configuration */
    void handleSyncConfig(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief PUT /api/sync/config - Update sync configuration */
    void handleUpdateSyncConfig(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief POST /api/sync/revalidate - Trigger manual certificate re-validation */
    void handleRevalidate(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/revalidation-history - Get re-validation history */
    void handleRevalidationHistory(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief POST /api/sync/trigger-daily - Trigger daily sync manually */
    void handleTriggerDailySync(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** @brief GET /api/sync/stats - Get sync statistics */
    void handleSyncStats(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    icao::relay::services::SyncService* syncService_;
    icao::relay::services::ValidationService* validationService_;
    common::IQueryExecutor* queryExecutor_;
    common::LdapConnectionPool* ldapPool_;
    icao::relay::Config& config_;
    infrastructure::SyncScheduler& scheduler_;
};

} // namespace handlers

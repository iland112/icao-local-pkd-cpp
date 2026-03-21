/**
 * @file icao_ldap_handler.h
 * @brief REST API handler for ICAO PKD LDAP synchronization
 */
#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpAppFramework.h>

namespace icao {
namespace relay {

class IcaoLdapSyncService;  // Forward declaration

class IcaoLdapHandler {
public:
    explicit IcaoLdapHandler(IcaoLdapSyncService* syncService);

    /// Register routes with Drogon framework
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    /// POST /api/sync/icao-ldap/trigger — Trigger manual sync
    void handleTriggerSync(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/sync/icao-ldap/status — Current sync status
    void handleGetStatus(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/sync/icao-ldap/history — Sync history
    void handleGetHistory(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/sync/icao-ldap/config — Get config
    void handleGetConfig(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// PUT /api/sync/icao-ldap/config — Update config
    void handleUpdateConfig(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    IcaoLdapSyncService* syncService_;
};

} // namespace relay
} // namespace icao

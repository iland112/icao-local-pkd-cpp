/**
 * @file icao_handler_factory.cpp
 * @brief Factory for ICAO version detection handler
 *
 * Separated from main.cpp to avoid namespace ambiguity between
 * icao::relay::repositories:: and upload module's repositories::.
 */

#include "upload/handlers/icao_handler.h"
#include "upload/services/icao_sync_service.h"
#include "upload/repositories/icao_version_repository.h"
#include "upload/infrastructure/http/http_client.h"
#include "i_query_executor.h"
#include <cstdlib>
#include <memory>

std::unique_ptr<handlers::IcaoHandler> createIcaoHandler(common::IQueryExecutor* qe) {
    auto repo = std::make_shared<repositories::IcaoVersionRepository>(qe);
    auto http = std::make_shared<infrastructure::http::HttpClient>();
    services::IcaoSyncService::Config cfg;
    if (auto e = std::getenv("ICAO_PORTAL_URL")) cfg.icaoPortalUrl = e;
    if (auto e = std::getenv("ICAO_HTTP_TIMEOUT")) cfg.httpTimeoutSeconds = std::stoi(e);
    auto svc = std::make_shared<services::IcaoSyncService>(repo, http, cfg);
    return std::make_unique<handlers::IcaoHandler>(svc);
}

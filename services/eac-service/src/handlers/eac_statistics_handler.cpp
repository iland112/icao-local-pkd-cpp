/**
 * @file eac_statistics_handler.cpp
 * @brief CVC certificate statistics and country list handlers
 */

#include "handlers/eac_statistics_handler.h"
#include "infrastructure/service_container.h"
#include "repositories/cvc_certificate_repository.h"

namespace eac::handlers {

EacStatisticsHandler::EacStatisticsHandler(infrastructure::ServiceContainer* services)
    : services_(services) {}

void EacStatisticsHandler::handleStatistics(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto stats = services_->cvcCertificateRepository()->getStatistics();

    Json::Value response;
    response["success"] = true;
    response["statistics"] = stats;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

void EacStatisticsHandler::handleCountries(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto countries = services_->cvcCertificateRepository()->getCountryList();

    Json::Value response;
    response["success"] = true;
    response["countries"] = countries;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

} // namespace eac::handlers

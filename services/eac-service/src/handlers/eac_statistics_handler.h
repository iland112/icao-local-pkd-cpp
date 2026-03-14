#pragma once

#include <drogon/HttpController.h>
#include <functional>

namespace eac::infrastructure { class ServiceContainer; }

namespace eac::handlers {

class EacStatisticsHandler {
public:
    explicit EacStatisticsHandler(infrastructure::ServiceContainer* services);

    void handleStatistics(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleCountries(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    infrastructure::ServiceContainer* services_;
};

} // namespace eac::handlers

#pragma once

#include <drogon/HttpController.h>
#include <functional>

namespace eac::infrastructure { class ServiceContainer; }

namespace eac::handlers {

class EacCertificateHandler {
public:
    explicit EacCertificateHandler(infrastructure::ServiceContainer* services);

    void handleSearch(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleDetail(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id);

    void handleDelete(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id);

    void handleChain(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id);

private:
    infrastructure::ServiceContainer* services_;
};

} // namespace eac::handlers

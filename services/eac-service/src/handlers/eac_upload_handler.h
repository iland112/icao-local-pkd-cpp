#pragma once

#include <drogon/HttpController.h>
#include <functional>

namespace eac::infrastructure { class ServiceContainer; }

namespace eac::handlers {

class EacUploadHandler {
public:
    explicit EacUploadHandler(infrastructure::ServiceContainer* services);

    void handleUpload(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handlePreview(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    infrastructure::ServiceContainer* services_;
};

} // namespace eac::handlers

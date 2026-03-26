#pragma once

/**
 * @file upload_handler.h
 * @brief Individual certificate upload handler
 *
 * Provides certificate upload endpoints (LDIF/ML upload moved to pkd-relay v2.41.0):
 * - POST /api/upload/certificate            - Upload individual certificate
 * - POST /api/upload/certificate/preview    - Preview certificate (parse only)
 */

#include <drogon/drogon.h>
#include <string>

namespace services { class UploadService; }
namespace common { class IQueryExecutor; }

namespace handlers {

class UploadHandler {
public:
    UploadHandler(services::UploadService* uploadService,
                  common::IQueryExecutor* queryExecutor);

    void registerRoutes(drogon::HttpAppFramework& app);

private:
    services::UploadService* uploadService_;
    common::IQueryExecutor* queryExecutor_;

    static constexpr int64_t MAX_CERT_FILE_SIZE = 10LL * 1024 * 1024;  // 10MB

    void handleUploadCertificate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handlePreviewCertificate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace handlers

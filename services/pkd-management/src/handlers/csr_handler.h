#pragma once

#include <drogon/HttpController.h>
#include <functional>
#include <string>

namespace services {
    class CsrService;
}

namespace common {
    class IQueryExecutor;
}

namespace handlers {

class CsrHandler {
public:
    CsrHandler(services::CsrService* csrService,
               common::IQueryExecutor* queryExecutor);
    ~CsrHandler() = default;

    void registerRoutes(drogon::HttpAppFramework& app);

private:
    void handleGenerate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleList(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleGetById(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    void handleExportPem(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    void handleExportDer(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    void handleImport(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleRegisterCertificate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    void handleDelete(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    services::CsrService* csrService_;
    common::IQueryExecutor* queryExecutor_;
};

} // namespace handlers

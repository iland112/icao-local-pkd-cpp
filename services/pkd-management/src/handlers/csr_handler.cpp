#include "csr_handler.h"
#include "../services/csr_service.h"
#include "handler_utils.h"
#include <spdlog/spdlog.h>

namespace handlers {

CsrHandler::CsrHandler(services::CsrService* csrService,
                        common::IQueryExecutor* queryExecutor)
    : csrService_(csrService), queryExecutor_(queryExecutor)
{
}

void CsrHandler::registerRoutes(drogon::HttpAppFramework& app)
{
    // POST /api/csr/generate
    app.registerHandler(
        "/api/csr/generate",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGenerate(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/csr
    app.registerHandler(
        "/api/csr",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleList(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/csr/{id}
    app.registerHandler(
        "/api/csr/{id}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleGetById(req, std::move(callback), id);
        },
        {drogon::Get}
    );

    // GET /api/csr/{id}/export/pem
    app.registerHandler(
        "/api/csr/{id}/export/pem",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleExportPem(req, std::move(callback), id);
        },
        {drogon::Get}
    );

    // GET /api/csr/{id}/export/der
    app.registerHandler(
        "/api/csr/{id}/export/der",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleExportDer(req, std::move(callback), id);
        },
        {drogon::Get}
    );

    // DELETE /api/csr/{id}
    app.registerHandler(
        "/api/csr/{id}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleDelete(req, std::move(callback), id);
        },
        {drogon::Delete}
    );

    spdlog::info("CsrHandler routes registered (6 endpoints)");
}

// POST /api/csr/generate
void CsrHandler::handleGenerate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    try {
        auto json = req->getJsonObject();
        if (!json) {
            Json::Value err;
            err["success"] = false;
            err["error"] = "Invalid JSON body";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        services::CsrGenerateRequest csrReq;
        csrReq.countryCode = (*json).get("countryCode", "").asString();
        csrReq.organization = (*json).get("organization", "").asString();
        csrReq.commonName = (*json).get("commonName", "").asString();
        csrReq.memo = (*json).get("memo", "").asString();

        // Extract username from JWT (set by auth middleware)
        csrReq.createdBy = req->getHeader("X-User-Name");
        if (csrReq.createdBy.empty()) {
            csrReq.createdBy = "system";
        }

        // Validate: at least one DN field
        if (csrReq.countryCode.empty() && csrReq.organization.empty() && csrReq.commonName.empty()) {
            Json::Value err;
            err["success"] = false;
            err["error"] = "At least one Subject DN field is required (countryCode, organization, or commonName)";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto result = csrService_->generate(csrReq);

        Json::Value response;
        if (result.success) {
            response["success"] = true;
            response["data"]["id"] = result.id;
            response["data"]["subjectDn"] = result.subjectDn;
            response["data"]["csrPem"] = result.csrPem;
            response["data"]["publicKeyFingerprint"] = result.publicKeyFingerprint;
            response["data"]["keyAlgorithm"] = "RSA-2048";
            response["data"]["signatureAlgorithm"] = "SHA256withRSA";
        } else {
            response["success"] = false;
            response["error"] = result.errorMessage;
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        if (!result.success) resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CsrHandler::generate", e));
    }
}

// GET /api/csr
void CsrHandler::handleList(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    try {
        int page = 1, pageSize = 20;
        auto pageParam = req->getParameter("page");
        auto sizeParam = req->getParameter("pageSize");
        auto statusParam = req->getParameter("status");

        if (!pageParam.empty()) {
            try { page = std::max(1, std::stoi(pageParam)); } catch (...) {}
        }
        if (!sizeParam.empty()) {
            try { pageSize = std::max(1, std::min(100, std::stoi(sizeParam))); } catch (...) {}
        }

        Json::Value response = csrService_->list(page, pageSize, statusParam);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CsrHandler::list", e));
    }
}

// GET /api/csr/{id}
void CsrHandler::handleGetById(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id)
{
    try {
        Json::Value data = csrService_->getById(id);
        if (data.isNull()) {
            Json::Value err;
            err["success"] = false;
            err["error"] = "CSR not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        Json::Value response;
        response["success"] = true;
        response["data"] = data;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CsrHandler::getById", e));
    }
}

// GET /api/csr/{id}/export/pem
void CsrHandler::handleExportPem(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id)
{
    try {
        std::string pem = csrService_->getPemById(id);
        if (pem.empty()) {
            Json::Value err;
            err["success"] = false;
            err["error"] = "CSR not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(pem);
        resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
        resp->addHeader("Content-Disposition", "attachment; filename=\"request.csr\"");
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CsrHandler::exportPem", e));
    }
}

// GET /api/csr/{id}/export/der
void CsrHandler::handleExportDer(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id)
{
    try {
        std::vector<uint8_t> der = csrService_->getDerById(id);
        if (der.empty()) {
            Json::Value err;
            err["success"] = false;
            err["error"] = "CSR not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(std::string(reinterpret_cast<const char*>(der.data()), der.size()));
        resp->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);
        resp->addHeader("Content-Disposition", "attachment; filename=\"request.der\"");
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CsrHandler::exportDer", e));
    }
}

// DELETE /api/csr/{id}
void CsrHandler::handleDelete(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id)
{
    try {
        // Check existence first
        Json::Value existing = csrService_->getById(id);
        if (existing.isNull()) {
            Json::Value err;
            err["success"] = false;
            err["error"] = "CSR not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        bool deleted = csrService_->deleteById(id);

        Json::Value response;
        response["success"] = deleted;
        if (!deleted) response["error"] = "Failed to delete CSR";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CsrHandler::delete", e));
    }
}

} // namespace handlers

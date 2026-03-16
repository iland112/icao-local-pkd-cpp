#include "csr_handler.h"
#include "../services/csr_service.h"
#include "handler_utils.h"
#include <icao/audit/audit_log.h>
#include <spdlog/spdlog.h>

namespace handlers {

CsrHandler::CsrHandler(services::CsrService* csrService,
                        common::IQueryExecutor* queryExecutor)
    : csrService_(csrService), queryExecutor_(queryExecutor)
{
}

// Helper: create audit entry from request with full context (user, IP, path, etc.)
static icao::audit::AuditLogEntry makeAudit(const drogon::HttpRequestPtr& req,
                                             icao::audit::OperationType opType) {
    return icao::audit::createAuditEntryFromRequest(req, opType);
}

static void audit(common::IQueryExecutor* qe, const drogon::HttpRequestPtr& req,
                  icao::audit::OperationType opType, const std::string& resourceId,
                  bool success = true, const std::string& errorMsg = "",
                  const Json::Value& meta = Json::nullValue) {
    try {
        auto entry = makeAudit(req, opType);
        entry.resourceId = resourceId;
        entry.success = success;
        if (!errorMsg.empty()) entry.errorMessage = errorMsg;
        if (!meta.isNull()) entry.metadata = meta;
        icao::audit::logOperation(qe, entry);
    } catch (...) {
        spdlog::warn("[CsrHandler] Audit log failed");
    }
}

void CsrHandler::registerRoutes(drogon::HttpAppFramework& app)
{
    app.registerHandler("/api/csr/generate",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleGenerate(req, std::move(cb)); }, {drogon::Post});

    app.registerHandler("/api/csr/import",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleImport(req, std::move(cb)); }, {drogon::Post});

    app.registerHandler("/api/csr",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) { handleList(req, std::move(cb)); }, {drogon::Get});

    app.registerHandler("/api/csr/{id}",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, const std::string& id) { handleGetById(req, std::move(cb), id); }, {drogon::Get});

    app.registerHandler("/api/csr/{id}/export/pem",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, const std::string& id) { handleExportPem(req, std::move(cb), id); }, {drogon::Get});

    app.registerHandler("/api/csr/{id}/certificate",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, const std::string& id) { handleRegisterCertificate(req, std::move(cb), id); }, {drogon::Post});

    app.registerHandler("/api/csr/{id}",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb, const std::string& id) { handleDelete(req, std::move(cb), id); }, {drogon::Delete});

    spdlog::info("CsrHandler routes registered (7 endpoints)");
}

void CsrHandler::handleGenerate(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    try {
        auto json = req->getJsonObject();
        if (!json) { auto r = drogon::HttpResponse::newHttpJsonResponse(Json::Value()); r->setStatusCode(drogon::k400BadRequest); callback(r); return; }

        services::CsrGenerateRequest csrReq;
        csrReq.countryCode = (*json).get("countryCode", "").asString();
        csrReq.organization = (*json).get("organization", "").asString();
        csrReq.commonName = (*json).get("commonName", "").asString();
        csrReq.memo = (*json).get("memo", "").asString();
        auto [userId, username] = icao::audit::extractUserFromRequest(req);
        csrReq.createdBy = username.value_or("system");

        if (csrReq.countryCode.empty() && csrReq.organization.empty() && csrReq.commonName.empty()) {
            Json::Value err; err["success"] = false; err["error"] = "At least one Subject DN field is required";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err); r->setStatusCode(drogon::k400BadRequest); callback(r); return;
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
            Json::Value meta; meta["subjectDn"] = result.subjectDn; meta["fingerprint"] = result.publicKeyFingerprint;
            audit(queryExecutor_, req, icao::audit::OperationType::CSR_GENERATE, result.id, true, "", meta);
        } else {
            response["success"] = false; response["error"] = result.errorMessage;
            audit(queryExecutor_, req, icao::audit::OperationType::CSR_GENERATE, "", false, result.errorMessage);
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        if (!result.success) resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    } catch (const std::exception& e) { callback(common::handler::internalError("CsrHandler::generate", e)); }
}

void CsrHandler::handleImport(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    try {
        auto json = req->getJsonObject();
        if (!json) { auto r = drogon::HttpResponse::newHttpJsonResponse(Json::Value()); r->setStatusCode(drogon::k400BadRequest); callback(r); return; }

        std::string csrPem = (*json).get("csrPem", "").asString();
        std::string privateKeyPem = (*json).get("privateKeyPem", "").asString();
        std::string memo = (*json).get("memo", "").asString();
        if (csrPem.empty() || privateKeyPem.empty()) {
            Json::Value err; err["success"] = false; err["error"] = "csrPem and privateKeyPem fields are required";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err); r->setStatusCode(drogon::k400BadRequest); callback(r); return;
        }

        auto [userId, username] = icao::audit::extractUserFromRequest(req);
        auto result = csrService_->importCsr(csrPem, privateKeyPem, memo, username.value_or("system"));

        Json::Value response;
        if (result.success) {
            response["success"] = true;
            response["data"]["id"] = result.id;
            response["data"]["subjectDn"] = result.subjectDn;
            response["data"]["publicKeyFingerprint"] = result.publicKeyFingerprint;
            Json::Value meta; meta["subjectDn"] = result.subjectDn; meta["source"] = "IMPORT";
            audit(queryExecutor_, req, icao::audit::OperationType::CSR_GENERATE, result.id, true, "", meta);
        } else {
            response["success"] = false; response["error"] = result.errorMessage;
            audit(queryExecutor_, req, icao::audit::OperationType::CSR_GENERATE, "", false, result.errorMessage);
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        if (!result.success) resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
    } catch (const std::exception& e) { callback(common::handler::internalError("CsrHandler::import", e)); }
}

void CsrHandler::handleList(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    try {
        int page = 1, pageSize = 20;
        try { if (!req->getParameter("page").empty()) page = std::max(1, std::stoi(req->getParameter("page"))); } catch (...) {}
        try { if (!req->getParameter("pageSize").empty()) pageSize = std::max(1, std::min(100, std::stoi(req->getParameter("pageSize")))); } catch (...) {}
        callback(drogon::HttpResponse::newHttpJsonResponse(csrService_->list(page, pageSize, req->getParameter("status"))));
    } catch (const std::exception& e) { callback(common::handler::internalError("CsrHandler::list", e)); }
}

void CsrHandler::handleGetById(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id)
{
    try {
        Json::Value data = csrService_->getById(id);
        if (data.isNull()) {
            Json::Value err; err["success"] = false; err["error"] = "CSR not found";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err); r->setStatusCode(drogon::k404NotFound); callback(r); return;
        }
        Json::Value response; response["success"] = true; response["data"] = data;
        audit(queryExecutor_, req, icao::audit::OperationType::CSR_VIEW, id);
        callback(drogon::HttpResponse::newHttpJsonResponse(response));
    } catch (const std::exception& e) { callback(common::handler::internalError("CsrHandler::getById", e)); }
}

void CsrHandler::handleExportPem(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id)
{
    try {
        std::string pem = csrService_->getPemById(id);
        if (pem.empty()) {
            Json::Value err; err["success"] = false; err["error"] = "CSR not found";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err); r->setStatusCode(drogon::k404NotFound); callback(r); return;
        }
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(pem); resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
        resp->addHeader("Content-Disposition", "attachment; filename=\"request.csr\"");
        Json::Value meta; meta["format"] = "PEM";
        audit(queryExecutor_, req, icao::audit::OperationType::CSR_EXPORT, id, true, "", meta);
        callback(resp);
    } catch (const std::exception& e) { callback(common::handler::internalError("CsrHandler::exportPem", e)); }
}

void CsrHandler::handleRegisterCertificate(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id)
{
    try {
        auto json = req->getJsonObject();
        if (!json) { auto r = drogon::HttpResponse::newHttpJsonResponse(Json::Value()); r->setStatusCode(drogon::k400BadRequest); callback(r); return; }
        std::string certPem = (*json).get("certificatePem", "").asString();
        if (certPem.empty()) {
            Json::Value err; err["success"] = false; err["error"] = "certificatePem field is required";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err); r->setStatusCode(drogon::k400BadRequest); callback(r); return;
        }

        auto [userId, username] = icao::audit::extractUserFromRequest(req);
        auto result = csrService_->registerCertificate(id, certPem, username.value_or("system"));

        Json::Value response;
        if (result.success) {
            response["success"] = true;
            response["data"]["id"] = result.id;
            response["data"]["subjectDn"] = result.subjectDn;
            response["data"]["fingerprint"] = result.publicKeyFingerprint;
            Json::Value meta; meta["certSubjectDn"] = result.subjectDn; meta["certFingerprint"] = result.publicKeyFingerprint;
            audit(queryExecutor_, req, icao::audit::OperationType::CSR_GENERATE, id, true, "", meta);
        } else {
            response["success"] = false; response["error"] = result.errorMessage;
            audit(queryExecutor_, req, icao::audit::OperationType::CSR_GENERATE, id, false, result.errorMessage);
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        if (!result.success) resp->setStatusCode(result.errorMessage.find("not found") != std::string::npos ? drogon::k404NotFound : drogon::k400BadRequest);
        callback(resp);
    } catch (const std::exception& e) { callback(common::handler::internalError("CsrHandler::registerCertificate", e)); }
}

void CsrHandler::handleDelete(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id)
{
    try {
        Json::Value existing = csrService_->getById(id);
        if (existing.isNull()) {
            Json::Value err; err["success"] = false; err["error"] = "CSR not found";
            auto r = drogon::HttpResponse::newHttpJsonResponse(err); r->setStatusCode(drogon::k404NotFound); callback(r); return;
        }

        bool deleted = csrService_->deleteById(id);
        Json::Value response; response["success"] = deleted;
        if (!deleted) response["error"] = "Failed to delete CSR";

        Json::Value meta; meta["subjectDn"] = existing.get("subject_dn", "").asString();
        audit(queryExecutor_, req, icao::audit::OperationType::CSR_DELETE, id, deleted, deleted ? "" : "Delete failed", meta);

        callback(drogon::HttpResponse::newHttpJsonResponse(response));
    } catch (const std::exception& e) { callback(common::handler::internalError("CsrHandler::delete", e)); }
}

} // namespace handlers

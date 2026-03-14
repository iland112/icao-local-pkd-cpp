/**
 * @file eac_certificate_handler.cpp
 * @brief CVC certificate search, detail, and chain handlers
 */

#include "handlers/eac_certificate_handler.h"
#include "infrastructure/service_container.h"
#include "repositories/cvc_certificate_repository.h"
#include "services/eac_chain_validator.h"

namespace eac::handlers {

EacCertificateHandler::EacCertificateHandler(infrastructure::ServiceContainer* services)
    : services_(services) {}

void EacCertificateHandler::handleSearch(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    auto country = req->getParameter("country");
    auto type = req->getParameter("type");
    auto status = req->getParameter("status");
    int page = 1, pageSize = 20;

    try { page = std::stoi(req->getParameter("page")); } catch (...) {}
    try { pageSize = std::stoi(req->getParameter("pageSize")); } catch (...) {}
    page = std::max(1, std::min(10000, page));
    pageSize = std::max(1, std::min(100, pageSize));

    auto repo = services_->cvcCertificateRepository();
    auto data = repo->findAll(country, type, status, page, pageSize);
    int total = repo->countAll(country, type, status);

    Json::Value response;
    response["success"] = true;
    response["data"] = data;
    response["total"] = total;
    response["page"] = page;
    response["pageSize"] = pageSize;
    response["totalPages"] = (total + pageSize - 1) / pageSize;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

void EacCertificateHandler::handleDetail(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto cert = services_->cvcCertificateRepository()->findById(id);
    if (!cert) {
        Json::Value err;
        err["success"] = false;
        err["error"] = "Certificate not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(drogon::k404NotFound);
        callback(resp);
        return;
    }

    Json::Value response;
    response["success"] = true;
    response["certificate"]["id"] = cert->id;
    response["certificate"]["cvcType"] = cert->cvcType;
    response["certificate"]["countryCode"] = cert->countryCode;
    response["certificate"]["car"] = cert->car;
    response["certificate"]["chr"] = cert->chr;
    response["certificate"]["chatOid"] = cert->chatOid;
    response["certificate"]["chatRole"] = cert->chatRole;
    response["certificate"]["chatPermissions"] = cert->chatPermissions;
    response["certificate"]["publicKeyOid"] = cert->publicKeyOid;
    response["certificate"]["publicKeyAlgorithm"] = cert->publicKeyAlgorithm;
    response["certificate"]["effectiveDate"] = cert->effectiveDate;
    response["certificate"]["expirationDate"] = cert->expirationDate;
    response["certificate"]["fingerprintSha256"] = cert->fingerprintSha256;
    response["certificate"]["signatureValid"] = cert->signatureValid;
    response["certificate"]["validationStatus"] = cert->validationStatus;
    response["certificate"]["validationMessage"] = cert->validationMessage;
    response["certificate"]["sourceType"] = cert->sourceType;
    response["certificate"]["createdAt"] = cert->createdAt;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

void EacCertificateHandler::handleChain(
    const drogon::HttpRequestPtr& /*req*/,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    auto result = services_->eacChainValidator()->validateChain(id);

    Json::Value response;
    response["success"] = true;
    response["chain"] = result;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

} // namespace eac::handlers

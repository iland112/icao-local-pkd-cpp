/**
 * @file eac_upload_handler.cpp
 * @brief CVC upload and preview handler
 */

#include "handlers/eac_upload_handler.h"
#include "infrastructure/service_container.h"
#include "services/cvc_service.h"

#include <spdlog/spdlog.h>

namespace eac::handlers {

static drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode code, const std::string& msg) {
    Json::Value body;
    body["success"] = false;
    body["error"] = msg;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    return resp;
}

EacUploadHandler::EacUploadHandler(infrastructure::ServiceContainer* services)
    : services_(services) {}

void EacUploadHandler::handleUpload(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    drogon::MultiPartParser parser;
    if (parser.parse(req) != 0 || parser.getFiles().empty()) {
        callback(errorResponse(drogon::k400BadRequest, "No file uploaded"));
        return;
    }

    const auto& file = parser.getFiles()[0];
    std::vector<uint8_t> binary(file.fileContent().begin(), file.fileContent().end());

    if (binary.empty()) {
        callback(errorResponse(drogon::k400BadRequest, "Empty file"));
        return;
    }

    auto result = services_->cvcService()->uploadCvc(binary);
    if (!result) {
        callback(errorResponse(drogon::k400BadRequest, "Failed to parse or save CVC (duplicate?)"));
        return;
    }

    Json::Value response;
    response["success"] = true;
    response["certificate"]["id"] = result->id;
    response["certificate"]["chr"] = result->chr;
    response["certificate"]["car"] = result->car;
    response["certificate"]["cvcType"] = result->cvcType;
    response["certificate"]["countryCode"] = result->countryCode;
    response["certificate"]["fingerprintSha256"] = result->fingerprintSha256;

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

void EacUploadHandler::handlePreview(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    drogon::MultiPartParser parser;
    if (parser.parse(req) != 0 || parser.getFiles().empty()) {
        callback(errorResponse(drogon::k400BadRequest, "No file uploaded"));
        return;
    }

    const auto& file = parser.getFiles()[0];
    std::vector<uint8_t> binary(file.fileContent().begin(), file.fileContent().end());

    auto cert = services_->cvcService()->previewCvc(binary);
    if (!cert) {
        callback(errorResponse(drogon::k400BadRequest, "Failed to parse CVC certificate"));
        return;
    }

    Json::Value response;
    response["success"] = true;
    response["certificate"] = services::CvcService::cvcToJson(*cert);

    callback(drogon::HttpResponse::newHttpJsonResponse(response));
}

} // namespace eac::handlers

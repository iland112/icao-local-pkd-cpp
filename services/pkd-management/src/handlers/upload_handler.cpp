/**
 * @file upload_handler.cpp
 * @brief Individual certificate upload handler implementation
 *
 * LDIF/ML upload moved to pkd-relay (v2.41.0).
 * Only individual certificate upload and preview remain.
 */

#include "upload_handler.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <json/json.h>

#include "../infrastructure/service_container.h"
#include "../services/upload_service.h"

#include <icao/audit/audit_log.h>
#include "handler_utils.h"

using icao::audit::AuditLogEntry;
using icao::audit::OperationType;
using icao::audit::logOperation;
using icao::audit::extractIpAddress;

extern infrastructure::ServiceContainer* g_services;

namespace handlers {

UploadHandler::UploadHandler(
    services::UploadService* uploadService,
    common::IQueryExecutor* queryExecutor)
    : uploadService_(uploadService)
    , queryExecutor_(queryExecutor)
{
    if (!uploadService_) throw std::invalid_argument("uploadService must not be null");
}

void UploadHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // POST /api/upload/certificate
    app.registerHandler(
        "/api/upload/certificate",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleUploadCertificate(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/upload/certificate/preview
    app.registerHandler(
        "/api/upload/certificate/preview",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePreviewCertificate(req, std::move(callback));
        },
        {drogon::Post}
    );

    spdlog::info("[UploadHandler] Registered 2 certificate upload routes");
}

// =============================================================================
// POST /api/upload/certificate
// =============================================================================

void UploadHandler::handleUploadCertificate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    spdlog::info("POST /api/upload/certificate - Individual certificate file upload");

    try {
        drogon::MultiPartParser fileParser;
        if (fileParser.parse(req) != 0) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid multipart form data";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& files = fileParser.getFiles();
        if (files.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "No file uploaded";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& file = files[0];
        std::string fileName = file.getFileName();
        auto fileContent = file.fileContent();
        size_t fileSize = file.fileLength();

        spdlog::info("Certificate file: name={}, size={}", fileName, fileSize);

        if (fileSize > static_cast<size_t>(MAX_CERT_FILE_SIZE)) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "File too large. Maximum size is 10MB for certificate files.";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        std::string uploadedBy = "unknown";
        auto jwtPayload = req->getAttributes()->get<Json::Value>("jwt_payload");
        if (jwtPayload.isMember("username")) {
            uploadedBy = jwtPayload["username"].asString();
        }

        std::vector<uint8_t> contentBytes(fileContent.begin(), fileContent.end());
        auto result = uploadService_->uploadCertificate(fileName, contentBytes, uploadedBy);

        Json::Value response;
        response["success"] = result.success;
        response["message"] = result.message;
        response["uploadId"] = result.uploadId;
        response["fileFormat"] = result.fileFormat;
        response["status"] = result.status;
        response["certificateCount"] = result.certificateCount;
        response["cscaCount"] = result.cscaCount;
        response["dscCount"] = result.dscCount;
        response["dscNcCount"] = result.dscNcCount;
        response["mlscCount"] = result.mlscCount;
        response["crlCount"] = result.crlCount;
        response["ldapStoredCount"] = result.ldapStoredCount;
        response["duplicateCount"] = result.duplicateCount;
        if (!result.errorMessage.empty()) {
            response["errorMessage"] = result.errorMessage;
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        if (result.success) {
            resp->setStatusCode(drogon::k200OK);
        } else if (result.status == "DUPLICATE") {
            resp->setStatusCode(drogon::k409Conflict);
        } else {
            resp->setStatusCode(drogon::k400BadRequest);
        }

        if (queryExecutor_) {
            AuditLogEntry auditEntry;
            auditEntry.username = uploadedBy;
            auditEntry.operationType = OperationType::FILE_UPLOAD;
            auditEntry.operationSubtype = "CERTIFICATE_" + result.fileFormat;
            auditEntry.resourceId = result.uploadId;
            auditEntry.resourceType = "UPLOADED_FILE";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "POST";
            auditEntry.requestPath = "/api/upload/certificate";
            auditEntry.success = result.success;
            Json::Value metadata;
            metadata["fileName"] = fileName;
            metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
            metadata["fileFormat"] = result.fileFormat;
            metadata["certificateCount"] = result.certificateCount;
            metadata["crlCount"] = result.crlCount;
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Certificate upload failed: {}", e.what());
        callback(common::handler::internalError("UploadHandler::uploadCertificate", e));
    }
}

// =============================================================================
// POST /api/upload/certificate/preview
// =============================================================================

void UploadHandler::handlePreviewCertificate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    spdlog::info("POST /api/upload/certificate/preview - Certificate file preview");

    try {
        drogon::MultiPartParser fileParser;
        if (fileParser.parse(req) != 0) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid multipart form data";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& files = fileParser.getFiles();
        if (files.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "No file uploaded";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& file = files[0];
        std::string fileName = file.getFileName();
        auto fileContent = file.fileContent();
        size_t fileSize = file.fileLength();

        if (fileSize > static_cast<size_t>(MAX_CERT_FILE_SIZE)) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "File too large. Maximum size is 10MB for certificate files.";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        std::vector<uint8_t> contentBytes(fileContent.begin(), fileContent.end());
        auto result = uploadService_->previewCertificate(fileName, contentBytes);

        Json::Value response;
        response["success"] = result.success;
        response["fileFormat"] = result.fileFormat;
        response["isDuplicate"] = result.isDuplicate;
        if (!result.duplicateUploadId.empty()) response["duplicateUploadId"] = result.duplicateUploadId;
        if (!result.message.empty()) response["message"] = result.message;
        if (!result.errorMessage.empty()) response["errorMessage"] = result.errorMessage;

        Json::Value certsArray(Json::arrayValue);
        for (const auto& cert : result.certificates) {
            Json::Value certJson;
            certJson["subjectDn"] = cert.subjectDn;
            certJson["issuerDn"] = cert.issuerDn;
            certJson["serialNumber"] = cert.serialNumber;
            certJson["countryCode"] = cert.countryCode;
            certJson["certificateType"] = cert.certificateType;
            certJson["isSelfSigned"] = cert.isSelfSigned;
            certJson["isLinkCertificate"] = cert.isLinkCertificate;
            certJson["notBefore"] = cert.notBefore;
            certJson["notAfter"] = cert.notAfter;
            certJson["isExpired"] = cert.isExpired;
            certJson["signatureAlgorithm"] = cert.signatureAlgorithm;
            certJson["publicKeyAlgorithm"] = cert.publicKeyAlgorithm;
            certJson["keySize"] = cert.keySize;
            certJson["fingerprintSha256"] = cert.fingerprintSha256;
            certJson["doc9303Checklist"] = cert.doc9303Checklist.toJson();
            certsArray.append(certJson);
        }
        response["certificates"] = certsArray;

        if (!result.deviations.empty()) {
            Json::Value devsArray(Json::arrayValue);
            for (const auto& dev : result.deviations) {
                Json::Value devJson;
                devJson["certificateIssuerDn"] = dev.certificateIssuerDn;
                devJson["certificateSerialNumber"] = dev.certificateSerialNumber;
                devJson["defectDescription"] = dev.defectDescription;
                devJson["defectTypeOid"] = dev.defectTypeOid;
                devJson["defectCategory"] = dev.defectCategory;
                devsArray.append(devJson);
            }
            response["deviations"] = devsArray;
            response["dlIssuerCountry"] = result.dlIssuerCountry;
            response["dlVersion"] = result.dlVersion;
            response["dlHashAlgorithm"] = result.dlHashAlgorithm;
            response["dlSignatureValid"] = result.dlSignatureValid;
            response["dlSigningTime"] = result.dlSigningTime;
            response["dlEContentType"] = result.dlEContentType;
            response["dlCmsDigestAlgorithm"] = result.dlCmsDigestAlgorithm;
            response["dlCmsSignatureAlgorithm"] = result.dlCmsSignatureAlgorithm;
            response["dlSignerDn"] = result.dlSignerDn;
        }

        if (result.hasCrlInfo) {
            Json::Value crlJson;
            crlJson["issuerDn"] = result.crlInfo.issuerDn;
            crlJson["countryCode"] = result.crlInfo.countryCode;
            crlJson["thisUpdate"] = result.crlInfo.thisUpdate;
            crlJson["nextUpdate"] = result.crlInfo.nextUpdate;
            crlJson["crlNumber"] = result.crlInfo.crlNumber;
            crlJson["revokedCount"] = result.crlInfo.revokedCount;
            response["crlInfo"] = crlJson;
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(drogon::k200OK);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Certificate preview failed: {}", e.what());
        callback(common::handler::internalError("UploadHandler::previewCertificate", e));
    }
}

} // namespace handlers

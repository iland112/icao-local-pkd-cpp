/**
 * @file UploadController.hpp
 * @brief REST API controller for file upload operations
 */

#pragma once

#include <drogon/drogon.h>
#include <drogon/HttpController.h>
#include "../../application/usecase/UploadFileUseCase.hpp"
#include "../../application/usecase/GetUploadHistoryUseCase.hpp"
#include "../../application/command/UploadFileCommand.hpp"
#include "shared/exception/ApplicationException.hpp"
#include "shared/exception/DomainException.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace fileupload::infrastructure::controller {

using namespace drogon;
using namespace fileupload::application::usecase;
using namespace fileupload::application::command;
using namespace fileupload::domain::repository;
using namespace fileupload::domain::port;

/**
 * @brief Controller for file upload REST API
 */
class UploadController : public HttpController<UploadController> {
private:
    std::shared_ptr<UploadLdifFileUseCase> uploadLdifUseCase_;
    std::shared_ptr<UploadMasterListUseCase> uploadMasterListUseCase_;
    std::shared_ptr<GetUploadHistoryUseCase> getUploadHistoryUseCase_;
    std::shared_ptr<GetUploadDetailUseCase> getUploadDetailUseCase_;
    std::shared_ptr<GetUploadStatisticsUseCase> getUploadStatisticsUseCase_;

    /**
     * @brief Create error response
     */
    static HttpResponsePtr createErrorResponse(
        HttpStatusCode status,
        const std::string& code,
        const std::string& message
    ) {
        Json::Value error;
        error["code"] = code;
        error["message"] = message;
        error["timestamp"] = trantor::Date::now().toFormattedString(false);

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(status);
        return resp;
    }

    /**
     * @brief Handle file upload
     */
    HttpResponsePtr handleUpload(
        const HttpRequestPtr& req,
        const std::string& fileType,
        std::function<application::response::UploadResponse(const UploadFileCommand&)> uploadFunc
    ) {
        try {
            // Get uploaded file
            MultiPartParser parser;
            if (parser.parse(req) != 0) {
                return createErrorResponse(
                    k400BadRequest,
                    "INVALID_REQUEST",
                    "Invalid multipart form data"
                );
            }

            auto& files = parser.getFiles();
            if (files.empty()) {
                return createErrorResponse(
                    k400BadRequest,
                    "NO_FILE",
                    "No file uploaded"
                );
            }

            auto& file = files[0];
            std::string fileName = file.getFileName();
            std::string content = file.fileContent();

            // Convert to command
            UploadFileCommand command;
            command.fileName = fileName;
            command.originalFileName = fileName;
            command.content = std::vector<uint8_t>(content.begin(), content.end());

            // Get user info from header if available
            auto userHeader = req->getHeader("X-User-Id");
            if (!userHeader.empty()) {
                command.uploadedBy = userHeader;
            }

            // Execute use case
            auto response = uploadFunc(command);

            // Return success response
            auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
            resp->setStatusCode(k201Created);
            return resp;

        } catch (const shared::exception::ApplicationException& e) {
            spdlog::warn("Application error in upload: {} - {}", e.getCode(), e.what());
            return createErrorResponse(k400BadRequest, e.getCode(), e.what());
        } catch (const shared::exception::DomainException& e) {
            spdlog::warn("Domain error in upload: {} - {}", e.getCode(), e.what());
            return createErrorResponse(k400BadRequest, e.getCode(), e.what());
        } catch (const std::exception& e) {
            spdlog::error("Unexpected error in upload: {}", e.what());
            return createErrorResponse(k500InternalServerError, "INTERNAL_ERROR", e.what());
        }
    }

public:
    /**
     * @brief Configure dependencies
     */
    void configure(
        std::shared_ptr<IUploadedFileRepository> repository,
        std::shared_ptr<IFileStoragePort> fileStorage
    ) {
        uploadLdifUseCase_ = std::make_shared<UploadLdifFileUseCase>(repository, fileStorage);
        uploadMasterListUseCase_ = std::make_shared<UploadMasterListUseCase>(repository, fileStorage);
        getUploadHistoryUseCase_ = std::make_shared<GetUploadHistoryUseCase>(repository);
        getUploadDetailUseCase_ = std::make_shared<GetUploadDetailUseCase>(repository);
        getUploadStatisticsUseCase_ = std::make_shared<GetUploadStatisticsUseCase>(repository);
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UploadController::uploadLdif, "/api/upload/ldif", Post);
    ADD_METHOD_TO(UploadController::uploadMasterList, "/api/upload/masterlist", Post);
    ADD_METHOD_TO(UploadController::getHistory, "/api/upload/history", Get);
    ADD_METHOD_TO(UploadController::getDetail, "/api/upload/{uploadId}", Get);
    ADD_METHOD_TO(UploadController::getStatistics, "/api/upload/statistics", Get);
    METHOD_LIST_END

    /**
     * @brief POST /api/upload/ldif - Upload LDIF file
     */
    void uploadLdif(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    ) {
        auto resp = handleUpload(req, "LDIF", [this](const UploadFileCommand& cmd) {
            return uploadLdifUseCase_->execute(cmd);
        });
        callback(resp);
    }

    /**
     * @brief POST /api/upload/masterlist - Upload Master List file
     */
    void uploadMasterList(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    ) {
        auto resp = handleUpload(req, "ML", [this](const UploadFileCommand& cmd) {
            return uploadMasterListUseCase_->execute(cmd);
        });
        callback(resp);
    }

    /**
     * @brief GET /api/upload/history - Get upload history
     */
    void getHistory(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    ) {
        try {
            // Parse query parameters
            int page = 0;
            int size = 20;

            auto pageParam = req->getParameter("page");
            auto sizeParam = req->getParameter("size");

            if (!pageParam.empty()) {
                page = std::stoi(pageParam);
            }
            if (!sizeParam.empty()) {
                size = std::min(std::stoi(sizeParam), 100);  // Max 100 per page
            }

            auto response = getUploadHistoryUseCase_->execute(page, size);

            auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
            callback(resp);

        } catch (const std::exception& e) {
            spdlog::error("Error getting upload history: {}", e.what());
            callback(createErrorResponse(k500InternalServerError, "INTERNAL_ERROR", e.what()));
        }
    }

    /**
     * @brief GET /api/upload/{uploadId} - Get upload detail
     */
    void getDetail(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback,
        const std::string& uploadId
    ) {
        try {
            auto response = getUploadDetailUseCase_->execute(uploadId);

            auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
            callback(resp);

        } catch (const shared::exception::ApplicationException& e) {
            callback(createErrorResponse(k404NotFound, e.getCode(), e.what()));
        } catch (const std::exception& e) {
            spdlog::error("Error getting upload detail: {}", e.what());
            callback(createErrorResponse(k500InternalServerError, "INTERNAL_ERROR", e.what()));
        }
    }

    /**
     * @brief GET /api/upload/statistics - Get upload statistics
     */
    void getStatistics(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    ) {
        try {
            auto response = getUploadStatisticsUseCase_->execute();

            auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
            callback(resp);

        } catch (const std::exception& e) {
            spdlog::error("Error getting upload statistics: {}", e.what());
            callback(createErrorResponse(k500InternalServerError, "INTERNAL_ERROR", e.what()));
        }
    }
};

} // namespace fileupload::infrastructure::controller

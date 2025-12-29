/**
 * @file GetUploadHistoryUseCase.hpp
 * @brief Use case for retrieving upload history
 */

#pragma once

#include "../response/UploadResponse.hpp"
#include "../../domain/repository/IUploadedFileRepository.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <optional>

namespace fileupload::application::usecase {

using namespace fileupload::domain::model;
using namespace fileupload::domain::repository;
using namespace fileupload::application::response;

/**
 * @brief Use case for getting upload history with pagination
 */
class GetUploadHistoryUseCase {
private:
    std::shared_ptr<IUploadedFileRepository> repository_;

public:
    explicit GetUploadHistoryUseCase(std::shared_ptr<IUploadedFileRepository> repository)
        : repository_(std::move(repository)) {}

    /**
     * @brief Execute the use case
     */
    UploadHistoryResponse execute(int page = 0, int size = 20, std::optional<UploadStatus> status = std::nullopt) {
        PageRequest pageRequest{page, size};

        Page<UploadedFile> result;
        if (status) {
            result = repository_->findByStatus(*status, pageRequest);
        } else {
            result = repository_->findAll(pageRequest);
        }

        UploadHistoryResponse response;
        response.page = result.page;
        response.size = result.size;
        response.totalElements = result.totalElements;
        response.totalPages = result.totalPages;
        response.hasNext = result.hasNext();
        response.hasPrevious = result.hasPrevious();

        for (const auto& file : result.content) {
            response.content.push_back(UploadDetailResponse::fromDomain(file));
        }

        return response;
    }
};

/**
 * @brief Use case for getting a single upload detail
 */
class GetUploadDetailUseCase {
private:
    std::shared_ptr<IUploadedFileRepository> repository_;

public:
    explicit GetUploadDetailUseCase(std::shared_ptr<IUploadedFileRepository> repository)
        : repository_(std::move(repository)) {}

    /**
     * @brief Execute the use case
     */
    UploadDetailResponse execute(const std::string& uploadIdStr) {
        auto uploadId = UploadId::of(uploadIdStr);
        auto fileOpt = repository_->findById(uploadId);

        if (!fileOpt) {
            throw shared::exception::ApplicationException(
                "UPLOAD_NOT_FOUND",
                "Upload not found: " + uploadIdStr
            );
        }

        return UploadDetailResponse::fromDomain(*fileOpt);
    }
};

/**
 * @brief Use case for getting upload statistics
 */
class GetUploadStatisticsUseCase {
private:
    std::shared_ptr<IUploadedFileRepository> repository_;

public:
    explicit GetUploadStatisticsUseCase(std::shared_ptr<IUploadedFileRepository> repository)
        : repository_(std::move(repository)) {}

    /**
     * @brief Execute the use case
     */
    UploadStatisticsResponse execute() {
        UploadStatisticsResponse response;

        response.totalUploads = repository_->count();
        response.pendingUploads = repository_->countByStatus(UploadStatus::PENDING);
        response.processingUploads = repository_->countByStatus(UploadStatus::PROCESSING);
        response.completedUploads = repository_->countByStatus(UploadStatus::COMPLETED);
        response.failedUploads = repository_->countByStatus(UploadStatus::FAILED);

        // TODO: Get certificate counts from certificate repository
        response.totalCsca = 0;
        response.totalDsc = 0;
        response.totalDscNc = 0;
        response.totalCrl = 0;
        response.totalMl = 0;

        return response;
    }
};

} // namespace fileupload::application::usecase

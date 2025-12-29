/**
 * @file FileUploadedEvent.hpp
 * @brief Domain event for file upload
 */

#pragma once

#include "shared/domain/AggregateRoot.hpp"
#include "../model/UploadId.hpp"
#include "../model/FileFormat.hpp"
#include <string>

namespace fileupload::domain::event {

using namespace fileupload::domain::model;

/**
 * @brief Event raised when a file is uploaded
 */
class FileUploadedEvent : public shared::domain::DomainEvent {
private:
    UploadId uploadId_;
    std::string fileName_;
    FileFormat fileFormat_;
    int64_t fileSize_;

public:
    FileUploadedEvent(
        UploadId uploadId,
        std::string fileName,
        FileFormat fileFormat,
        int64_t fileSize
    )
        : DomainEvent("FileUploaded"),
          uploadId_(std::move(uploadId)),
          fileName_(std::move(fileName)),
          fileFormat_(fileFormat),
          fileSize_(fileSize) {}

    [[nodiscard]] const UploadId& getUploadId() const noexcept { return uploadId_; }
    [[nodiscard]] const std::string& getFileName() const noexcept { return fileName_; }
    [[nodiscard]] FileFormat getFileFormat() const noexcept { return fileFormat_; }
    [[nodiscard]] int64_t getFileSize() const noexcept { return fileSize_; }
};

/**
 * @brief Event raised when file processing starts
 */
class FileProcessingStartedEvent : public shared::domain::DomainEvent {
private:
    UploadId uploadId_;

public:
    explicit FileProcessingStartedEvent(UploadId uploadId)
        : DomainEvent("FileProcessingStarted"),
          uploadId_(std::move(uploadId)) {}

    [[nodiscard]] const UploadId& getUploadId() const noexcept { return uploadId_; }
};

/**
 * @brief Event raised when file processing completes
 */
class FileProcessingCompletedEvent : public shared::domain::DomainEvent {
private:
    UploadId uploadId_;
    int cscaCount_;
    int dscCount_;
    int dscNcCount_;
    int crlCount_;
    int mlCount_;

public:
    FileProcessingCompletedEvent(
        UploadId uploadId,
        int cscaCount,
        int dscCount,
        int dscNcCount,
        int crlCount,
        int mlCount
    )
        : DomainEvent("FileProcessingCompleted"),
          uploadId_(std::move(uploadId)),
          cscaCount_(cscaCount),
          dscCount_(dscCount),
          dscNcCount_(dscNcCount),
          crlCount_(crlCount),
          mlCount_(mlCount) {}

    [[nodiscard]] const UploadId& getUploadId() const noexcept { return uploadId_; }
    [[nodiscard]] int getCscaCount() const noexcept { return cscaCount_; }
    [[nodiscard]] int getDscCount() const noexcept { return dscCount_; }
    [[nodiscard]] int getDscNcCount() const noexcept { return dscNcCount_; }
    [[nodiscard]] int getCrlCount() const noexcept { return crlCount_; }
    [[nodiscard]] int getMlCount() const noexcept { return mlCount_; }

    [[nodiscard]] int getTotalCount() const noexcept {
        return cscaCount_ + dscCount_ + dscNcCount_ + crlCount_ + mlCount_;
    }
};

/**
 * @brief Event raised when file processing fails
 */
class FileProcessingFailedEvent : public shared::domain::DomainEvent {
private:
    UploadId uploadId_;
    std::string errorMessage_;

public:
    FileProcessingFailedEvent(UploadId uploadId, std::string errorMessage)
        : DomainEvent("FileProcessingFailed"),
          uploadId_(std::move(uploadId)),
          errorMessage_(std::move(errorMessage)) {}

    [[nodiscard]] const UploadId& getUploadId() const noexcept { return uploadId_; }
    [[nodiscard]] const std::string& getErrorMessage() const noexcept { return errorMessage_; }
};

} // namespace fileupload::domain::event

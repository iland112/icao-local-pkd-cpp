/**
 * @file UploadedFile.hpp
 * @brief Aggregate Root for uploaded file
 */

#pragma once

#include "shared/domain/AggregateRoot.hpp"
#include "UploadId.hpp"
#include "FileName.hpp"
#include "FileHash.hpp"
#include "FileSize.hpp"
#include "FileFormat.hpp"
#include "UploadStatus.hpp"
#include <string>
#include <optional>
#include <chrono>

namespace fileupload::domain::model {

/**
 * @brief Upload Statistics
 */
struct UploadStatistics {
    int totalEntries = 0;
    int processedEntries = 0;
    int cscaCount = 0;
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int mlCount = 0;

    [[nodiscard]] double getProgressPercent() const {
        if (totalEntries == 0) return 0.0;
        return (static_cast<double>(processedEntries) / totalEntries) * 100.0;
    }
};

/**
 * @brief Uploaded File Aggregate Root
 */
class UploadedFile : public shared::domain::AggregateRoot<UploadId> {
private:
    FileName fileName_;
    std::optional<std::string> originalFileName_;
    std::optional<std::string> filePath_;
    FileHash fileHash_;
    FileSize fileSize_;
    FileFormat fileFormat_;
    std::optional<std::string> collectionNumber_;
    UploadStatus status_;
    std::chrono::system_clock::time_point uploadTimestamp_;
    std::optional<std::chrono::system_clock::time_point> completedTimestamp_;
    std::optional<std::string> errorMessage_;
    std::optional<std::string> uploadedBy_;
    UploadStatistics statistics_;

public:
    /**
     * @brief Create a new uploaded file
     */
    static UploadedFile create(
        FileName fileName,
        FileHash fileHash,
        FileSize fileSize,
        FileFormat fileFormat,
        std::optional<std::string> originalFileName = std::nullopt,
        std::optional<std::string> uploadedBy = std::nullopt
    ) {
        return UploadedFile(
            UploadId::generate(),
            std::move(fileName),
            std::move(fileHash),
            std::move(fileSize),
            fileFormat,
            std::move(originalFileName),
            std::move(uploadedBy)
        );
    }

    /**
     * @brief Reconstruct from persistence
     */
    static UploadedFile reconstruct(
        UploadId id,
        FileName fileName,
        FileHash fileHash,
        FileSize fileSize,
        FileFormat fileFormat,
        UploadStatus status,
        std::chrono::system_clock::time_point uploadTimestamp,
        std::optional<std::string> originalFileName = std::nullopt,
        std::optional<std::string> filePath = std::nullopt,
        std::optional<std::string> collectionNumber = std::nullopt,
        std::optional<std::chrono::system_clock::time_point> completedTimestamp = std::nullopt,
        std::optional<std::string> errorMessage = std::nullopt,
        std::optional<std::string> uploadedBy = std::nullopt,
        UploadStatistics statistics = {}
    ) {
        UploadedFile file(
            std::move(id),
            std::move(fileName),
            std::move(fileHash),
            std::move(fileSize),
            fileFormat,
            std::move(originalFileName),
            std::move(uploadedBy)
        );
        file.status_ = status;
        file.uploadTimestamp_ = uploadTimestamp;
        file.filePath_ = std::move(filePath);
        file.collectionNumber_ = std::move(collectionNumber);
        file.completedTimestamp_ = completedTimestamp;
        file.errorMessage_ = std::move(errorMessage);
        file.statistics_ = statistics;
        return file;
    }

    // Getters
    [[nodiscard]] const FileName& getFileName() const noexcept { return fileName_; }
    [[nodiscard]] const std::optional<std::string>& getOriginalFileName() const noexcept { return originalFileName_; }
    [[nodiscard]] const std::optional<std::string>& getFilePath() const noexcept { return filePath_; }
    [[nodiscard]] const FileHash& getFileHash() const noexcept { return fileHash_; }
    [[nodiscard]] const FileSize& getFileSize() const noexcept { return fileSize_; }
    [[nodiscard]] FileFormat getFileFormat() const noexcept { return fileFormat_; }
    [[nodiscard]] const std::optional<std::string>& getCollectionNumber() const noexcept { return collectionNumber_; }
    [[nodiscard]] UploadStatus getStatus() const noexcept { return status_; }
    [[nodiscard]] std::chrono::system_clock::time_point getUploadTimestamp() const noexcept { return uploadTimestamp_; }
    [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>& getCompletedTimestamp() const noexcept { return completedTimestamp_; }
    [[nodiscard]] const std::optional<std::string>& getErrorMessage() const noexcept { return errorMessage_; }
    [[nodiscard]] const std::optional<std::string>& getUploadedBy() const noexcept { return uploadedBy_; }
    [[nodiscard]] const UploadStatistics& getStatistics() const noexcept { return statistics_; }

    // Domain methods

    /**
     * @brief Set file path after storage
     */
    void setFilePath(const std::string& path) {
        filePath_ = path;
        incrementVersion();
    }

    /**
     * @brief Set collection number (from LDIF)
     */
    void setCollectionNumber(const std::string& number) {
        collectionNumber_ = number;
        incrementVersion();
    }

    /**
     * @brief Start processing
     */
    void startProcessing() {
        if (!isValidTransition(status_, UploadStatus::PROCESSING)) {
            throw shared::exception::DomainException(
                "INVALID_STATUS_TRANSITION",
                "Cannot start processing from status: " + toString(status_)
            );
        }
        status_ = UploadStatus::PROCESSING;
        incrementVersion();
    }

    /**
     * @brief Update processing progress
     */
    void updateProgress(int processedEntries, int totalEntries) {
        statistics_.processedEntries = processedEntries;
        statistics_.totalEntries = totalEntries;
    }

    /**
     * @brief Increment certificate counts
     */
    void incrementCscaCount() { statistics_.cscaCount++; }
    void incrementDscCount() { statistics_.dscCount++; }
    void incrementDscNcCount() { statistics_.dscNcCount++; }
    void incrementCrlCount() { statistics_.crlCount++; }
    void incrementMlCount() { statistics_.mlCount++; }

    /**
     * @brief Complete processing
     */
    void complete(const UploadStatistics& finalStats) {
        if (!isValidTransition(status_, UploadStatus::COMPLETED)) {
            throw shared::exception::DomainException(
                "INVALID_STATUS_TRANSITION",
                "Cannot complete from status: " + toString(status_)
            );
        }
        status_ = UploadStatus::COMPLETED;
        completedTimestamp_ = std::chrono::system_clock::now();
        statistics_ = finalStats;
        incrementVersion();
    }

    /**
     * @brief Mark as failed
     */
    void fail(const std::string& message) {
        if (status_ == UploadStatus::COMPLETED || status_ == UploadStatus::FAILED) {
            throw shared::exception::DomainException(
                "INVALID_STATUS_TRANSITION",
                "Cannot fail from status: " + toString(status_)
            );
        }
        status_ = UploadStatus::FAILED;
        completedTimestamp_ = std::chrono::system_clock::now();
        errorMessage_ = message;
        incrementVersion();
    }

    /**
     * @brief Check if processing is complete
     */
    [[nodiscard]] bool isComplete() const noexcept {
        return status_ == UploadStatus::COMPLETED;
    }

    /**
     * @brief Check if processing failed
     */
    [[nodiscard]] bool isFailed() const noexcept {
        return status_ == UploadStatus::FAILED;
    }

    /**
     * @brief Check if file is being processed
     */
    [[nodiscard]] bool isProcessing() const noexcept {
        return status_ == UploadStatus::PROCESSING;
    }

private:
    UploadedFile(
        UploadId id,
        FileName fileName,
        FileHash fileHash,
        FileSize fileSize,
        FileFormat fileFormat,
        std::optional<std::string> originalFileName,
        std::optional<std::string> uploadedBy
    )
        : AggregateRoot<UploadId>(std::move(id)),
          fileName_(std::move(fileName)),
          originalFileName_(std::move(originalFileName)),
          fileHash_(std::move(fileHash)),
          fileSize_(std::move(fileSize)),
          fileFormat_(fileFormat),
          status_(UploadStatus::PENDING),
          uploadTimestamp_(std::chrono::system_clock::now()),
          uploadedBy_(std::move(uploadedBy)) {}
};

} // namespace fileupload::domain::model

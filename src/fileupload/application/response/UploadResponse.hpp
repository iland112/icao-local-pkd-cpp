/**
 * @file UploadResponse.hpp
 * @brief Response DTOs for upload operations
 */

#pragma once

#include "../../domain/model/UploadedFile.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace fileupload::application::response {

using namespace fileupload::domain::model;

/**
 * @brief Response for single upload operation
 */
struct UploadResponse {
    std::string uploadId;
    std::string fileName;
    std::string fileFormat;
    int64_t fileSize;
    std::string status;
    std::string uploadTimestamp;
    std::string message;

    static UploadResponse fromDomain(const UploadedFile& file) {
        UploadResponse response;
        response.uploadId = file.getId().toString();
        response.fileName = file.getFileName().toString();
        response.fileFormat = toString(file.getFileFormat());
        response.fileSize = file.getFileSize().toBytes();
        response.status = toString(file.getStatus());

        // Format timestamp
        auto time_t = std::chrono::system_clock::to_time_t(file.getUploadTimestamp());
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t));
        response.uploadTimestamp = buffer;

        return response;
    }

    [[nodiscard]] nlohmann::json toJson() const {
        return {
            {"uploadId", uploadId},
            {"fileName", fileName},
            {"fileFormat", fileFormat},
            {"fileSize", fileSize},
            {"status", status},
            {"uploadTimestamp", uploadTimestamp},
            {"message", message}
        };
    }
};

/**
 * @brief Response for upload details with statistics
 */
struct UploadDetailResponse {
    std::string uploadId;
    std::string fileName;
    std::string originalFileName;
    std::string filePath;
    std::string fileHash;
    int64_t fileSize;
    std::string fileSizeHuman;
    std::string fileFormat;
    std::string collectionNumber;
    std::string status;
    std::string uploadTimestamp;
    std::string completedTimestamp;
    std::string errorMessage;
    std::string uploadedBy;

    // Statistics
    int totalEntries;
    int processedEntries;
    double progressPercent;
    int cscaCount;
    int dscCount;
    int dscNcCount;
    int crlCount;
    int mlCount;

    static UploadDetailResponse fromDomain(const UploadedFile& file) {
        UploadDetailResponse response;
        response.uploadId = file.getId().toString();
        response.fileName = file.getFileName().toString();
        response.originalFileName = file.getOriginalFileName().value_or("");
        response.filePath = file.getFilePath().value_or("");
        response.fileHash = file.getFileHash().toString();
        response.fileSize = file.getFileSize().toBytes();
        response.fileSizeHuman = file.getFileSize().toHumanReadable();
        response.fileFormat = toString(file.getFileFormat());
        response.collectionNumber = file.getCollectionNumber().value_or("");
        response.status = toString(file.getStatus());
        response.errorMessage = file.getErrorMessage().value_or("");
        response.uploadedBy = file.getUploadedBy().value_or("");

        // Format timestamps
        auto formatTime = [](std::chrono::system_clock::time_point tp) {
            auto time_t = std::chrono::system_clock::to_time_t(tp);
            char buffer[64];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t));
            return std::string(buffer);
        };

        response.uploadTimestamp = formatTime(file.getUploadTimestamp());
        if (file.getCompletedTimestamp()) {
            response.completedTimestamp = formatTime(*file.getCompletedTimestamp());
        }

        // Statistics
        const auto& stats = file.getStatistics();
        response.totalEntries = stats.totalEntries;
        response.processedEntries = stats.processedEntries;
        response.progressPercent = stats.getProgressPercent();
        response.cscaCount = stats.cscaCount;
        response.dscCount = stats.dscCount;
        response.dscNcCount = stats.dscNcCount;
        response.crlCount = stats.crlCount;
        response.mlCount = stats.mlCount;

        return response;
    }

    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json json = {
            {"uploadId", uploadId},
            {"fileName", fileName},
            {"fileHash", fileHash},
            {"fileSize", fileSize},
            {"fileSizeHuman", fileSizeHuman},
            {"fileFormat", fileFormat},
            {"status", status},
            {"uploadTimestamp", uploadTimestamp},
            {"statistics", {
                {"totalEntries", totalEntries},
                {"processedEntries", processedEntries},
                {"progressPercent", progressPercent},
                {"cscaCount", cscaCount},
                {"dscCount", dscCount},
                {"dscNcCount", dscNcCount},
                {"crlCount", crlCount},
                {"mlCount", mlCount}
            }}
        };

        if (!originalFileName.empty()) json["originalFileName"] = originalFileName;
        if (!filePath.empty()) json["filePath"] = filePath;
        if (!collectionNumber.empty()) json["collectionNumber"] = collectionNumber;
        if (!completedTimestamp.empty()) json["completedTimestamp"] = completedTimestamp;
        if (!errorMessage.empty()) json["errorMessage"] = errorMessage;
        if (!uploadedBy.empty()) json["uploadedBy"] = uploadedBy;

        return json;
    }
};

/**
 * @brief Response for upload history list
 */
struct UploadHistoryResponse {
    std::vector<UploadDetailResponse> content;
    int page;
    int size;
    int64_t totalElements;
    int totalPages;
    bool hasNext;
    bool hasPrevious;

    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json contentArray = nlohmann::json::array();
        for (const auto& item : content) {
            contentArray.push_back(item.toJson());
        }

        return {
            {"content", contentArray},
            {"page", page},
            {"size", size},
            {"totalElements", totalElements},
            {"totalPages", totalPages},
            {"hasNext", hasNext},
            {"hasPrevious", hasPrevious}
        };
    }
};

/**
 * @brief Response for upload statistics
 */
struct UploadStatisticsResponse {
    int64_t totalUploads;
    int64_t pendingUploads;
    int64_t processingUploads;
    int64_t completedUploads;
    int64_t failedUploads;
    int64_t totalCsca;
    int64_t totalDsc;
    int64_t totalDscNc;
    int64_t totalCrl;
    int64_t totalMl;

    [[nodiscard]] nlohmann::json toJson() const {
        return {
            {"totalUploads", totalUploads},
            {"pendingUploads", pendingUploads},
            {"processingUploads", processingUploads},
            {"completedUploads", completedUploads},
            {"failedUploads", failedUploads},
            {"certificates", {
                {"csca", totalCsca},
                {"dsc", totalDsc},
                {"dscNc", totalDscNc},
                {"crl", totalCrl},
                {"ml", totalMl}
            }}
        };
    }
};

} // namespace fileupload::application::response

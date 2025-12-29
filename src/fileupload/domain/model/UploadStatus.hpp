/**
 * @file UploadStatus.hpp
 * @brief Enum for upload processing status
 */

#pragma once

#include <string>
#include <stdexcept>

namespace fileupload::domain::model {

/**
 * @brief Upload processing status
 */
enum class UploadStatus {
    PENDING,      // File uploaded, waiting for processing
    PROCESSING,   // Currently being processed
    COMPLETED,    // Processing completed successfully
    FAILED        // Processing failed
};

/**
 * @brief Convert UploadStatus to string
 */
inline std::string toString(UploadStatus status) {
    switch (status) {
        case UploadStatus::PENDING: return "PENDING";
        case UploadStatus::PROCESSING: return "PROCESSING";
        case UploadStatus::COMPLETED: return "COMPLETED";
        case UploadStatus::FAILED: return "FAILED";
        default: throw std::invalid_argument("Unknown UploadStatus");
    }
}

/**
 * @brief Parse string to UploadStatus
 */
inline UploadStatus parseUploadStatus(const std::string& str) {
    if (str == "PENDING") return UploadStatus::PENDING;
    if (str == "PROCESSING") return UploadStatus::PROCESSING;
    if (str == "COMPLETED") return UploadStatus::COMPLETED;
    if (str == "FAILED") return UploadStatus::FAILED;
    throw std::invalid_argument("Unknown upload status: " + str);
}

/**
 * @brief Check if status transition is valid
 */
inline bool isValidTransition(UploadStatus from, UploadStatus to) {
    switch (from) {
        case UploadStatus::PENDING:
            return to == UploadStatus::PROCESSING || to == UploadStatus::FAILED;
        case UploadStatus::PROCESSING:
            return to == UploadStatus::COMPLETED || to == UploadStatus::FAILED;
        case UploadStatus::COMPLETED:
        case UploadStatus::FAILED:
            return false;  // Terminal states
        default:
            return false;
    }
}

} // namespace fileupload::domain::model

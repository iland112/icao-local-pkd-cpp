/**
 * @file UploadFileCommand.hpp
 * @brief Command DTOs for upload operations
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace fileupload::application::command {

/**
 * @brief Command for uploading a file
 */
struct UploadFileCommand {
    std::string fileName;
    std::string originalFileName;
    std::vector<uint8_t> content;
    std::string uploadedBy;

    UploadFileCommand() = default;

    UploadFileCommand(
        std::string fileName,
        std::string originalFileName,
        std::vector<uint8_t> content,
        std::string uploadedBy = ""
    )
        : fileName(std::move(fileName)),
          originalFileName(std::move(originalFileName)),
          content(std::move(content)),
          uploadedBy(std::move(uploadedBy)) {}

    [[nodiscard]] bool isEmpty() const noexcept {
        return content.empty();
    }

    [[nodiscard]] size_t getSize() const noexcept {
        return content.size();
    }
};

/**
 * @brief Command for checking duplicate files
 */
struct CheckDuplicateCommand {
    std::string fileHash;

    explicit CheckDuplicateCommand(std::string hash)
        : fileHash(std::move(hash)) {}
};

} // namespace fileupload::application::command

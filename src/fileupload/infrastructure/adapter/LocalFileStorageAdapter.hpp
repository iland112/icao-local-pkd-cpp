/**
 * @file LocalFileStorageAdapter.hpp
 * @brief Local filesystem adapter for file storage
 */

#pragma once

#include "../../domain/port/IFileStoragePort.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fileupload::infrastructure::adapter {

using namespace fileupload::domain::port;
namespace fs = std::filesystem;

/**
 * @brief Local filesystem implementation of file storage
 */
class LocalFileStorageAdapter : public IFileStoragePort {
private:
    fs::path basePath_;

    void ensureDirectoryExists(const fs::path& path) {
        if (!fs::exists(path)) {
            if (!fs::create_directories(path)) {
                throw shared::exception::InfrastructureException(
                    "STORAGE_ERROR",
                    "Failed to create directory: " + path.string()
                );
            }
        }
    }

public:
    explicit LocalFileStorageAdapter(const std::string& basePath)
        : basePath_(basePath) {
        ensureDirectoryExists(basePath_);
    }

    std::string store(const std::string& fileId, const std::vector<uint8_t>& content) override {
        // Create subdirectory based on first 2 chars of fileId (for better distribution)
        std::string subDir = fileId.length() >= 2 ? fileId.substr(0, 2) : "00";
        fs::path dirPath = basePath_ / subDir;
        ensureDirectoryExists(dirPath);

        fs::path filePath = dirPath / fileId;
        std::string pathStr = filePath.string();

        std::ofstream file(pathStr, std::ios::binary);
        if (!file) {
            throw shared::exception::InfrastructureException(
                "STORAGE_ERROR",
                "Failed to create file: " + pathStr
            );
        }

        file.write(reinterpret_cast<const char*>(content.data()),
                   static_cast<std::streamsize>(content.size()));

        if (!file) {
            throw shared::exception::InfrastructureException(
                "STORAGE_ERROR",
                "Failed to write file: " + pathStr
            );
        }

        file.close();
        return pathStr;
    }

    std::vector<uint8_t> read(const std::string& path) override {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw shared::exception::InfrastructureException(
                "STORAGE_ERROR",
                "Failed to open file: " + path
            );
        }

        auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> content(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(content.data()), size)) {
            throw shared::exception::InfrastructureException(
                "STORAGE_ERROR",
                "Failed to read file: " + path
            );
        }

        return content;
    }

    bool remove(const std::string& path) override {
        std::error_code ec;
        return fs::remove(path, ec);
    }

    bool exists(const std::string& path) override {
        return fs::exists(path);
    }

    int64_t getSize(const std::string& path) override {
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec) {
            throw shared::exception::InfrastructureException(
                "STORAGE_ERROR",
                "Failed to get file size: " + path
            );
        }
        return static_cast<int64_t>(size);
    }
};

} // namespace fileupload::infrastructure::adapter

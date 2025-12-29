/**
 * @file IFileStoragePort.hpp
 * @brief Port interface for file storage operations
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace fileupload::domain::port {

/**
 * @brief Port interface for file storage
 */
class IFileStoragePort {
public:
    virtual ~IFileStoragePort() = default;

    /**
     * @brief Store file content
     * @param fileId Unique file identifier
     * @param content File content
     * @return Storage path
     */
    virtual std::string store(const std::string& fileId, const std::vector<uint8_t>& content) = 0;

    /**
     * @brief Read file content
     * @param path Storage path
     * @return File content
     */
    virtual std::vector<uint8_t> read(const std::string& path) = 0;

    /**
     * @brief Delete file
     * @param path Storage path
     * @return true if deleted successfully
     */
    virtual bool remove(const std::string& path) = 0;

    /**
     * @brief Check if file exists
     * @param path Storage path
     * @return true if exists
     */
    virtual bool exists(const std::string& path) = 0;

    /**
     * @brief Get file size
     * @param path Storage path
     * @return File size in bytes
     */
    virtual int64_t getSize(const std::string& path) = 0;
};

} // namespace fileupload::domain::port

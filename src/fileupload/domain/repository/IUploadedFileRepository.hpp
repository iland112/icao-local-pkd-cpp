/**
 * @file IUploadedFileRepository.hpp
 * @brief Repository interface for UploadedFile aggregate
 */

#pragma once

#include "../model/UploadedFile.hpp"
#include "../model/UploadId.hpp"
#include "../model/FileHash.hpp"
#include "../model/UploadStatus.hpp"
#include <optional>
#include <vector>
#include <memory>

namespace fileupload::domain::repository {

using namespace fileupload::domain::model;

/**
 * @brief Pagination parameters
 */
struct PageRequest {
    int page = 0;
    int size = 20;

    [[nodiscard]] int getOffset() const noexcept {
        return page * size;
    }
};

/**
 * @brief Paginated result
 */
template<typename T>
struct Page {
    std::vector<T> content;
    int page;
    int size;
    int64_t totalElements;
    int totalPages;

    [[nodiscard]] bool hasNext() const noexcept {
        return page < totalPages - 1;
    }

    [[nodiscard]] bool hasPrevious() const noexcept {
        return page > 0;
    }
};

/**
 * @brief Repository interface for UploadedFile aggregate
 */
class IUploadedFileRepository {
public:
    virtual ~IUploadedFileRepository() = default;

    /**
     * @brief Save or update an uploaded file
     * @param file The file to save
     * @return The saved file
     */
    virtual UploadedFile save(const UploadedFile& file) = 0;

    /**
     * @brief Find by ID
     * @param id The upload ID
     * @return The file if found
     */
    virtual std::optional<UploadedFile> findById(const UploadId& id) = 0;

    /**
     * @brief Find by file hash
     * @param hash The SHA-256 hash
     * @return The file if found
     */
    virtual std::optional<UploadedFile> findByHash(const FileHash& hash) = 0;

    /**
     * @brief Find all with pagination
     * @param pageRequest Pagination parameters
     * @return Paginated list of files
     */
    virtual Page<UploadedFile> findAll(const PageRequest& pageRequest) = 0;

    /**
     * @brief Find by status with pagination
     * @param status The upload status
     * @param pageRequest Pagination parameters
     * @return Paginated list of files
     */
    virtual Page<UploadedFile> findByStatus(UploadStatus status, const PageRequest& pageRequest) = 0;

    /**
     * @brief Find recent uploads
     * @param limit Maximum number of results
     * @return List of recent uploads
     */
    virtual std::vector<UploadedFile> findRecent(int limit = 10) = 0;

    /**
     * @brief Check if file with hash already exists
     * @param hash The SHA-256 hash
     * @return true if exists
     */
    virtual bool existsByHash(const FileHash& hash) = 0;

    /**
     * @brief Delete by ID
     * @param id The upload ID
     * @return true if deleted
     */
    virtual bool deleteById(const UploadId& id) = 0;

    /**
     * @brief Count total uploads
     * @return Total count
     */
    virtual int64_t count() = 0;

    /**
     * @brief Count by status
     * @param status The upload status
     * @return Count
     */
    virtual int64_t countByStatus(UploadStatus status) = 0;
};

} // namespace fileupload::domain::repository

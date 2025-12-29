/**
 * @file PostgresUploadedFileRepository.hpp
 * @brief PostgreSQL implementation of UploadedFile repository
 */

#pragma once

#include "../../domain/repository/IUploadedFileRepository.hpp"
#include "../../domain/model/UploadedFile.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <memory>

namespace fileupload::infrastructure::repository {

using namespace fileupload::domain::repository;
using namespace fileupload::domain::model;
using namespace drogon::orm;

/**
 * @brief PostgreSQL implementation of UploadedFile repository
 */
class PostgresUploadedFileRepository : public IUploadedFileRepository {
private:
    std::string connectionString_;

    /**
     * @brief Get database client
     */
    DbClientPtr getClient() const {
        return drogon::app().getDbClient();
    }

    /**
     * @brief Parse timestamp string to time_point
     */
    static std::chrono::system_clock::time_point parseTimestamp(const std::string& ts) {
        std::tm tm = {};
        std::istringstream ss(ts);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    /**
     * @brief Format time_point to string
     */
    static std::string formatTimestamp(std::chrono::system_clock::time_point tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::gmtime(&time_t));
        return buffer;
    }

    /**
     * @brief Map database row to domain object
     */
    static UploadedFile mapToDomain(const Row& row) {
        UploadStatistics stats;
        stats.totalEntries = row["total_entries"].as<int>();
        stats.processedEntries = row["processed_entries"].as<int>();
        stats.cscaCount = row["csca_count"].as<int>();
        stats.dscCount = row["dsc_count"].as<int>();
        stats.dscNcCount = row["dsc_nc_count"].as<int>();
        stats.crlCount = row["crl_count"].as<int>();
        stats.mlCount = row["ml_count"].as<int>();

        std::optional<std::chrono::system_clock::time_point> completedTs;
        if (!row["completed_timestamp"].isNull()) {
            completedTs = parseTimestamp(row["completed_timestamp"].as<std::string>());
        }

        std::optional<std::string> errorMessage;
        if (!row["error_message"].isNull()) {
            errorMessage = row["error_message"].as<std::string>();
        }

        std::optional<std::string> collectionNumber;
        if (!row["collection_number"].isNull()) {
            collectionNumber = row["collection_number"].as<std::string>();
        }

        std::optional<std::string> filePath;
        if (!row["file_path"].isNull()) {
            filePath = row["file_path"].as<std::string>();
        }

        std::optional<std::string> originalFileName;
        if (!row["original_file_name"].isNull()) {
            originalFileName = row["original_file_name"].as<std::string>();
        }

        std::optional<std::string> uploadedBy;
        if (!row["uploaded_by"].isNull()) {
            uploadedBy = row["uploaded_by"].as<std::string>();
        }

        return UploadedFile::reconstruct(
            UploadId::of(row["id"].as<std::string>()),
            FileName::of(row["file_name"].as<std::string>()),
            FileHash::of(row["file_hash"].as<std::string>()),
            FileSize::ofBytes(row["file_size"].as<int64_t>()),
            parseFileFormat(row["file_format"].as<std::string>()),
            parseUploadStatus(row["status"].as<std::string>()),
            parseTimestamp(row["upload_timestamp"].as<std::string>()),
            originalFileName,
            filePath,
            collectionNumber,
            completedTs,
            errorMessage,
            uploadedBy,
            stats
        );
    }

public:
    PostgresUploadedFileRepository() = default;

    UploadedFile save(const UploadedFile& file) override {
        auto client = getClient();

        std::string sql;
        bool isInsert = false;

        // Check if exists
        auto checkResult = client->execSqlSync(
            "SELECT COUNT(*) FROM uploaded_file WHERE id = $1",
            file.getId().toString()
        );

        if (checkResult[0][0].as<int64_t>() == 0) {
            isInsert = true;
            sql = R"(
                INSERT INTO uploaded_file (
                    id, file_name, original_file_name, file_path, file_hash,
                    file_size, file_format, collection_number, status,
                    upload_timestamp, completed_timestamp, error_message, uploaded_by,
                    total_entries, processed_entries, csca_count, dsc_count,
                    dsc_nc_count, crl_count, ml_count
                ) VALUES (
                    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13,
                    $14, $15, $16, $17, $18, $19, $20
                )
            )";
        } else {
            sql = R"(
                UPDATE uploaded_file SET
                    file_name = $2, original_file_name = $3, file_path = $4,
                    file_hash = $5, file_size = $6, file_format = $7,
                    collection_number = $8, status = $9, upload_timestamp = $10,
                    completed_timestamp = $11, error_message = $12, uploaded_by = $13,
                    total_entries = $14, processed_entries = $15, csca_count = $16,
                    dsc_count = $17, dsc_nc_count = $18, crl_count = $19, ml_count = $20
                WHERE id = $1
            )";
        }

        const auto& stats = file.getStatistics();

        client->execSqlSync(
            sql,
            file.getId().toString(),
            file.getFileName().toString(),
            file.getOriginalFileName().value_or(""),
            file.getFilePath().value_or(""),
            file.getFileHash().toString(),
            file.getFileSize().toBytes(),
            toString(file.getFileFormat()),
            file.getCollectionNumber().value_or(""),
            toString(file.getStatus()),
            formatTimestamp(file.getUploadTimestamp()),
            file.getCompletedTimestamp() ? formatTimestamp(*file.getCompletedTimestamp()) : "",
            file.getErrorMessage().value_or(""),
            file.getUploadedBy().value_or(""),
            stats.totalEntries,
            stats.processedEntries,
            stats.cscaCount,
            stats.dscCount,
            stats.dscNcCount,
            stats.crlCount,
            stats.mlCount
        );

        spdlog::debug("{} uploaded file: {}", isInsert ? "Inserted" : "Updated", file.getId().toString());
        return file;
    }

    std::optional<UploadedFile> findById(const UploadId& id) override {
        auto client = getClient();
        auto result = client->execSqlSync(
            "SELECT * FROM uploaded_file WHERE id = $1",
            id.toString()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return mapToDomain(result[0]);
    }

    std::optional<UploadedFile> findByHash(const FileHash& hash) override {
        auto client = getClient();
        auto result = client->execSqlSync(
            "SELECT * FROM uploaded_file WHERE file_hash = $1",
            hash.toString()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return mapToDomain(result[0]);
    }

    Page<UploadedFile> findAll(const PageRequest& pageRequest) override {
        auto client = getClient();

        // Get total count
        auto countResult = client->execSqlSync("SELECT COUNT(*) FROM uploaded_file");
        int64_t totalElements = countResult[0][0].as<int64_t>();

        // Get page
        auto result = client->execSqlSync(
            "SELECT * FROM uploaded_file ORDER BY upload_timestamp DESC LIMIT $1 OFFSET $2",
            pageRequest.size,
            pageRequest.getOffset()
        );

        Page<UploadedFile> page;
        page.page = pageRequest.page;
        page.size = pageRequest.size;
        page.totalElements = totalElements;
        page.totalPages = static_cast<int>((totalElements + pageRequest.size - 1) / pageRequest.size);

        for (const auto& row : result) {
            page.content.push_back(mapToDomain(row));
        }

        return page;
    }

    Page<UploadedFile> findByStatus(UploadStatus status, const PageRequest& pageRequest) override {
        auto client = getClient();
        std::string statusStr = toString(status);

        // Get total count
        auto countResult = client->execSqlSync(
            "SELECT COUNT(*) FROM uploaded_file WHERE status = $1",
            statusStr
        );
        int64_t totalElements = countResult[0][0].as<int64_t>();

        // Get page
        auto result = client->execSqlSync(
            "SELECT * FROM uploaded_file WHERE status = $1 ORDER BY upload_timestamp DESC LIMIT $2 OFFSET $3",
            statusStr,
            pageRequest.size,
            pageRequest.getOffset()
        );

        Page<UploadedFile> page;
        page.page = pageRequest.page;
        page.size = pageRequest.size;
        page.totalElements = totalElements;
        page.totalPages = static_cast<int>((totalElements + pageRequest.size - 1) / pageRequest.size);

        for (const auto& row : result) {
            page.content.push_back(mapToDomain(row));
        }

        return page;
    }

    std::vector<UploadedFile> findRecent(int limit) override {
        auto client = getClient();
        auto result = client->execSqlSync(
            "SELECT * FROM uploaded_file ORDER BY upload_timestamp DESC LIMIT $1",
            limit
        );

        std::vector<UploadedFile> files;
        for (const auto& row : result) {
            files.push_back(mapToDomain(row));
        }

        return files;
    }

    bool existsByHash(const FileHash& hash) override {
        auto client = getClient();
        auto result = client->execSqlSync(
            "SELECT COUNT(*) FROM uploaded_file WHERE file_hash = $1",
            hash.toString()
        );

        return result[0][0].as<int64_t>() > 0;
    }

    bool deleteById(const UploadId& id) override {
        auto client = getClient();
        auto result = client->execSqlSync(
            "DELETE FROM uploaded_file WHERE id = $1",
            id.toString()
        );

        return result.affectedRows() > 0;
    }

    int64_t count() override {
        auto client = getClient();
        auto result = client->execSqlSync("SELECT COUNT(*) FROM uploaded_file");
        return result[0][0].as<int64_t>();
    }

    int64_t countByStatus(UploadStatus status) override {
        auto client = getClient();
        auto result = client->execSqlSync(
            "SELECT COUNT(*) FROM uploaded_file WHERE status = $1",
            toString(status)
        );
        return result[0][0].as<int64_t>();
    }
};

} // namespace fileupload::infrastructure::repository

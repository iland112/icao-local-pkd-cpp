/**
 * @file UploadFileUseCase.hpp
 * @brief Use case for uploading files (LDIF, Master List)
 */

#pragma once

#include "../command/UploadFileCommand.hpp"
#include "../response/UploadResponse.hpp"
#include "../../domain/model/UploadedFile.hpp"
#include "../../domain/model/FileHash.hpp"
#include "../../domain/model/FileName.hpp"
#include "../../domain/model/FileSize.hpp"
#include "../../domain/model/FileFormat.hpp"
#include "../../domain/repository/IUploadedFileRepository.hpp"
#include "../../domain/port/IFileStoragePort.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <string>

namespace fileupload::application::usecase {

using namespace fileupload::domain::model;
using namespace fileupload::domain::repository;
using namespace fileupload::domain::port;
using namespace fileupload::application::command;
using namespace fileupload::application::response;

/**
 * @brief Use case for uploading LDIF files
 */
class UploadLdifFileUseCase {
private:
    std::shared_ptr<IUploadedFileRepository> repository_;
    std::shared_ptr<IFileStoragePort> fileStorage_;

public:
    UploadLdifFileUseCase(
        std::shared_ptr<IUploadedFileRepository> repository,
        std::shared_ptr<IFileStoragePort> fileStorage
    )
        : repository_(std::move(repository)),
          fileStorage_(std::move(fileStorage)) {}

    /**
     * @brief Execute the use case
     */
    UploadResponse execute(const UploadFileCommand& command) {
        // Validate input
        if (command.isEmpty()) {
            throw shared::exception::ApplicationException(
                "EMPTY_FILE",
                "File content cannot be empty"
            );
        }

        // Compute file hash
        auto fileHash = FileHash::compute(command.content);

        // Check for duplicate
        if (repository_->existsByHash(fileHash)) {
            throw shared::exception::ApplicationException(
                "DUPLICATE_FILE",
                "A file with the same content has already been uploaded"
            );
        }

        // Create domain object
        auto fileName = FileName::of(command.fileName);
        auto fileSize = FileSize::ofBytes(static_cast<int64_t>(command.content.size()));

        auto uploadedFile = UploadedFile::create(
            fileName,
            fileHash,
            fileSize,
            FileFormat::LDIF,
            command.originalFileName,
            command.uploadedBy
        );

        // Store file
        std::string storagePath = fileStorage_->store(
            uploadedFile.getId().toString(),
            command.content
        );
        uploadedFile.setFilePath(storagePath);

        // Save to repository
        auto savedFile = repository_->save(uploadedFile);

        // Build response
        auto response = UploadResponse::fromDomain(savedFile);
        response.message = "LDIF file uploaded successfully. Processing will begin shortly.";

        return response;
    }
};

/**
 * @brief Use case for uploading Master List files
 */
class UploadMasterListUseCase {
private:
    std::shared_ptr<IUploadedFileRepository> repository_;
    std::shared_ptr<IFileStoragePort> fileStorage_;

public:
    UploadMasterListUseCase(
        std::shared_ptr<IUploadedFileRepository> repository,
        std::shared_ptr<IFileStoragePort> fileStorage
    )
        : repository_(std::move(repository)),
          fileStorage_(std::move(fileStorage)) {}

    /**
     * @brief Execute the use case
     */
    UploadResponse execute(const UploadFileCommand& command) {
        // Validate input
        if (command.isEmpty()) {
            throw shared::exception::ApplicationException(
                "EMPTY_FILE",
                "File content cannot be empty"
            );
        }

        // Compute file hash
        auto fileHash = FileHash::compute(command.content);

        // Check for duplicate
        if (repository_->existsByHash(fileHash)) {
            throw shared::exception::ApplicationException(
                "DUPLICATE_FILE",
                "A file with the same content has already been uploaded"
            );
        }

        // Create domain object
        auto fileName = FileName::of(command.fileName);
        auto fileSize = FileSize::ofBytes(static_cast<int64_t>(command.content.size()));

        auto uploadedFile = UploadedFile::create(
            fileName,
            fileHash,
            fileSize,
            FileFormat::ML,
            command.originalFileName,
            command.uploadedBy
        );

        // Store file
        std::string storagePath = fileStorage_->store(
            uploadedFile.getId().toString(),
            command.content
        );
        uploadedFile.setFilePath(storagePath);

        // Save to repository
        auto savedFile = repository_->save(uploadedFile);

        // Build response
        auto response = UploadResponse::fromDomain(savedFile);
        response.message = "Master List file uploaded successfully. Processing will begin shortly.";

        return response;
    }
};

} // namespace fileupload::application::usecase

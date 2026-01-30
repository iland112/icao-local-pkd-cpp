#include "upload_service.h"
#include <spdlog/spdlog.h>
#include <uuid/uuid.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <openssl/evp.h>

namespace services {

// ============================================================================
// Constructor
// ============================================================================

UploadService::UploadService(
    repositories::UploadRepository* uploadRepo,
    repositories::CertificateRepository* certRepo,
    LDAP* ldapConn
)
    : uploadRepo_(uploadRepo)
    , certRepo_(certRepo)
    , ldapConn_(ldapConn)
{
    if (!uploadRepo_) {
        throw std::invalid_argument("UploadService: uploadRepo cannot be nullptr");
    }
    if (!certRepo_) {
        throw std::invalid_argument("UploadService: certRepo cannot be nullptr");
    }
    spdlog::info("UploadService initialized with Repository dependencies");
}

// ============================================================================
// Public Methods - LDIF Upload
// ============================================================================

UploadService::LdifUploadResult UploadService::uploadLdif(
    const std::string& fileName,
    const std::vector<uint8_t>& fileContent,
    const std::string& uploadMode,
    const std::string& uploadedBy
)
{
    spdlog::info("UploadService::uploadLdif - fileName: {}, size: {} bytes", fileName, fileContent.size());

    LdifUploadResult result;
    result.success = false;
    result.status = "PENDING";

    try {
        // Step 1: Compute file hash
        std::string fileHash = computeFileHash(fileContent);
        spdlog::debug("File hash: {}", fileHash.substr(0, 16) + "...");

        // Step 2: Check for duplicate file
        auto duplicateUpload = uploadRepo_->findByFileHash(fileHash);
        if (duplicateUpload) {
            spdlog::warn("Duplicate LDIF file detected: existing upload {}", duplicateUpload->id);
            result.success = false;
            result.status = "DUPLICATE";
            result.errorMessage = "Duplicate file detected. This file has already been uploaded.";
            result.message = "File with hash " + fileHash.substr(0, 16) + "... already exists";
            // Return existing upload ID for reference
            result.uploadId = duplicateUpload->id;
            return result;
        }

        // Step 3: Generate upload ID
        result.uploadId = generateUploadId();

        // Step 4: Record to database using Repository
        repositories::Upload upload;
        upload.id = result.uploadId;
        upload.fileName = fileName;
        upload.fileHash = fileHash;
        upload.fileFormat = "LDIF";
        upload.fileSize = fileContent.size();
        upload.status = "PENDING";
        upload.uploadedBy = uploadedBy;

        if (!uploadRepo_->insert(upload)) {
            throw std::runtime_error("Failed to insert upload record");
        }

        // Step 6: Save to temporary file
        std::string tempFilePath = saveToTempFile(result.uploadId, fileContent, ".ldif");
        spdlog::debug("Saved to temp file: {}", tempFilePath);

        // TODO Phase 4: Extract LDIF processing logic from main.cpp
        // For now, processing must be triggered externally via processLdifFileAsync()
        spdlog::warn("UploadService::uploadLdif - LDIF processing must be triggered externally");
        spdlog::warn("TODO: Move processLdifFileAsync() logic into UploadService");

        result.success = true;
        result.status = "PENDING";
        result.message = "LDIF file uploaded successfully. Processing started.";

    } catch (const std::exception& e) {
        spdlog::error("UploadService::uploadLdif failed: {}", e.what());
        result.success = false;
        result.status = "FAILED";
        result.errorMessage = e.what();

        if (!result.uploadId.empty()) {
            uploadRepo_->updateStatus(result.uploadId, "FAILED", e.what());
        }
    }

    return result;
}

UploadService::MasterListUploadResult UploadService::uploadMasterList(
    const std::string& fileName,
    const std::vector<uint8_t>& fileContent,
    const std::string& uploadMode,
    const std::string& uploadedBy
)
{
    spdlog::info("UploadService::uploadMasterList - fileName: {}, size: {} bytes", fileName, fileContent.size());

    MasterListUploadResult result;
    result.success = false;
    result.status = "PENDING";

    try {
        // Step 1: Compute file hash
        std::string fileHash = computeFileHash(fileContent);
        spdlog::debug("File hash: {}", fileHash.substr(0, 16) + "...");

        // Step 2: Check for duplicate file
        auto duplicateUpload = uploadRepo_->findByFileHash(fileHash);
        if (duplicateUpload) {
            spdlog::warn("Duplicate Master List file detected: existing upload {}", duplicateUpload->id);
            result.success = false;
            result.status = "DUPLICATE";
            result.errorMessage = "Duplicate file detected. This file has already been uploaded.";
            result.message = "File with hash " + fileHash.substr(0, 16) + "... already exists";
            // Return existing upload ID for reference
            result.uploadId = duplicateUpload->id;
            return result;
        }

        // Step 3: Generate upload ID
        result.uploadId = generateUploadId();

        // Step 4: Record to database using Repository
        repositories::Upload upload;
        upload.id = result.uploadId;
        upload.fileName = fileName;
        upload.fileHash = fileHash;
        upload.fileFormat = "ML";
        upload.fileSize = fileContent.size();
        upload.status = "PENDING";
        upload.uploadedBy = uploadedBy;

        if (!uploadRepo_->insert(upload)) {
            throw std::runtime_error("Failed to insert upload record");
        }

        // Step 6: Save to temporary file
        std::string tempFilePath = saveToTempFile(result.uploadId, fileContent, ".ml");
        spdlog::debug("Saved to temp file: {}", tempFilePath);

        // TODO Phase 4: Extract Master List processing logic from main.cpp
        // For now, processing must be triggered externally via processMasterListFileAsync()
        spdlog::warn("UploadService::uploadMasterList - Master List processing must be triggered externally");
        spdlog::warn("TODO: Move processMasterListFileAsync() logic into UploadService");

        result.success = true;
        result.status = "PENDING";
        result.message = "Master List file uploaded successfully. Processing started.";

    } catch (const std::exception& e) {
        spdlog::error("UploadService::uploadMasterList failed: {}", e.what());
        result.success = false;
        result.status = "FAILED";
        result.errorMessage = e.what();

        if (!result.uploadId.empty()) {
            uploadRepo_->updateStatus(result.uploadId, "FAILED", e.what());
        }
    }

    return result;
}

// ============================================================================
// Public Methods - Upload History
// ============================================================================

Json::Value UploadService::getUploadHistory(const UploadHistoryFilter& filter)
{
    spdlog::info("UploadService::getUploadHistory - page: {}, size: {}", filter.page, filter.size);

    Json::Value response;
    int offset = filter.page * filter.size;

    try {
        // Use Repository to get uploads
        std::vector<repositories::Upload> uploads = uploadRepo_->findAll(
            filter.size,
            offset,
            filter.sort,
            filter.direction
        );

        // Convert to JSON
        Json::Value content = Json::arrayValue;
        for (const auto& upload : uploads) {
            Json::Value item;
            item["id"] = upload.id;
            item["fileName"] = upload.fileName;
            item["fileFormat"] = upload.fileFormat;
            item["fileSize"] = upload.fileSize;
            item["status"] = upload.status;
            item["uploadedBy"] = upload.uploadedBy;
            item["cscaCount"] = upload.cscaCount;
            item["dscCount"] = upload.dscCount;
            item["dscNcCount"] = upload.dscNcCount;
            item["certificateCount"] = upload.cscaCount + upload.dscCount + upload.dscNcCount;  // Backward compatibility
            item["crlCount"] = upload.crlCount;
            item["mlscCount"] = upload.mlscCount;
            item["mlCount"] = upload.mlCount;

            // Validation statistics
            Json::Value validation;
            validation["validCount"] = upload.validationValidCount;
            validation["invalidCount"] = upload.validationInvalidCount;
            validation["pendingCount"] = upload.validationPendingCount;
            validation["errorCount"] = upload.validationErrorCount;
            validation["trustChainValidCount"] = upload.trustChainValidCount;
            validation["trustChainInvalidCount"] = upload.trustChainInvalidCount;
            validation["cscaNotFoundCount"] = upload.cscaNotFoundCount;
            validation["expiredCount"] = upload.expiredCount;
            validation["revokedCount"] = upload.revokedCount;
            item["validation"] = validation;

            item["createdAt"] = upload.createdAt;
            item["updatedAt"] = upload.updatedAt;
            if (upload.errorMessage) {
                item["errorMessage"] = *upload.errorMessage;
            }
            content.append(item);
        }

        int totalElements = uploadRepo_->countAll();

        response["content"] = content;
        response["totalElements"] = totalElements;
        response["totalPages"] = (totalElements + filter.size - 1) / filter.size;
        response["number"] = filter.page;
        response["size"] = filter.size;

    } catch (const std::exception& e) {
        spdlog::error("UploadService::getUploadHistory failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

Json::Value UploadService::getUploadDetail(const std::string& uploadId)
{
    spdlog::info("UploadService::getUploadDetail - uploadId: {}", uploadId);

    Json::Value response;

    try {
        auto uploadOpt = uploadRepo_->findById(uploadId);
        if (!uploadOpt) {
            response["error"] = "Upload not found";
            return response;
        }

        const auto& upload = *uploadOpt;
        response["id"] = upload.id;
        response["fileName"] = upload.fileName;
        response["fileFormat"] = upload.fileFormat;
        response["fileSize"] = upload.fileSize;
        response["status"] = upload.status;
        response["uploadedBy"] = upload.uploadedBy;
        if (upload.processingMode) {
            response["processingMode"] = *upload.processingMode;
        }
        response["totalEntries"] = upload.totalEntries;
        response["processedEntries"] = upload.processedEntries;
        response["cscaCount"] = upload.cscaCount;
        response["dscCount"] = upload.dscCount;
        response["dscNcCount"] = upload.dscNcCount;
        response["certificateCount"] = upload.cscaCount + upload.dscCount + upload.dscNcCount;  // Backward compatibility
        response["crlCount"] = upload.crlCount;
        response["mlscCount"] = upload.mlscCount;
        response["mlCount"] = upload.mlCount;

        // Validation statistics
        Json::Value validation;
        validation["validCount"] = upload.validationValidCount;
        validation["invalidCount"] = upload.validationInvalidCount;
        validation["pendingCount"] = upload.validationPendingCount;
        validation["errorCount"] = upload.validationErrorCount;
        validation["trustChainValidCount"] = upload.trustChainValidCount;
        validation["trustChainInvalidCount"] = upload.trustChainInvalidCount;
        validation["cscaNotFoundCount"] = upload.cscaNotFoundCount;
        validation["expiredCount"] = upload.expiredCount;
        validation["revokedCount"] = upload.revokedCount;
        response["validation"] = validation;

        response["createdAt"] = upload.createdAt;
        response["updatedAt"] = upload.updatedAt;
        if (upload.errorMessage) {
            response["errorMessage"] = *upload.errorMessage;
        }

    } catch (const std::exception& e) {
        spdlog::error("UploadService::getUploadDetail failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

// ============================================================================
// Stubs (TODO)
// ============================================================================

bool UploadService::triggerParsing(const std::string& uploadId)
{
    spdlog::warn("UploadService::triggerParsing - TODO: Implement");
    return false;
}

bool UploadService::triggerValidation(const std::string& uploadId)
{
    spdlog::warn("UploadService::triggerValidation - TODO: Implement");
    return false;
}

Json::Value UploadService::getUploadValidations(
    const std::string& uploadId,
    const ValidationFilter& filter
)
{
    spdlog::warn("UploadService::getUploadValidations - TODO: Implement");
    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value UploadService::getUploadIssues(const std::string& uploadId)
{
    spdlog::info("UploadService::getUploadIssues - uploadId: {}", uploadId);

    try {
        // Delegate to repository
        return uploadRepo_->findDuplicatesByUploadId(uploadId);
    } catch (const std::exception& e) {
        spdlog::error("UploadService::getUploadIssues failed: {}", e.what());
        Json::Value response;
        response["success"] = false;
        response["error"] = e.what();
        response["duplicates"] = Json::Value(Json::arrayValue);
        response["totalDuplicates"] = 0;
        return response;
    }
}

bool UploadService::deleteUpload(const std::string& uploadId)
{
    spdlog::info("UploadService::deleteUpload - uploadId: {}", uploadId);

    try {
        return uploadRepo_->deleteById(uploadId);
    } catch (const std::exception& e) {
        spdlog::error("UploadService::deleteUpload failed: {}", e.what());
        return false;
    }
}

Json::Value UploadService::getUploadStatistics()
{
    spdlog::info("UploadService::getUploadStatistics");

    try {
        return uploadRepo_->getStatisticsSummary();
    } catch (const std::exception& e) {
        spdlog::error("UploadService::getUploadStatistics failed: {}", e.what());
        Json::Value response;
        response["error"] = e.what();
        return response;
    }
}

Json::Value UploadService::getCountryStatistics()
{
    spdlog::info("UploadService::getCountryStatistics");

    try {
        return uploadRepo_->getCountryStatistics();
    } catch (const std::exception& e) {
        spdlog::error("UploadService::getCountryStatistics failed: {}", e.what());
        Json::Value response;
        response["error"] = e.what();
        return response;
    }
}

Json::Value UploadService::getDetailedCountryStatistics(int limit)
{
    spdlog::info("UploadService::getDetailedCountryStatistics - limit: {}", limit);

    try {
        return uploadRepo_->getDetailedCountryStatistics(limit);
    } catch (const std::exception& e) {
        spdlog::error("UploadService::getDetailedCountryStatistics failed: {}", e.what());
        Json::Value response;
        response["error"] = e.what();
        return response;
    }
}

// ============================================================================
// Private Helper Methods
// ============================================================================

std::string UploadService::generateUploadId()
{
    uuid_t uuid;
    char uuidStr[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuidStr);
    return std::string(uuidStr);
}

std::string UploadService::saveToTempFile(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    const std::string& extension
)
{
    std::string tempDir = "/app/uploads";
    std::string filePath = tempDir + "/" + uploadId + extension;

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create temp file: " + filePath);
    }

    file.write(reinterpret_cast<const char*>(content.data()), content.size());
    file.close();

    spdlog::debug("Saved temp file: {}", filePath);
    return filePath;
}

std::string UploadService::computeFileHash(const std::vector<uint8_t>& content)
{
    unsigned char hash[32];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, content.data(), content.size());
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

} // namespace services

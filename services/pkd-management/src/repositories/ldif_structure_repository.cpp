/**
 * @file ldif_structure_repository.cpp
 * @brief LDIF Structure Repository Implementation
 *
 * @author SmartCore Inc.
 * @date 2026-01-31
 */

#include "ldif_structure_repository.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <stdexcept>

namespace repositories {

// --- LdifStructureData Methods ---

Json::Value LdifStructureData::toJson() const {
    Json::Value root;

    // Entries array
    Json::Value entriesArray(Json::arrayValue);
    for (const auto& entry : entries) {
        Json::Value entryObj;
        entryObj["dn"] = entry.dn;
        entryObj["objectClass"] = entry.objectClass;
        entryObj["lineNumber"] = entry.lineNumber;

        // Attributes array
        Json::Value attrsArray(Json::arrayValue);
        for (const auto& attr : entry.attributes) {
            Json::Value attrObj;
            attrObj["name"] = attr.name;
            attrObj["value"] = attr.value;
            attrObj["isBinary"] = attr.isBinary;
            if (attr.isBinary) {
                attrObj["binarySize"] = static_cast<Json::Int64>(attr.binarySize);
            }
            attrsArray.append(attrObj);
        }
        entryObj["attributes"] = attrsArray;

        entriesArray.append(entryObj);
    }
    root["entries"] = entriesArray;

    // Statistics
    root["totalEntries"] = totalEntries;
    root["displayedEntries"] = displayedEntries;
    root["totalAttributes"] = totalAttributes;
    root["truncated"] = truncated;

    // ObjectClass counts
    Json::Value objectClassObj;
    for (const auto& [className, count] : objectClassCounts) {
        objectClassObj[className] = count;
    }
    root["objectClassCounts"] = objectClassObj;

    return root;
}

// --- LdifStructureRepository Methods ---

LdifStructureRepository::LdifStructureRepository(UploadRepository* uploadRepo)
    : uploadRepository_(uploadRepo) {
    if (!uploadRepository_) {
        throw std::invalid_argument("UploadRepository cannot be null");
    }
}

LdifStructureData LdifStructureRepository::getLdifStructure(
    const std::string& uploadId,
    int maxEntries
) {
    spdlog::info("LdifStructureRepository: Getting LDIF structure for upload {} (maxEntries: {})",
                 uploadId, maxEntries);

    // 1. Get upload record from database
    auto uploadOpt = uploadRepository_->findById(uploadId);
    if (!uploadOpt.has_value()) {
        throw std::runtime_error("Upload not found: " + uploadId);
    }

    const Upload& upload = uploadOpt.value();

    // 2. Validate LDIF format
    validateLdifFormat(upload);

    // 3. Resolve file path
    std::string filePath = resolveFilePath(upload);
    spdlog::debug("Resolved file path: {}", filePath);

    // 4. Parse LDIF file
    icao::ldif::LdifStructure parsedStructure;
    try {
        parsedStructure = icao::ldif::LdifParser::parse(filePath, maxEntries);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse LDIF file {}: {}", filePath, e.what());
        throw std::runtime_error("LDIF parsing failed: " + std::string(e.what()));
    }

    // 5. Convert to LdifStructureData
    LdifStructureData result;
    result.entries = std::move(parsedStructure.entries);
    result.totalEntries = parsedStructure.totalEntries;
    result.displayedEntries = static_cast<int>(result.entries.size());
    result.totalAttributes = parsedStructure.totalAttributes;
    result.objectClassCounts = std::move(parsedStructure.objectClassCounts);
    result.truncated = parsedStructure.truncated;

    spdlog::info("LDIF structure retrieved: {} entries (total: {})",
                 result.displayedEntries, result.totalEntries);

    return result;
}

std::string LdifStructureRepository::resolveFilePath(const Upload& upload) {
    // Files are stored as {uploadId}.ldif in /app/uploads/
    // This matches the Master List structure endpoint pattern

    std::string basePath = "/app/uploads";
    std::string filePath = basePath + "/" + upload.id + ".ldif";

    spdlog::debug("Constructed file path: {}", filePath);

    // Check if file exists
    if (!std::filesystem::exists(filePath)) {
        throw std::runtime_error("LDIF file not found: " + filePath);
    }

    return filePath;
}

void LdifStructureRepository::validateLdifFormat(const Upload& upload) {
    // Check if file format is LDIF
    if (upload.fileFormat != "LDIF") {
        throw std::runtime_error(
            "Invalid file format: expected LDIF, got " + upload.fileFormat
        );
    }

    // Check if upload is completed
    if (upload.status != "COMPLETED" && upload.status != "PARSING" && upload.status != "PARSED") {
        spdlog::warn("Upload {} is in status: {} (not COMPLETED)", upload.id, upload.status);
        // Don't throw - allow viewing structure even if processing failed
    }
}

}  // namespace repositories

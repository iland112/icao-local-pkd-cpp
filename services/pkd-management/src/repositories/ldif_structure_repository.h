/**
 * @file ldif_structure_repository.h
 * @brief LDIF Structure Repository - Data Access Layer for LDIF file structure
 *
 * Handles file system operations and LDIF parsing for structure visualization.
 * Does NOT interact with database (read-only file access).
 *
 * @author SmartCore Inc.
 * @date 2026-01-31
 */

#pragma once

#include <string>
#include <memory>
#include <json/json.h>
#include "../common/ldif_parser.h"
#include "upload_repository.h"

namespace repositories {

/**
 * @brief LDIF Structure Data (JSON-serializable)
 */
struct LdifStructureData {
    std::vector<icao::ldif::LdifEntryStructure> entries;
    int totalEntries;
    int displayedEntries;
    int totalAttributes;
    std::map<std::string, int> objectClassCounts;
    bool truncated;

    /**
     * @brief Convert to JSON
     */
    Json::Value toJson() const;
};

/**
 * @brief Repository for LDIF file structure access
 *
 * Responsibilities:
 * - Get file path from UploadRepository
 * - Parse LDIF file using LdifParser
 * - Return structured data for visualization
 */
class LdifStructureRepository {
public:
    /**
     * @brief Constructor
     * @param uploadRepo Upload repository for file path lookup
     */
    explicit LdifStructureRepository(UploadRepository* uploadRepo);

    ~LdifStructureRepository() = default;

    /**
     * @brief Get LDIF file structure
     *
     * @param uploadId Upload UUID
     * @param maxEntries Maximum number of entries to parse (default: 100)
     * @return LdifStructureData with parsed structure
     * @throws std::runtime_error if upload not found, not LDIF, or parse error
     */
    LdifStructureData getLdifStructure(const std::string& uploadId, int maxEntries = 100);

private:
    UploadRepository* uploadRepository_;

    /**
     * @brief Resolve file path from upload ID
     *
     * @param upload Upload entity
     * @return Absolute file path
     * @throws std::runtime_error if file not found
     */
    std::string resolveFilePath(const Upload& upload);

    /**
     * @brief Validate that upload is LDIF format
     *
     * @param upload Upload entity
     * @throws std::runtime_error if not LDIF
     */
    void validateLdifFormat(const Upload& upload);
};

}  // namespace repositories

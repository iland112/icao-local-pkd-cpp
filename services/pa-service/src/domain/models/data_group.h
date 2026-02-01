/**
 * @file data_group.h
 * @brief Domain model for ICAO 9303 Data Group
 *
 * Represents a single data group with hash verification result.
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>

namespace domain {
namespace models {

/**
 * @brief Data Group domain model
 *
 * Represents a single data group (DG1, DG2, etc.) with:
 * - Expected hash (from SOD)
 * - Actual hash (computed from data)
 * - Validation result
 * - Optional raw data
 */
struct DataGroup {
    // Data group identification
    std::string dgNumber;  // "DG1", "DG2", ..., "DG15"
    int dgTag = 0;  // ASN.1 tag number (e.g., 0x61 for DG1)

    // Hash verification
    std::string expectedHash;  // From SOD (hex-encoded)
    std::string actualHash;    // Computed from data (hex-encoded)
    bool hashValid = false;    // expectedHash == actualHash

    // Hash algorithm used
    std::string hashAlgorithm;  // "SHA-1", "SHA-256", etc.

    // Data group content (optional - may be large for DG2)
    std::optional<std::vector<uint8_t>> rawData;
    size_t dataSize = 0;  // Size in bytes

    // Parsing status
    bool parsingSuccess = false;
    std::optional<std::string> parsingErrors;

    // Content type (for DG2, DG3, DG4)
    std::optional<std::string> contentType;  // "JPEG2000", "JPEG", etc.

    /**
     * @brief Convert to JSON for API response
     * @param includeRawData Whether to include raw data in response
     * @return Json::Value representation
     */
    Json::Value toJson(bool includeRawData = false) const;

    /**
     * @brief Create from raw data
     * @param dgNumber Data group number
     * @param data Raw data bytes
     * @param expectedHash Expected hash from SOD
     * @param hashAlgorithm Hash algorithm to use
     * @return DataGroup instance
     */
    static DataGroup fromRawData(
        const std::string& dgNumber,
        const std::vector<uint8_t>& data,
        const std::string& expectedHash,
        const std::string& hashAlgorithm
    );

    /**
     * @brief Compute hash of raw data
     * @param hashAlgorithm Hash algorithm ("SHA-1", "SHA-256", etc.)
     * @return Hex-encoded hash
     */
    std::string computeHash(const std::string& hashAlgorithm) const;

    /**
     * @brief Verify hash matches expected value
     * @return true if actualHash matches expectedHash
     */
    bool verifyHash() const {
        return !expectedHash.empty() &&
               !actualHash.empty() &&
               expectedHash == actualHash;
    }

    /**
     * @brief Get data group description
     * @return Human-readable description (e.g., "Machine Readable Zone")
     */
    std::string getDescription() const;
};

/**
 * @brief Data Group validation result (for multiple DGs)
 */
struct DataGroupValidationResult {
    int totalGroups = 0;
    int validGroups = 0;
    int invalidGroups = 0;
    std::vector<DataGroup> dataGroups;

    /**
     * @brief Convert to JSON
     */
    Json::Value toJson() const {
        Json::Value json;
        json["totalGroups"] = totalGroups;
        json["validGroups"] = validGroups;
        json["invalidGroups"] = invalidGroups;

        Json::Value dgArray = Json::arrayValue;
        for (const auto& dg : dataGroups) {
            dgArray.append(dg.toJson(false));
        }
        json["dataGroups"] = dgArray;

        return json;
    }
};

} // namespace models
} // namespace domain

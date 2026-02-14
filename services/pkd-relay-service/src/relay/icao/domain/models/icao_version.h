/**
 * @file icao_version.h
 * @brief ICAO PKD version metadata domain model
 */
#pragma once

#include <string>
#include <ctime>
#include <optional>

namespace icao {
namespace relay {
namespace icao_module {
namespace domain {
namespace models {

/**
 * @brief ICAO PKD Version metadata
 *
 * Represents a detected version of ICAO PKD LDIF files from the public portal.
 * Tracks lifecycle: DETECTED → NOTIFIED → DOWNLOADED → IMPORTED
 */
struct IcaoVersion {
    int id;
    std::string collectionType;  // "DSC_CRL" or "MASTERLIST"
    std::string fileName;         // "icaopkd-001-dsccrl-005973.ldif"
    int fileVersion;              // 5973

    // Timestamps
    std::string detectedAt;       // ISO 8601 format
    std::optional<std::string> downloadedAt;
    std::optional<std::string> importedAt;

    // Status
    std::string status;           // "DETECTED", "NOTIFIED", "DOWNLOADED", "IMPORTED", "FAILED"

    // Notification
    bool notificationSent;
    std::optional<std::string> notificationSentAt;

    // Link to upload
    std::optional<std::string> importUploadId;  // UUID string format
    std::optional<int> certificateCount;
    std::optional<std::string> errorMessage;

    /**
     * @brief Factory method to create from ICAO portal HTML parsing
     */
    static IcaoVersion createDetected(const std::string& collectionType,
                                      const std::string& fileName,
                                      int fileVersion) {
        IcaoVersion version;
        version.id = 0;  // Will be set by database
        version.collectionType = collectionType;
        version.fileName = fileName;
        version.fileVersion = fileVersion;
        version.status = "DETECTED";
        version.notificationSent = false;
        return version;
    }

    /**
     * @brief Check if this version is newer than another
     */
    bool isNewerThan(const IcaoVersion& other) const {
        if (collectionType != other.collectionType) {
            return false;
        }
        return fileVersion > other.fileVersion;
    }

    /**
     * @brief Get human-readable status description
     */
    std::string getStatusDescription() const {
        if (status == "DETECTED") return "New version detected, awaiting download";
        if (status == "NOTIFIED") return "Notification sent to administrator";
        if (status == "DOWNLOADED") return "Downloaded from ICAO portal";
        if (status == "IMPORTED") return "Successfully imported to system";
        if (status == "FAILED") return "Import failed: " + errorMessage.value_or("Unknown error");
        return "Unknown status";
    }
};

} // namespace models
} // namespace domain
} // namespace icao_module
} // namespace relay
} // namespace icao

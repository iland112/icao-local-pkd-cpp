#pragma once

#include <string>
#include <vector>
#include <optional>
#include <libpq-fe.h>
#include "../domain/models/icao_version.h"

namespace repositories {

/**
 * @brief Repository for ICAO PKD version tracking
 *
 * Handles database operations for the icao_pkd_versions table.
 * Provides CRUD operations and business-specific queries.
 */
class IcaoVersionRepository {
public:
    /**
     * @brief Constructor
     * @param connInfo PostgreSQL connection string
     */
    explicit IcaoVersionRepository(const std::string& connInfo);
    ~IcaoVersionRepository();

    /**
     * @brief Insert a new detected version
     * @return true if inserted successfully, false if duplicate or error
     */
    bool insert(const domain::models::IcaoVersion& version);

    /**
     * @brief Update version status
     */
    bool updateStatus(const std::string& fileName, const std::string& newStatus);

    /**
     * @brief Mark notification as sent
     */
    bool markNotificationSent(const std::string& fileName);

    /**
     * @brief Link version to uploaded file
     */
    bool linkToUpload(const std::string& fileName, const std::string& uploadId,
                     int certificateCount);

    /**
     * @brief Check if version already exists in database
     */
    bool exists(const std::string& collectionType, int fileVersion);

    /**
     * @brief Get version by file name
     */
    std::optional<domain::models::IcaoVersion> getByFileName(const std::string& fileName);

    /**
     * @brief Get latest version for each collection type
     * @return Vector with at most 2 elements (DSC_CRL and MASTERLIST)
     */
    std::vector<domain::models::IcaoVersion> getLatest();

    /**
     * @brief Get version history (most recent first)
     * @param limit Maximum number of records to return
     */
    std::vector<domain::models::IcaoVersion> getHistory(int limit = 10);

    /**
     * @brief Get all local versions for comparison with remote
     */
    std::vector<domain::models::IcaoVersion> getAllVersions();

private:
    std::string connInfo_;

    /**
     * @brief Helper to convert PGresult row to IcaoVersion object
     */
    domain::models::IcaoVersion resultToVersion(PGresult* res, int row);

    /**
     * @brief Helper to get optional string from PGresult
     */
    std::optional<std::string> getOptionalString(PGresult* res, int row, int col);

    /**
     * @brief Helper to get optional int from PGresult
     */
    std::optional<int> getOptionalInt(PGresult* res, int row, int col);
};

} // namespace repositories

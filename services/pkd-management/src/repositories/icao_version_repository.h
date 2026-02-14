#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"
#include "../domain/models/icao_version.h"

namespace repositories {

/**
 * @brief Repository for ICAO PKD version tracking
 *
 * Handles database operations for the icao_pkd_versions table.
 * Provides CRUD operations and business-specific queries.
 *
 * Uses IQueryExecutor for database-agnostic operation (PostgreSQL + Oracle).
 */
class IcaoVersionRepository {
public:
    /**
     * @brief Constructor
     * @param executor Query executor (PostgreSQL or Oracle, non-owning pointer)
     */
    explicit IcaoVersionRepository(common::IQueryExecutor* executor);
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

    /**
     * @brief Get version comparison status (detected vs uploaded)
     * @return Vector of tuples: (collection_type, detected_version, uploaded_version, upload_timestamp)
     */
    std::vector<std::tuple<std::string, int, int, std::string>> getVersionComparison();

private:
    common::IQueryExecutor* executor_;  // Query executor (non-owning)

    /**
     * @brief Helper to convert JSON row to IcaoVersion object
     */
    domain::models::IcaoVersion jsonToVersion(const Json::Value& row);

    /**
     * @brief Helper to get optional string from JSON value
     */
    std::optional<std::string> getOptionalString(const Json::Value& val);

    /**
     * @brief Helper to get optional int from JSON value
     */
    std::optional<int> getOptionalInt(const Json::Value& val);

    /**
     * @brief Helper to parse int from JSON value (handles Oracle string returns)
     */
    int getInt(const Json::Value& val, int defaultVal = 0);
};

} // namespace repositories

/**
 * @file icao_version_repository.h
 * @brief Repository for ICAO PKD version tracking
 */
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <libpq-fe.h>
#include "../domain/models/icao_version.h"

namespace icao {
namespace relay {
namespace icao_module {
namespace repositories {

/**
 * @brief Repository for ICAO PKD version tracking
 *
 * Handles database operations for the icao_pkd_versions table.
 * Provides CRUD operations and business-specific queries.
 */
class icao::relay::icao_module::domain::models::IcaoVersionRepository {
public:
    /**
     * @brief Constructor
     * @param connInfo PostgreSQL connection string
     */
    explicit icao::relay::icao_module::domain::models::IcaoVersionRepository(const std::string& connInfo);
    ~icao::relay::icao_module::domain::models::IcaoVersionRepository();

    /**
     * @brief Insert a new detected version
     * @return true if inserted successfully, false if duplicate or error
     */
    bool insert(const icao::relay::icao_module::domain::models::IcaoVersion& version);

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
    std::optional<icao::relay::icao_module::domain::models::IcaoVersion> getByFileName(const std::string& fileName);

    /**
     * @brief Get latest version for each collection type
     * @return Vector with at most 2 elements (DSC_CRL and MASTERLIST)
     */
    std::vector<icao::relay::icao_module::domain::models::IcaoVersion> getLatest();

    /**
     * @brief Get version history (most recent first)
     * @param limit Maximum number of records to return
     */
    std::vector<icao::relay::icao_module::domain::models::IcaoVersion> getHistory(int limit = 10);

    /**
     * @brief Get all local versions for comparison with remote
     */
    std::vector<icao::relay::icao_module::domain::models::IcaoVersion> getAllVersions();

    /**
     * @brief Get version comparison status (detected vs uploaded)
     * @return Vector of tuples: (collection_type, detected_version, uploaded_version, upload_timestamp)
     */
    std::vector<std::tuple<std::string, int, int, std::string>> getVersionComparison();

private:
    std::string connInfo_;

    /**
     * @brief Helper to convert PGresult row to icao::relay::icao_module::domain::models::IcaoVersion object
     */
    icao::relay::icao_module::domain::models::IcaoVersion resultToVersion(PGresult* res, int row);

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
} // namespace icao_module
} // namespace relay
} // namespace icao

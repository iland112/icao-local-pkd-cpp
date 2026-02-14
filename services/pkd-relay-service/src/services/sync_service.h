/**
 * @file sync_service.h
 * @brief Service layer for DB-LDAP sync status operations
 */
#pragma once

#include "../repositories/sync_status_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include <memory>
#include <json/json.h>

namespace icao::relay::services {

/**
 * @brief Service layer for sync status operations
 *
 * Provides business logic for DB-LDAP synchronization monitoring.
 * Uses Repository pattern for database access.
 */
class SyncService {
public:
    /**
     * @brief Constructor with dependency injection
     * @param syncStatusRepo SyncStatusRepository instance
     * @param certificateRepo CertificateRepository instance
     * @param crlRepo CrlRepository instance
     */
    SyncService(
        std::shared_ptr<repositories::SyncStatusRepository> syncStatusRepo,
        std::shared_ptr<repositories::CertificateRepository> certificateRepo,
        std::shared_ptr<repositories::CrlRepository> crlRepo
    );

    /**
     * @brief Get current sync status (latest check)
     * @return JSON response with sync status or error
     */
    Json::Value getCurrentStatus();

    /**
     * @brief Get sync history with pagination
     * @param limit Maximum number of results (default: 50)
     * @param offset Number of results to skip (default: 0)
     * @return JSON response with history array and pagination info
     */
    Json::Value getSyncHistory(int limit = 50, int offset = 0);

    /**
     * @brief Perform manual sync check and save result
     *
     * This method:
     * 1. Counts certificates/CRLs in DB by type
     * 2. Counts certificates/CRLs in LDAP by type (via LDAP operations)
     * 3. Calculates discrepancies
     * 4. Saves sync status to database
     *
     * @param dbCounts Database counts by type (from caller)
     * @param ldapCounts LDAP counts by type (from caller)
     * @param countryStats Country-level statistics (optional)
     * @return JSON response with new sync status
     */
    Json::Value performSyncCheck(
        const Json::Value& dbCounts,
        const Json::Value& ldapCounts,
        const Json::Value& countryStats = Json::Value(Json::objectValue)
    );

    /**
     * @brief Get sync statistics summary
     * @return JSON response with statistics
     */
    Json::Value getSyncStatistics();

private:
    /**
     * @brief Calculate discrepancies between DB and LDAP counts
     * @param dbCounts Database counts
     * @param ldapCounts LDAP counts
     * @return Discrepancy values by type
     */
    Json::Value calculateDiscrepancies(
        const Json::Value& dbCounts,
        const Json::Value& ldapCounts
    );

    /**
     * @brief Convert SyncStatus domain object to JSON
     * @param syncStatus Domain object
     * @return JSON representation
     */
    Json::Value syncStatusToJson(const domain::SyncStatus& syncStatus);

    std::shared_ptr<repositories::SyncStatusRepository> syncStatusRepo_;
    std::shared_ptr<repositories::CertificateRepository> certificateRepo_;
    std::shared_ptr<repositories::CrlRepository> crlRepo_;
};

} // namespace icao::relay::services

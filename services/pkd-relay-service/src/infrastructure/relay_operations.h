#pragma once

/**
 * @file relay_operations.h
 * @brief Core relay operations extracted from main.cpp
 *
 * Contains database/LDAP statistics gathering, sync check,
 * and Config::loadFromDatabase() implementation.
 *
 * @date 2026-02-17
 */

#include "relay/sync/common/types.h"
#include "relay/sync/common/config.h"
#include "i_query_executor.h"
#include <ldap_connection_pool.h>
#include <json/json.h>

namespace icao::relay::repositories {
    class SyncStatusRepository;
}

namespace infrastructure {

/**
 * @brief Get database certificate/CRL statistics
 * @param executor Query executor for database access
 * @return Database statistics (counts by type, country breakdown)
 */
icao::relay::DbStats getDbStats(common::IQueryExecutor* executor);

/**
 * @brief Get LDAP directory certificate/CRL statistics
 * @param ldapPool LDAP connection pool
 * @param config Service configuration (for base DNs)
 * @return LDAP statistics (counts by type, country breakdown)
 */
icao::relay::LdapStats getLdapStats(common::LdapConnectionPool* ldapPool,
                                     const icao::relay::Config& config);

/**
 * @brief Save sync check result to database
 * @param result Sync result to save
 * @param syncStatusRepo Repository for sync status persistence
 * @return Sync status ID (0 for UUID, -1 for error)
 */
int saveSyncStatus(const icao::relay::SyncResult& result,
                   icao::relay::repositories::SyncStatusRepository* syncStatusRepo);

/**
 * @brief Perform complete sync check (DB stats + LDAP stats + save)
 * @param executor Query executor for database access
 * @param ldapPool LDAP connection pool
 * @param config Service configuration
 * @param syncStatusRepo Repository for sync status persistence
 * @return Sync result with all statistics and discrepancies
 */
icao::relay::SyncResult performSyncCheck(common::IQueryExecutor* executor,
                                          common::LdapConnectionPool* ldapPool,
                                          const icao::relay::Config& config,
                                          icao::relay::repositories::SyncStatusRepository* syncStatusRepo);

/**
 * @brief Get revalidation history from database
 * @param executor Query executor for database access
 * @param limit Maximum number of results (default: 10)
 * @return JSON array of revalidation history entries
 */
Json::Value getRevalidationHistory(common::IQueryExecutor* executor, int limit = 10);

/**
 * @brief Format scheduled time as HH:MM string
 * @param targetHour Hour (0-23)
 * @param targetMinute Minute (0-59)
 * @return Formatted time string
 */
std::string formatScheduledTime(int targetHour, int targetMinute);

} // namespace infrastructure

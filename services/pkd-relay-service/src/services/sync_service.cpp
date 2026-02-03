#include "sync_service.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace icao::relay::services {

SyncService::SyncService(
    std::shared_ptr<repositories::SyncStatusRepository> syncStatusRepo,
    std::shared_ptr<repositories::CertificateRepository> certificateRepo,
    std::shared_ptr<repositories::CrlRepository> crlRepo
)
    : syncStatusRepo_(syncStatusRepo),
      certificateRepo_(certificateRepo),
      crlRepo_(crlRepo)
{
    spdlog::info("[SyncService] Initialized with repository dependencies");
}

Json::Value SyncService::getCurrentStatus() {
    Json::Value response;

    try {
        auto syncStatus = syncStatusRepo_->findLatest();

        if (!syncStatus.has_value()) {
            response["success"] = false;
            response["message"] = "No sync status found. Run sync check first.";
            return response;
        }

        response["success"] = true;
        response["data"] = syncStatusToJson(syncStatus.value());
        return response;

    } catch (const std::exception& e) {
        spdlog::error("[SyncService] Exception in getCurrentStatus(): {}", e.what());
        response["success"] = false;
        response["message"] = "Failed to get current status";
        response["error"] = e.what();
        return response;
    }
}

Json::Value SyncService::getSyncHistory(int limit, int offset) {
    Json::Value response;

    try {
        auto syncStatuses = syncStatusRepo_->findAll(limit, offset);
        int totalCount = syncStatusRepo_->count();

        Json::Value dataArray(Json::arrayValue);
        for (const auto& syncStatus : syncStatuses) {
            dataArray.append(syncStatusToJson(syncStatus));
        }

        response["success"] = true;
        response["data"] = dataArray;
        response["pagination"]["total"] = totalCount;
        response["pagination"]["limit"] = limit;
        response["pagination"]["offset"] = offset;
        response["pagination"]["count"] = static_cast<int>(syncStatuses.size());

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[SyncService] Exception in getSyncHistory(): {}", e.what());
        response["success"] = false;
        response["message"] = "Failed to get sync history";
        response["error"] = e.what();
        return response;
    }
}

Json::Value SyncService::performSyncCheck(
    const Json::Value& dbCounts,
    const Json::Value& ldapCounts,
    const Json::Value& countryStats
) {
    Json::Value response;

    try {
        // Calculate discrepancies
        Json::Value discrepancies = calculateDiscrepancies(dbCounts, ldapCounts);

        // Determine if sync is required (any discrepancy > 0)
        bool syncRequired = false;
        if (discrepancies["total"].asInt() > 0) {
            syncRequired = true;
        }

        // Create SyncStatus domain object
        domain::SyncStatus syncStatus;
        syncStatus.setDbCscaCount(dbCounts.get("csca", 0).asInt());
        syncStatus.setDbMlscCount(dbCounts.get("mlsc", 0).asInt());
        syncStatus.setDbDscCount(dbCounts.get("dsc", 0).asInt());
        syncStatus.setDbDscNcCount(dbCounts.get("dsc_nc", 0).asInt());
        syncStatus.setDbCrlCount(dbCounts.get("crl", 0).asInt());
        syncStatus.setDbStoredInLdapCount(dbCounts.get("stored_in_ldap", 0).asInt());

        syncStatus.setLdapCscaCount(ldapCounts.get("csca", 0).asInt());
        syncStatus.setLdapMlscCount(ldapCounts.get("mlsc", 0).asInt());
        syncStatus.setLdapDscCount(ldapCounts.get("dsc", 0).asInt());
        syncStatus.setLdapDscNcCount(ldapCounts.get("dsc_nc", 0).asInt());
        syncStatus.setLdapCrlCount(ldapCounts.get("crl", 0).asInt());

        syncStatus.setCscaDiscrepancy(discrepancies["csca"].asInt());
        syncStatus.setMlscDiscrepancy(discrepancies["mlsc"].asInt());
        syncStatus.setDscDiscrepancy(discrepancies["dsc"].asInt());
        syncStatus.setDscNcDiscrepancy(discrepancies["dsc_nc"].asInt());
        syncStatus.setCrlDiscrepancy(discrepancies["crl"].asInt());
        syncStatus.setTotalDiscrepancy(discrepancies["total"].asInt());

        syncStatus.setSyncRequired(syncRequired);
        syncStatus.setCountryStats(countryStats);

        // Save to database
        bool saved = syncStatusRepo_->create(syncStatus);

        if (!saved) {
            response["success"] = false;
            response["message"] = "Failed to save sync status";
            return response;
        }

        response["success"] = true;
        response["message"] = syncRequired ? "Sync required - discrepancies detected" : "Sync not required - all in sync";
        response["data"] = syncStatusToJson(syncStatus);

        spdlog::info("[SyncService] Sync check completed. Sync required: {}, Total discrepancy: {}",
                     syncRequired, discrepancies["total"].asInt());

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[SyncService] Exception in performSyncCheck(): {}", e.what());
        response["success"] = false;
        response["message"] = "Failed to perform sync check";
        response["error"] = e.what();
        return response;
    }
}

Json::Value SyncService::getSyncStatistics() {
    Json::Value response;

    try {
        auto latestSync = syncStatusRepo_->findLatest();

        if (!latestSync.has_value()) {
            response["success"] = false;
            response["message"] = "No sync data available";
            return response;
        }

        const auto& sync = latestSync.value();

        Json::Value stats;
        stats["totalChecks"] = syncStatusRepo_->count();
        stats["lastCheckTime"] = syncStatusToJson(sync)["checkedAt"];
        stats["syncRequired"] = sync.isSyncRequired();
        stats["totalDiscrepancy"] = sync.getTotalDiscrepancy();

        // Discrepancy breakdown
        Json::Value discrepancyBreakdown;
        discrepancyBreakdown["csca"] = sync.getCscaDiscrepancy();
        discrepancyBreakdown["mlsc"] = sync.getMlscDiscrepancy();
        discrepancyBreakdown["dsc"] = sync.getDscDiscrepancy();
        discrepancyBreakdown["dsc_nc"] = sync.getDscNcDiscrepancy();
        discrepancyBreakdown["crl"] = sync.getCrlDiscrepancy();
        stats["discrepancyBreakdown"] = discrepancyBreakdown;

        // Certificate counts
        Json::Value counts;
        counts["dbTotal"] = sync.getDbCscaCount() + sync.getDbMlscCount() +
                            sync.getDbDscCount() + sync.getDbDscNcCount();
        counts["ldapTotal"] = sync.getLdapCscaCount() + sync.getLdapMlscCount() +
                              sync.getLdapDscCount() + sync.getLdapDscNcCount();
        counts["crlTotal"] = sync.getDbCrlCount();
        stats["counts"] = counts;

        response["success"] = true;
        response["data"] = stats;

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[SyncService] Exception in getSyncStatistics(): {}", e.what());
        response["success"] = false;
        response["message"] = "Failed to get sync statistics";
        response["error"] = e.what();
        return response;
    }
}

Json::Value SyncService::calculateDiscrepancies(
    const Json::Value& dbCounts,
    const Json::Value& ldapCounts
) {
    Json::Value discrepancies;

    int cscaDisc = std::abs(dbCounts.get("csca", 0).asInt() - ldapCounts.get("csca", 0).asInt());
    int mlscDisc = std::abs(dbCounts.get("mlsc", 0).asInt() - ldapCounts.get("mlsc", 0).asInt());
    int dscDisc = std::abs(dbCounts.get("dsc", 0).asInt() - ldapCounts.get("dsc", 0).asInt());
    int dscNcDisc = std::abs(dbCounts.get("dsc_nc", 0).asInt() - ldapCounts.get("dsc_nc", 0).asInt());
    int crlDisc = std::abs(dbCounts.get("crl", 0).asInt() - ldapCounts.get("crl", 0).asInt());

    discrepancies["csca"] = cscaDisc;
    discrepancies["mlsc"] = mlscDisc;
    discrepancies["dsc"] = dscDisc;
    discrepancies["dsc_nc"] = dscNcDisc;
    discrepancies["crl"] = crlDisc;
    discrepancies["total"] = cscaDisc + mlscDisc + dscDisc + dscNcDisc + crlDisc;

    return discrepancies;
}

Json::Value SyncService::syncStatusToJson(const domain::SyncStatus& syncStatus) {
    Json::Value json;

    json["id"] = syncStatus.getId();

    // Format timestamp as ISO 8601
    auto tp = syncStatus.getCheckedAt();
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    json["checkedAt"] = oss.str();

    // Database counts
    Json::Value dbCounts;
    dbCounts["csca"] = syncStatus.getDbCscaCount();
    dbCounts["mlsc"] = syncStatus.getDbMlscCount();
    dbCounts["dsc"] = syncStatus.getDbDscCount();
    dbCounts["dsc_nc"] = syncStatus.getDbDscNcCount();
    dbCounts["crl"] = syncStatus.getDbCrlCount();
    dbCounts["stored_in_ldap"] = syncStatus.getDbStoredInLdapCount();
    json["dbCounts"] = dbCounts;

    // LDAP counts
    Json::Value ldapCounts;
    ldapCounts["csca"] = syncStatus.getLdapCscaCount();
    ldapCounts["mlsc"] = syncStatus.getLdapMlscCount();
    ldapCounts["dsc"] = syncStatus.getLdapDscCount();
    ldapCounts["dsc_nc"] = syncStatus.getLdapDscNcCount();
    ldapCounts["crl"] = syncStatus.getLdapCrlCount();
    json["ldapCounts"] = ldapCounts;

    // Discrepancies
    Json::Value discrepancies;
    discrepancies["csca"] = syncStatus.getCscaDiscrepancy();
    discrepancies["mlsc"] = syncStatus.getMlscDiscrepancy();
    discrepancies["dsc"] = syncStatus.getDscDiscrepancy();
    discrepancies["dsc_nc"] = syncStatus.getDscNcDiscrepancy();
    discrepancies["crl"] = syncStatus.getCrlDiscrepancy();
    discrepancies["total"] = syncStatus.getTotalDiscrepancy();
    json["discrepancies"] = discrepancies;

    json["syncRequired"] = syncStatus.isSyncRequired();
    json["countryStats"] = syncStatus.getCountryStats();

    return json;
}

} // namespace icao::relay::services

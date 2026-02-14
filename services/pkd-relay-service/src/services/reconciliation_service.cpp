/**
 * @file reconciliation_service.cpp
 * @brief Reconciliation service implementation
 */
#include "reconciliation_service.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace icao::relay::services {

ReconciliationService::ReconciliationService(
    std::shared_ptr<repositories::ReconciliationRepository> reconciliationRepo,
    std::shared_ptr<repositories::CertificateRepository> certificateRepo,
    std::shared_ptr<repositories::CrlRepository> crlRepo
)
    : reconciliationRepo_(reconciliationRepo),
      certificateRepo_(certificateRepo),
      crlRepo_(crlRepo)
{
    spdlog::info("[ReconciliationService] Initialized with repository dependencies");
}

Json::Value ReconciliationService::startReconciliation(
    const std::string& triggeredBy,
    bool dryRun
) {
    Json::Value response;

    try {
        // Create new reconciliation summary with required fields
        auto now = std::chrono::system_clock::now();
        domain::ReconciliationSummary summary(
            0,                      // id (will be set by repository)
            triggeredBy,            // triggered_by
            now,                    // triggered_at
            std::nullopt,           // completed_at (not completed yet)
            "IN_PROGRESS",          // status
            dryRun,                 // dry_run
            0, 0,                   // success_count, failed_count
            0, 0,                   // csca_added, csca_deleted
            0, 0,                   // dsc_added, dsc_deleted
            0, 0,                   // dsc_nc_added, dsc_nc_deleted
            0, 0,                   // crl_added, crl_deleted
            0,                      // total_added
            0,                      // duration_ms
            std::nullopt,           // error_message
            std::nullopt            // sync_status_id
        );

        bool created = reconciliationRepo_->createSummary(summary);

        if (!created) {
            response["success"] = false;
            response["message"] = "Failed to create reconciliation record";
            return response;
        }

        response["success"] = true;
        response["message"] = dryRun ? "Dry run started" : "Reconciliation started";
        response["reconciliationId"] = summary.getId();
        response["triggeredBy"] = triggeredBy;
        response["dryRun"] = dryRun;

        spdlog::info("[ReconciliationService] Started reconciliation #{} (triggered by: {}, dry_run: {})",
                     summary.getId(), triggeredBy, dryRun);

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationService] Exception in startReconciliation(): {}", e.what());
        response["success"] = false;
        response["message"] = "Failed to start reconciliation";
        response["error"] = e.what();
        return response;
    }
}

bool ReconciliationService::logReconciliationOperation(
    const std::string& reconciliationId,
    const std::string& certFingerprint,
    const std::string& certType,
    const std::string& countryCode,
    const std::string& action,
    const std::string& result,
    const std::string& errorMessage
) {
    try {
        domain::ReconciliationLog log(
            0,  // id will be set by database
            reconciliationId,
            std::chrono::system_clock::now(),
            certFingerprint,
            certType,
            countryCode,
            action,
            result,
            errorMessage.empty() ? std::nullopt : std::optional<std::string>(errorMessage)
        );

        bool created = reconciliationRepo_->createLog(log);

        if (!created) {
            spdlog::warn("[ReconciliationService] Failed to log operation for reconciliation #{}",
                         reconciliationId);
        }

        return created;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationService] Exception in logReconciliationOperation(): {}",
                      e.what());
        return false;
    }
}

Json::Value ReconciliationService::completeReconciliation(
    const std::string& reconciliationId,
    const std::string& status,
    const domain::ReconciliationSummary& summary
) {
    Json::Value response;

    try {
        // Update summary with final status and completion time
        auto updatedSummary = summary;
        updatedSummary.setId(reconciliationId);
        updatedSummary.setStatus(status);
        updatedSummary.setCompletedAt(std::chrono::system_clock::now());

        bool updated = reconciliationRepo_->updateSummary(updatedSummary);

        if (!updated) {
            response["success"] = false;
            response["message"] = "Failed to update reconciliation record";
            return response;
        }

        response["success"] = true;
        response["message"] = "Reconciliation completed";
        response["data"] = summaryToJson(updatedSummary);

        spdlog::info("[ReconciliationService] Completed reconciliation #{} with status: {}",
                     reconciliationId, status);

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationService] Exception in completeReconciliation(): {}",
                      e.what());
        response["success"] = false;
        response["message"] = "Failed to complete reconciliation";
        response["error"] = e.what();
        return response;
    }
}

Json::Value ReconciliationService::getReconciliationHistory(int limit, int offset) {
    Json::Value response;

    try {
        auto summaries = reconciliationRepo_->findAllSummaries(limit, offset);
        int totalCount = reconciliationRepo_->countSummaries();

        Json::Value dataArray(Json::arrayValue);
        for (const auto& summary : summaries) {
            dataArray.append(summaryToJson(summary));
        }

        response["success"] = true;
        response["data"] = dataArray;
        response["pagination"]["total"] = totalCount;
        response["pagination"]["limit"] = limit;
        response["pagination"]["offset"] = offset;
        response["pagination"]["count"] = static_cast<int>(summaries.size());

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationService] Exception in getReconciliationHistory(): {}",
                      e.what());
        response["success"] = false;
        response["message"] = "Failed to get reconciliation history";
        response["error"] = e.what();
        return response;
    }
}

Json::Value ReconciliationService::getReconciliationDetails(
    const std::string& reconciliationId,
    int logLimit,
    int logOffset
) {
    Json::Value response;

    try {
        // Get summary
        auto summaryOpt = reconciliationRepo_->findSummaryById(reconciliationId);

        if (!summaryOpt.has_value()) {
            response["success"] = false;
            response["message"] = "Reconciliation not found";
            return response;
        }

        // Get logs
        auto logs = reconciliationRepo_->findLogsByReconciliationId(
            reconciliationId, logLimit, logOffset
        );
        int totalLogs = reconciliationRepo_->countLogsByReconciliationId(reconciliationId);

        Json::Value logsArray(Json::arrayValue);
        for (const auto& log : logs) {
            logsArray.append(logToJson(log));
        }

        response["success"] = true;
        response["summary"] = summaryToJson(summaryOpt.value());
        response["logs"] = logsArray;
        response["logPagination"]["total"] = totalLogs;
        response["logPagination"]["limit"] = logLimit;
        response["logPagination"]["offset"] = logOffset;
        response["logPagination"]["count"] = static_cast<int>(logs.size());

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationService] Exception in getReconciliationDetails(): {}",
                      e.what());
        response["success"] = false;
        response["message"] = "Failed to get reconciliation details";
        response["error"] = e.what();
        return response;
    }
}

Json::Value ReconciliationService::getReconciliationStatistics() {
    Json::Value response;

    try {
        int totalReconciliations = reconciliationRepo_->countSummaries();

        // Get recent reconciliations for statistics
        auto recentSummaries = reconciliationRepo_->findAllSummaries(10, 0);

        int totalSuccess = 0;
        int totalFailed = 0;
        int totalAdded = 0;
        int totalDeleted = 0;

        for (const auto& summary : recentSummaries) {
            if (summary.getStatus() == "COMPLETED") {
                totalSuccess++;
            } else if (summary.getStatus() == "FAILED") {
                totalFailed++;
            }
            totalAdded += summary.getTotalAdded();
            totalDeleted += (summary.getCscaDeleted() + summary.getDscDeleted() +
                            summary.getDscNcDeleted() + summary.getCrlDeleted());
        }

        Json::Value stats;
        stats["totalReconciliations"] = totalReconciliations;
        stats["recentSuccess"] = totalSuccess;
        stats["recentFailed"] = totalFailed;
        stats["recentTotalAdded"] = totalAdded;
        stats["recentTotalDeleted"] = totalDeleted;

        if (!recentSummaries.empty()) {
            stats["lastReconciliation"] = summaryToJson(recentSummaries[0]);
        }

        response["success"] = true;
        response["data"] = stats;

        return response;

    } catch (const std::exception& e) {
        spdlog::error("[ReconciliationService] Exception in getReconciliationStatistics(): {}",
                      e.what());
        response["success"] = false;
        response["message"] = "Failed to get reconciliation statistics";
        response["error"] = e.what();
        return response;
    }
}

Json::Value ReconciliationService::summaryToJson(const domain::ReconciliationSummary& summary) {
    Json::Value json;

    json["id"] = summary.getId();
    json["triggeredBy"] = summary.getTriggeredBy();

    // Format timestamps as ISO 8601
    auto formatTimestamp = [](const std::chrono::system_clock::time_point& tp) -> std::string {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    };

    json["triggeredAt"] = formatTimestamp(summary.getTriggeredAt());

    if (summary.getCompletedAt().has_value()) {
        json["completedAt"] = formatTimestamp(summary.getCompletedAt().value());
    } else {
        json["completedAt"] = Json::Value::null;
    }

    json["status"] = summary.getStatus();
    json["dryRun"] = summary.isDryRun();

    json["successCount"] = summary.getSuccessCount();
    json["failedCount"] = summary.getFailedCount();

    // Addition counters
    Json::Value added;
    added["csca"] = summary.getCscaAdded();
    added["dsc"] = summary.getDscAdded();
    added["dsc_nc"] = summary.getDscNcAdded();
    added["crl"] = summary.getCrlAdded();
    added["total"] = summary.getTotalAdded();
    json["added"] = added;

    // Deletion counters
    Json::Value deleted;
    deleted["csca"] = summary.getCscaDeleted();
    deleted["dsc"] = summary.getDscDeleted();
    deleted["dsc_nc"] = summary.getDscNcDeleted();
    deleted["crl"] = summary.getCrlDeleted();
    json["deleted"] = deleted;

    return json;
}

Json::Value ReconciliationService::logToJson(const domain::ReconciliationLog& log) {
    Json::Value json;

    json["id"] = log.getId();
    json["reconciliationId"] = log.getReconciliationId();

    // Format timestamp
    std::time_t t = std::chrono::system_clock::to_time_t(log.getCreatedAt());
    std::tm tm = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    json["createdAt"] = oss.str();

    json["certFingerprint"] = log.getCertFingerprint();
    json["certType"] = log.getCertType();
    json["countryCode"] = log.getCountryCode();
    json["action"] = log.getAction();
    json["result"] = log.getResult();

    if (log.getErrorMessage().has_value()) {
        json["errorMessage"] = log.getErrorMessage().value();
    } else {
        json["errorMessage"] = Json::Value::null;
    }

    return json;
}

} // namespace icao::relay::services

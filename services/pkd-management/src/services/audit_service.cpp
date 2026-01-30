#include "audit_service.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace services {

// ============================================================================
// Constructor & Destructor
// ============================================================================

AuditService::AuditService(repositories::AuditRepository* auditRepo)
    : auditRepo_(auditRepo)
{
    if (!auditRepo_) {
        throw std::invalid_argument("AuditService: auditRepo cannot be nullptr");
    }
    spdlog::info("AuditService initialized with Repository dependencies");
}

// ============================================================================
// Public Methods - Audit Log Recording
// ============================================================================

bool AuditService::recordAuditLog(const AuditLogEntry& entry)
{
    spdlog::info("AuditService::recordAuditLog - operationType: {}, username: {}",
        entry.operationType, entry.username);

    try {
        // Use Repository to insert audit log
        bool result = auditRepo_->insert(
            entry.operationType,
            entry.username,
            entry.ipAddress,
            entry.success,
            entry.errorMessage,
            jsonToJsonbString(entry.metadata),
            entry.durationMs
        );

        if (result) {
            spdlog::debug("Audit log recorded: {} by {}", entry.operationType, entry.username);
        }

        return result;

    } catch (const std::exception& e) {
        spdlog::error("AuditService::recordAuditLog failed: {}", e.what());
        return false;
    }
}

bool AuditService::recordFileUploadAudit(
    const std::string& username,
    const std::string& ipAddress,
    const std::string& fileName,
    const std::string& fileFormat,
    size_t fileSize,
    bool success,
    const std::string& errorMessage,
    int durationMs
)
{
    spdlog::debug("Recording file upload audit: {} ({})", fileName, fileFormat);

    Json::Value metadata;
    metadata["fileName"] = fileName;
    metadata["fileFormat"] = fileFormat;
    metadata["fileSize"] = static_cast<Json::UInt64>(fileSize);

    AuditLogEntry entry;
    entry.operationType = "FILE_UPLOAD";
    entry.username = username;
    entry.ipAddress = ipAddress;
    entry.success = success;
    entry.errorMessage = errorMessage;
    entry.metadata = metadata;
    entry.durationMs = durationMs;

    return recordAuditLog(entry);
}

bool AuditService::recordCertificateSearchAudit(
    const std::string& username,
    const std::string& ipAddress,
    const std::string& searchType,
    const std::string& searchValue,
    int resultCount,
    int durationMs
)
{
    spdlog::debug("Recording certificate search audit: {} = {}", searchType, searchValue);

    Json::Value metadata;
    metadata["searchType"] = searchType;
    metadata["searchValue"] = searchValue;
    metadata["resultCount"] = resultCount;

    AuditLogEntry entry;
    entry.operationType = "CERTIFICATE_SEARCH";
    entry.username = username;
    entry.ipAddress = ipAddress;
    entry.success = true;
    entry.metadata = metadata;
    entry.durationMs = durationMs;

    return recordAuditLog(entry);
}

bool AuditService::recordPaVerificationAudit(
    const std::string& username,
    const std::string& ipAddress,
    const std::string& verificationResult,
    const std::string& dscFingerprint,
    const std::string& errorMessage,
    int durationMs
)
{
    spdlog::debug("Recording PA verification audit: {}", verificationResult);

    Json::Value metadata;
    metadata["verificationResult"] = verificationResult;
    metadata["dscFingerprint"] = dscFingerprint;

    AuditLogEntry entry;
    entry.operationType = "PA_VERIFY";
    entry.username = username;
    entry.ipAddress = ipAddress;
    entry.success = (verificationResult == "VALID");
    entry.errorMessage = errorMessage;
    entry.metadata = metadata;
    entry.durationMs = durationMs;

    return recordAuditLog(entry);
}

// ============================================================================
// Public Methods - Audit Log Retrieval
// ============================================================================

Json::Value AuditService::getOperationLogs(const AuditLogFilter& filter)
{
    spdlog::info("AuditService::getOperationLogs - limit: {}, offset: {}",
        filter.limit, filter.offset);

    Json::Value response;

    try {
        // Use Repository to get audit logs
        Json::Value logs = auditRepo_->findAll(
            filter.limit,
            filter.offset,
            filter.operationType,
            filter.username
        );

        // Build response
        response["success"] = true;
        response["data"] = logs;
        response["count"] = logs.size();
        response["limit"] = filter.limit;
        response["offset"] = filter.offset;

        // Get total count (would need separate query in production)
        response["total"] = logs.size(); // Simplified for now

    } catch (const std::exception& e) {
        spdlog::error("AuditService::getOperationLogs failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
        response["data"] = Json::arrayValue;
        response["count"] = 0;
        response["limit"] = filter.limit;
        response["offset"] = filter.offset;
        response["total"] = 0;
    }

    return response;
}

Json::Value AuditService::getOperationLogById(int logId)
{
    spdlog::info("AuditService::getOperationLogById - logId: {}", logId);

    Json::Value response;
    response["success"] = false;

    try {
        // TODO: Use Repository findById() method
        spdlog::warn("AuditService::getOperationLogById - TODO: Implement with Repository");

        response["error"] = "Not yet implemented";

    } catch (const std::exception& e) {
        spdlog::error("AuditService::getOperationLogById failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

// ============================================================================
// Public Methods - Operation Statistics
// ============================================================================

Json::Value AuditService::getOperationStatistics(
    const std::string& startDate,
    const std::string& endDate
)
{
    spdlog::info("AuditService::getOperationStatistics - startDate: {}, endDate: {}",
        startDate, endDate);

    try {
        // Use Repository to get statistics
        Json::Value stats = auditRepo_->getStatistics(startDate, endDate);

        // Wrap in data object for API response
        Json::Value response;
        response["success"] = true;
        response["data"] = stats;

        return response;

    } catch (const std::exception& e) {
        spdlog::error("AuditService::getOperationStatistics failed: {}", e.what());
        Json::Value response;
        response["success"] = false;
        response["error"] = e.what();
        return response;
    }
}

Json::Value AuditService::getOperationStatisticsByType(
    const std::string& operationType,
    const std::string& startDate,
    const std::string& endDate
)
{
    spdlog::info("AuditService::getOperationStatisticsByType - type: {}", operationType);

    spdlog::warn("TODO: Implement operation statistics by type");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

// ============================================================================
// Public Methods - User Activity Tracking
// ============================================================================

Json::Value AuditService::getUserActivity(
    const std::string& username,
    const std::string& startDate,
    const std::string& endDate
)
{
    spdlog::info("AuditService::getUserActivity - username: {}", username);

    spdlog::warn("TODO: Implement user activity tracking");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value AuditService::getTopActiveUsers(
    int limit,
    const std::string& startDate,
    const std::string& endDate
)
{
    spdlog::info("AuditService::getTopActiveUsers - limit: {}", limit);

    spdlog::warn("TODO: Implement top active users");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

// ============================================================================
// Public Methods - Security Event Monitoring
// ============================================================================

Json::Value AuditService::getFailedOperations(int limit, int offset)
{
    spdlog::info("AuditService::getFailedOperations - limit: {}, offset: {}", limit, offset);

    spdlog::warn("TODO: Implement failed operations retrieval");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value AuditService::getSuspiciousActivities(
    const std::string& startDate,
    const std::string& endDate
)
{
    spdlog::info("AuditService::getSuspiciousActivities");

    spdlog::warn("TODO: Implement suspicious activities detection");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

Json::Value AuditService::getOperationsByIp(
    const std::string& ipAddress,
    int limit,
    int offset
)
{
    spdlog::info("AuditService::getOperationsByIp - IP: {}", ipAddress);

    spdlog::warn("TODO: Implement operations by IP retrieval");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

// ============================================================================
// Public Methods - Data Retention & Cleanup
// ============================================================================

int AuditService::deleteOldAuditLogs(int daysToKeep)
{
    spdlog::info("AuditService::deleteOldAuditLogs - daysToKeep: {}", daysToKeep);

    try {
        // TODO: Implement audit log cleanup with Repository
        spdlog::warn("TODO: AuditRepository needs deleteOld() method");

        return 0;

    } catch (const std::exception& e) {
        spdlog::error("AuditService::deleteOldAuditLogs failed: {}", e.what());
        return 0;
    }
}

Json::Value AuditService::getRetentionStatistics()
{
    spdlog::info("AuditService::getRetentionStatistics");

    spdlog::warn("TODO: Implement retention statistics");

    Json::Value response;
    response["success"] = false;
    response["message"] = "Not yet implemented";
    return response;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

std::string AuditService::jsonToJsonbString(const Json::Value& json)
{
    Json::FastWriter writer;
    return writer.write(json);
}

} // namespace services

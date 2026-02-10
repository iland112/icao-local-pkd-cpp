#include "audit_service.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

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
// Public Methods - Audit Log Retrieval
// ============================================================================

Json::Value AuditService::getOperationLogs(const AuditLogFilter& filter)
{
    spdlog::info("AuditService::getOperationLogs - limit: {}, offset: {}", filter.limit, filter.offset);

    try {
        // Validate and normalize limit
        int normalizedLimit = validateLimit(filter.limit);

        // Get audit logs from repository with all filters (including success)
        Json::Value logs = auditRepo_->findAll(
            normalizedLimit,
            filter.offset,
            filter.operationType,
            filter.username,
            filter.success
        );

        // Get total count with same filters for accurate pagination
        int totalCount = auditRepo_->countAll(
            filter.operationType,
            filter.username,
            filter.success
        );

        // Build response
        Json::Value response;
        response["success"] = true;
        response["data"] = logs;
        response["count"] = static_cast<int>(logs.size());
        response["total"] = totalCount;
        response["limit"] = normalizedLimit;
        response["offset"] = filter.offset;

        return response;

    } catch (const std::exception& e) {
        spdlog::error("AuditService::getOperationLogs failed: {}", e.what());
        Json::Value response;
        response["success"] = false;
        response["error"] = e.what();
        response["data"] = Json::arrayValue;
        response["count"] = 0;
        response["total"] = 0;
        return response;
    }
}

Json::Value AuditService::getOperationStatistics()
{
    spdlog::info("AuditService::getOperationStatistics");

    try {
        // Get statistics from repository (without date filter)
        Json::Value stats = auditRepo_->getStatistics("", "");

        // topUsers already has correct field names from repository
        // (username, operationCount)

        // Build response
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

// ============================================================================
// Private Helper Methods
// ============================================================================

int AuditService::validateLimit(int limit)
{
    if (limit > 100) {
        spdlog::debug("AuditService: Limit {} exceeds maximum, capping to 100", limit);
        return 100;
    }
    if (limit < 1) {
        spdlog::debug("AuditService: Limit {} below minimum, setting to 50", limit);
        return 50;
    }
    return limit;
}

} // namespace services

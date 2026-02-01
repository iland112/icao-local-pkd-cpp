/**
 * @file ldif_structure_service.cpp
 * @brief LDIF Structure Service Implementation
 *
 * @author SmartCore Inc.
 * @date 2026-01-31
 * @version v2.2.2
 */

#include "ldif_structure_service.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace services {

LdifStructureService::LdifStructureService(
    repositories::LdifStructureRepository* ldifStructureRepo
) : ldifStructureRepository_(ldifStructureRepo) {
    if (!ldifStructureRepository_) {
        throw std::invalid_argument("LdifStructureRepository cannot be null");
    }
}

Json::Value LdifStructureService::getLdifStructure(
    const std::string& uploadId,
    int maxEntries
) {
    spdlog::info("LdifStructureService: Getting LDIF structure for upload {} (maxEntries: {})",
                 uploadId, maxEntries);

    try {
        // Validate input
        int validatedMaxEntries = validateMaxEntries(maxEntries);

        // Get LDIF structure from repository
        auto structureData = ldifStructureRepository_->getLdifStructure(
            uploadId,
            validatedMaxEntries
        );

        // Return success response
        return createSuccessResponse(structureData);

    } catch (const std::exception& e) {
        spdlog::error("LdifStructureService: Error getting LDIF structure: {}", e.what());
        return createErrorResponse(e.what());
    }
}

int LdifStructureService::validateMaxEntries(int maxEntries) {
    // Valid range: 1 - 10000
    const int MIN_ENTRIES = 1;
    const int MAX_ENTRIES = 10000;

    if (maxEntries < MIN_ENTRIES) {
        spdlog::warn("maxEntries {} is too small, clamping to {}", maxEntries, MIN_ENTRIES);
        return MIN_ENTRIES;
    }

    if (maxEntries > MAX_ENTRIES) {
        spdlog::warn("maxEntries {} is too large, clamping to {}", maxEntries, MAX_ENTRIES);
        return MAX_ENTRIES;
    }

    return maxEntries;
}

Json::Value LdifStructureService::createSuccessResponse(
    const repositories::LdifStructureData& data
) {
    Json::Value response;
    response["success"] = true;
    response["data"] = data.toJson();

    return response;
}

Json::Value LdifStructureService::createErrorResponse(const std::string& errorMessage) {
    Json::Value response;
    response["success"] = false;
    response["error"] = errorMessage;

    return response;
}

}  // namespace services

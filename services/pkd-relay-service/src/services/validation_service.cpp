#include "validation_service.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <stdexcept>

namespace icao::relay::services {

ValidationService::ValidationService(repositories::ValidationRepository* validationRepo)
    : validationRepo_(validationRepo) {
    if (!validationRepo_) {
        throw std::invalid_argument("ValidationRepository cannot be null");
    }
    spdlog::debug("[ValidationService] Initialized");
}

std::string ValidationService::determineValidationStatus(bool isExpired, const std::string& currentStatus) {
    if (isExpired) {
        // Expired certificates are always INVALID
        return "INVALID";
    }

    // Not expired - maintain current status if VALID, or set to PENDING
    if (currentStatus == "VALID") {
        return "VALID";
    }

    // If it was INVALID/PENDING and now not expired, set to PENDING
    // (needs full validation to become VALID)
    return "PENDING";
}

Json::Value ValidationService::revalidateAll() {
    auto startTime = std::chrono::steady_clock::now();

    Json::Value response;
    response["success"] = false;

    try {
        spdlog::info("[ValidationService] Starting certificate revalidation");

        // Get all validation results with expiration info
        std::vector<domain::ValidationResult> validations = validationRepo_->findAllWithExpirationInfo();

        int totalProcessed = 0;
        int newlyExpired = 0;
        int newlyValid = 0;
        int unchanged = 0;
        int errors = 0;

        for (const auto& validation : validations) {
            try {
                bool currentExpired = !validation.isValidityPeriodValid();
                bool actualExpired = validation.isExpired();

                totalProcessed++;

                // Check if expiration status changed
                if (currentExpired != actualExpired) {
                    std::string newStatus = determineValidationStatus(actualExpired, validation.getValidationStatus());
                    bool newValidityPeriodValid = !actualExpired;

                    // Update database
                    bool updated = validationRepo_->updateValidityStatus(
                        validation.getId(),
                        newValidityPeriodValid,
                        newStatus
                    );

                    if (updated) {
                        if (actualExpired && !currentExpired) {
                            newlyExpired++;
                            spdlog::debug("[ValidationService] Certificate {} newly expired", validation.getId());
                        } else if (!actualExpired && currentExpired) {
                            newlyValid++;
                            spdlog::debug("[ValidationService] Certificate {} newly valid", validation.getId());
                        }
                    } else {
                        errors++;
                        spdlog::warn("[ValidationService] Failed to update validation {}", validation.getId());
                    }
                } else {
                    unchanged++;
                }

            } catch (const std::exception& e) {
                errors++;
                spdlog::error("[ValidationService] Error processing validation {}: {}", validation.getId(), e.what());
            }
        }

        // Update expired counts in uploaded_file table
        int uploadsUpdated = validationRepo_->updateAllUploadExpiredCounts();

        auto endTime = std::chrono::steady_clock::now();
        int durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        // Save revalidation history
        validationRepo_->saveRevalidationHistory(
            totalProcessed,
            newlyExpired,
            newlyValid,
            unchanged,
            errors,
            durationMs
        );

        // Build response
        response["success"] = true;
        response["totalProcessed"] = totalProcessed;
        response["newlyExpired"] = newlyExpired;
        response["newlyValid"] = newlyValid;
        response["unchanged"] = unchanged;
        response["errors"] = errors;
        response["uploadsUpdated"] = uploadsUpdated;
        response["durationMs"] = durationMs;

        spdlog::info("[ValidationService] Revalidation complete: {} processed, {} newly expired, {} newly valid, {} unchanged, {} errors ({}ms)",
                    totalProcessed, newlyExpired, newlyValid, unchanged, errors, durationMs);

    } catch (const std::exception& e) {
        spdlog::error("[ValidationService] Revalidation failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
        response["totalProcessed"] = 0;
        response["errors"] = 1;
    }

    return response;
}

} // namespace icao::relay::services

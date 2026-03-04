/**
 * @file icao_sync_service.cpp
 * @brief ICAO sync service implementation
 */
#include "icao_sync_service.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>

namespace services {

IcaoSyncService::IcaoSyncService(
    std::shared_ptr<repositories::IcaoVersionRepository> repo,
    std::shared_ptr<infrastructure::http::HttpClient> httpClient,
    const Config& config)
    : repo_(repo)
    , httpClient_(httpClient)
    , config_(config) {

    spdlog::info("[IcaoSyncService] Initialized");
    spdlog::info("[IcaoSyncService] Portal URL: {}", config_.icaoPortalUrl);
    spdlog::info("[IcaoSyncService] Notification email: {}", config_.notificationEmail);
    spdlog::info("[IcaoSyncService] Auto-notify: {}", config_.autoNotify ? "enabled" : "disabled");
}

IcaoSyncService::~IcaoSyncService() {}

IcaoSyncService::CheckResult IcaoSyncService::checkForUpdates() {
    spdlog::info("[IcaoSyncService] Starting ICAO version check");

    CheckResult result;
    result.success = false;
    result.newVersionCount = 0;

    // Step 1: Fetch remote versions from ICAO portal
    auto remoteVersions = fetchRemoteVersions();
    if (remoteVersions.empty()) {
        result.message = "Failed to fetch ICAO portal HTML or no versions found";
        spdlog::error("[IcaoSyncService] {}", result.message);
        return result;
    }

    spdlog::info("[IcaoSyncService] Found {} versions on ICAO portal",
                remoteVersions.size());

    // Step 2: Get local versions from database
    auto localVersions = repo_->getAllVersions();
    spdlog::info("[IcaoSyncService] Found {} versions in local database",
                localVersions.size());

    // Step 3: Compare and find new versions
    auto newVersions = findNewVersions(remoteVersions, localVersions);

    if (newVersions.empty()) {
        result.success = true;
        result.message = "No new versions detected. System is up to date.";
        spdlog::info("[IcaoSyncService] {}", result.message);
        return result;
    }

    spdlog::info("[IcaoSyncService] Detected {} new versions", newVersions.size());

    // Step 4: Save new versions to database
    if (!saveNewVersions(newVersions)) {
        result.message = "Failed to save new versions to database";
        spdlog::error("[IcaoSyncService] {}", result.message);
        return result;
    }

    // Step 5: Send notification (if enabled)
    if (config_.autoNotify) {
        if (sendNotification(newVersions)) {
            spdlog::info("[IcaoSyncService] Notification sent successfully");
        } else {
            spdlog::warn("[IcaoSyncService] Failed to send notification");
        }
    }

    // Build success result
    result.success = true;
    result.newVersionCount = static_cast<int>(newVersions.size());
    result.newVersions = newVersions;
    result.message = "New versions detected and saved";

    return result;
}

std::vector<domain::models::IcaoVersion> IcaoSyncService::getLatestVersions() {
    return repo_->getLatest();
}

std::vector<domain::models::IcaoVersion> IcaoSyncService::getVersionHistory(int limit) {
    return repo_->getHistory(limit);
}

std::vector<std::tuple<std::string, int, int, std::string>>
IcaoSyncService::getVersionComparison() {
    return repo_->getVersionComparison();
}

// --- Private methods ---

std::vector<domain::models::IcaoVersion> IcaoSyncService::fetchRemoteVersions() {
    // Fetch HTML from ICAO portal
    auto htmlOpt = httpClient_->fetchHtml(config_.icaoPortalUrl,
                                         config_.httpTimeoutSeconds);

    if (!htmlOpt.has_value()) {
        spdlog::error("[IcaoSyncService] Failed to fetch ICAO portal HTML");
        return {};
    }

    std::string html = htmlOpt.value();
    spdlog::debug("[IcaoSyncService] Fetched HTML ({} bytes)", html.size());

    // Parse HTML to extract version numbers
    auto versions = utils::HtmlParser::parseVersions(html);

    spdlog::info("[IcaoSyncService] Parsed {} versions from HTML", versions.size());

    return versions;
}

std::vector<domain::models::IcaoVersion> IcaoSyncService::findNewVersions(
    const std::vector<domain::models::IcaoVersion>& remoteVersions,
    const std::vector<domain::models::IcaoVersion>& localVersions) {

    std::vector<domain::models::IcaoVersion> newVersions;

    for (const auto& remote : remoteVersions) {
        bool isNew = true;

        // Check if this version already exists in local database
        for (const auto& local : localVersions) {
            if (remote.collectionType == local.collectionType &&
                remote.fileVersion == local.fileVersion) {
                isNew = false;
                break;
            }
        }

        if (isNew) {
            newVersions.push_back(remote);
            spdlog::info("[IcaoSyncService] New version: {} (v{})",
                        remote.fileName, remote.fileVersion);
        }
    }

    return newVersions;
}

bool IcaoSyncService::saveNewVersions(
    const std::vector<domain::models::IcaoVersion>& newVersions) {

    bool allSuccess = true;

    for (const auto& version : newVersions) {
        bool inserted = repo_->insert(version);

        if (inserted) {
            spdlog::info("[IcaoSyncService] Saved new version: {}",
                        version.fileName);
        } else {
            spdlog::error("[IcaoSyncService] Failed to save version: {}",
                         version.fileName);
            allSuccess = false;
        }
    }

    return allSuccess;
}

bool IcaoSyncService::sendNotification(
    const std::vector<domain::models::IcaoVersion>& newVersions) {

    spdlog::info("[IcaoSyncService] ICAO PKD update notification:");
    spdlog::info("[IcaoSyncService] To: {}", config_.notificationEmail);
    spdlog::info("[IcaoSyncService] Subject: [ICAO PKD] New Certificate Updates Available");
    for (const auto& version : newVersions) {
        spdlog::info("[IcaoSyncService] New version: {} (v{}, type: {})",
                     version.fileName, version.fileVersion, version.collectionType);
    }

    for (const auto& version : newVersions) {
        repo_->markNotificationSent(version.fileName);
    }

    return true;
}

} // namespace services

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
    std::shared_ptr<infrastructure::notification::EmailSender> emailSender,
    const Config& config)
    : repo_(repo)
    , httpClient_(httpClient)
    , emailSender_(emailSender)
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

    auto message = buildNotificationMessage(newVersions);

    bool sent = emailSender_->send(message);

    if (sent) {
        // Mark notifications as sent in database
        for (const auto& version : newVersions) {
            repo_->markNotificationSent(version.fileName);
        }
    }

    return sent;
}

infrastructure::notification::EmailSender::EmailMessage
IcaoSyncService::buildNotificationMessage(
    const std::vector<domain::models::IcaoVersion>& newVersions) {

    infrastructure::notification::EmailSender::EmailMessage message;

    message.toAddresses = { config_.notificationEmail };
    message.subject = "[ICAO PKD] New Certificate Updates Available";

    // Build email body
    std::ostringstream body;

    body << "Dear Administrator,\n\n";
    body << "The ICAO PKD monitoring system has detected new certificate updates:\n\n";
    body << "NEW VERSIONS DETECTED:\n";

    for (const auto& version : newVersions) {
        body << "- " << version.fileName
             << " (Version " << version.fileVersion << ")\n";
        body << "  Type: " << version.collectionType << "\n";
        body << "  Detected: " << version.detectedAt << "\n\n";
    }

    body << "ACTION REQUIRED:\n";
    body << "1. Download the new files from: " << config_.icaoPortalUrl << "\n";
    body << "2. Upload to Local PKD system: http://localhost:3000/upload\n";
    body << "3. Verify import completion in Upload History\n\n";

    body << "DASHBOARD:\n";
    body << "View current status: http://localhost:3000/\n\n";

    body << "---\n";
    body << "This is an automated notification from ICAO Local PKD v1.7.0\n";
    body << "For support, contact your system administrator\n";

    message.body = body.str();

    return message;
}

} // namespace services

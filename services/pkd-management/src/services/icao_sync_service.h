#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../domain/models/icao_version.h"
#include "../repositories/icao_version_repository.h"
#include "../infrastructure/http/http_client.h"
#include "../infrastructure/notification/email_sender.h"
#include "../utils/html_parser.h"

namespace services {

/**
 * @brief ICAO Auto Sync Service (Tier 1: Manual Download with Notification)
 *
 * Orchestrates the workflow for checking ICAO PKD portal for new versions,
 * detecting updates, and notifying administrators.
 *
 * Workflow:
 * 1. Fetch ICAO portal HTML
 * 2. Parse version numbers
 * 3. Compare with local database
 * 4. Save new versions
 * 5. Send email notification
 */
class IcaoSyncService {
public:
    struct CheckResult {
        bool success;
        std::string message;
        int newVersionCount;
        std::vector<domain::models::IcaoVersion> newVersions;
    };

    struct Config {
        std::string icaoPortalUrl;
        std::string notificationEmail;
        bool autoNotify;
        int httpTimeoutSeconds;
    };

    IcaoSyncService(std::shared_ptr<repositories::IcaoVersionRepository> repo,
                    std::shared_ptr<infrastructure::http::HttpClient> httpClient,
                    std::shared_ptr<infrastructure::notification::EmailSender> emailSender,
                    const Config& config);

    ~IcaoSyncService();

    /**
     * @brief Check ICAO portal for new versions
     *
     * This is the main entry point for version checking.
     * Can be triggered manually via API or automatically by cron job.
     */
    CheckResult checkForUpdates();

    /**
     * @brief Get latest detected versions (one per collection type)
     */
    std::vector<domain::models::IcaoVersion> getLatestVersions();

    /**
     * @brief Get version history (most recent first)
     */
    std::vector<domain::models::IcaoVersion> getVersionHistory(int limit);

private:
    std::shared_ptr<repositories::IcaoVersionRepository> repo_;
    std::shared_ptr<infrastructure::http::HttpClient> httpClient_;
    std::shared_ptr<infrastructure::notification::EmailSender> emailSender_;
    Config config_;

    /**
     * @brief Fetch and parse ICAO portal HTML
     */
    std::vector<domain::models::IcaoVersion> fetchRemoteVersions();

    /**
     * @brief Compare remote versions with local database
     */
    std::vector<domain::models::IcaoVersion> findNewVersions(
        const std::vector<domain::models::IcaoVersion>& remoteVersions,
        const std::vector<domain::models::IcaoVersion>& localVersions);

    /**
     * @brief Save new versions to database
     */
    bool saveNewVersions(const std::vector<domain::models::IcaoVersion>& newVersions);

    /**
     * @brief Send notification email to administrator
     */
    bool sendNotification(const std::vector<domain::models::IcaoVersion>& newVersions);

    /**
     * @brief Build notification email message
     */
    infrastructure::notification::EmailSender::EmailMessage buildNotificationMessage(
        const std::vector<domain::models::IcaoVersion>& newVersions);
};

} // namespace services

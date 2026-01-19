#pragma once

#include <string>
#include <vector>

namespace infrastructure {
namespace notification {

/**
 * @brief Email sender using SMTP protocol
 *
 * Simple email notification system for ICAO version updates.
 * Uses system's mail command or SMTP library (libcurl).
 */
class EmailSender {
public:
    struct EmailConfig {
        std::string smtpHost;
        int smtpPort;
        std::string username;
        std::string password;
        std::string fromAddress;
        bool useTls;
    };

    struct EmailMessage {
        std::vector<std::string> toAddresses;
        std::string subject;
        std::string body;
    };

    explicit EmailSender(const EmailConfig& config);
    ~EmailSender();

    /**
     * @brief Send an email notification
     * @return true if sent successfully, false otherwise
     */
    bool send(const EmailMessage& message);

private:
    EmailConfig config_;

    /**
     * @brief Format email body with proper headers
     */
    std::string formatEmail(const EmailMessage& message);

    /**
     * @brief Send via system mail command (fallback)
     */
    bool sendViaSystemMail(const EmailMessage& message);
};

} // namespace notification
} // namespace infrastructure

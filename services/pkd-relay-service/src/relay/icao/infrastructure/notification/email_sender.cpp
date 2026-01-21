#include "email_sender.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <cstdlib>
#include <array>
#include <memory>

namespace infrastructure {
namespace notification {

EmailSender::EmailSender(const EmailConfig& config)
    : config_(config) {
    spdlog::info("[EmailSender] Initialized with SMTP: {}:{}",
                config_.smtpHost, config_.smtpPort);
}

EmailSender::~EmailSender() {}

bool EmailSender::send(const EmailMessage& message) {
    spdlog::info("[EmailSender] Sending email to {} recipient(s): {}",
                message.toAddresses.size(), message.subject);

    // For now, use system mail command (simple implementation)
    // TODO: Implement proper SMTP client using libcurl
    return sendViaSystemMail(message);
}

std::string EmailSender::formatEmail(const EmailMessage& message) {
    std::ostringstream oss;

    oss << "From: " << config_.fromAddress << "\n";
    oss << "To: ";
    for (size_t i = 0; i < message.toAddresses.size(); i++) {
        oss << message.toAddresses[i];
        if (i < message.toAddresses.size() - 1) {
            oss << ", ";
        }
    }
    oss << "\n";
    oss << "Subject: " << message.subject << "\n";
    oss << "\n";
    oss << message.body << "\n";

    return oss.str();
}

bool EmailSender::sendViaSystemMail(const EmailMessage& message) {
    // Check if mail command is available
    int checkResult = system("command -v mail >/dev/null 2>&1");
    if (checkResult != 0) {
        spdlog::warn("[EmailSender] System 'mail' command not available");
        spdlog::info("[EmailSender] Email content (would be sent):\n{}",
                    formatEmail(message));
        return false;
    }

    // Build mail command
    std::ostringstream cmd;
    cmd << "echo \"" << message.body << "\" | mail -s \"" << message.subject << "\"";

    for (const auto& to : message.toAddresses) {
        cmd << " " << to;
    }

    spdlog::debug("[EmailSender] Executing: {}", cmd.str());

    int result = system(cmd.str().c_str());

    if (result == 0) {
        spdlog::info("[EmailSender] Email sent successfully");
        return true;
    } else {
        spdlog::error("[EmailSender] Failed to send email (exit code: {})", result);
        return false;
    }
}

} // namespace notification
} // namespace infrastructure

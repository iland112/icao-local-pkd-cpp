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

    // Log email content instead of system() execution (security fix)
    // TODO: Implement proper SMTP via libcurl when email sending is required
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
    // Log email content (no system() calls for security)
    std::ostringstream recipients;
    for (size_t i = 0; i < message.toAddresses.size(); i++) {
        recipients << message.toAddresses[i];
        if (i < message.toAddresses.size() - 1) recipients << ", ";
    }

    spdlog::info("[EmailSender] Email notification (log only):");
    spdlog::info("[EmailSender] To: {}", recipients.str());
    spdlog::info("[EmailSender] Subject: {}", message.subject);
    spdlog::info("[EmailSender] Body: {}", message.body);
    // TODO: Implement proper SMTP via libcurl when email sending is required
    return true;
}

} // namespace notification
} // namespace infrastructure

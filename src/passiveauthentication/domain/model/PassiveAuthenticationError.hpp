#pragma once

#include <string>
#include <chrono>

namespace pa::domain::model {

/**
 * Error details for Passive Authentication verification.
 */
class PassiveAuthenticationError {
public:
    enum class Severity {
        INFO,       // Informational (does not affect verification)
        WARNING,    // Warning (verification continues)
        CRITICAL    // Critical error (verification fails)
    };

private:
    std::string code_;
    std::string message_;
    Severity severity_;
    std::chrono::system_clock::time_point timestamp_;

    PassiveAuthenticationError(
        std::string code,
        std::string message,
        Severity severity
    ) : code_(std::move(code)),
        message_(std::move(message)),
        severity_(severity),
        timestamp_(std::chrono::system_clock::now()) {}

public:
    PassiveAuthenticationError() = default;

    /**
     * Create a critical error.
     */
    static PassiveAuthenticationError critical(
        const std::string& code,
        const std::string& message
    ) {
        return PassiveAuthenticationError(code, message, Severity::CRITICAL);
    }

    /**
     * Create a warning.
     */
    static PassiveAuthenticationError warning(
        const std::string& code,
        const std::string& message
    ) {
        return PassiveAuthenticationError(code, message, Severity::WARNING);
    }

    /**
     * Create an info message.
     */
    static PassiveAuthenticationError info(
        const std::string& code,
        const std::string& message
    ) {
        return PassiveAuthenticationError(code, message, Severity::INFO);
    }

    const std::string& getCode() const { return code_; }
    const std::string& getMessage() const { return message_; }
    Severity getSeverity() const { return severity_; }
    const std::chrono::system_clock::time_point& getTimestamp() const { return timestamp_; }

    std::string getSeverityString() const {
        switch (severity_) {
            case Severity::INFO: return "INFO";
            case Severity::WARNING: return "WARNING";
            case Severity::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }

    bool isCritical() const { return severity_ == Severity::CRITICAL; }
    bool isWarning() const { return severity_ == Severity::WARNING; }
    bool isInfo() const { return severity_ == Severity::INFO; }
};

} // namespace pa::domain::model

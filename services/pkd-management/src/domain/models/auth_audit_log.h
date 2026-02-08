#pragma once

#include <string>
#include <optional>
#include <chrono>

/**
 * @file auth_audit_log.h
 * @brief AuthAuditLog Domain Model
 *
 * Represents an authentication audit log entry.
 *
 * @note Part of Phase 5.4: AuthHandler Repository Pattern Migration
 * @date 2026-02-08
 */

namespace domain {

/**
 * @brief AuthAuditLog entity
 *
 * Records authentication events (login, logout, token refresh) for security auditing.
 */
class AuthAuditLog {
public:
    AuthAuditLog() = default;

    /**
     * @brief Constructor with all fields
     */
    AuthAuditLog(const std::string& id,
                 const std::optional<std::string>& userId,
                 const std::string& username,
                 const std::string& eventType,
                 const std::optional<std::string>& ipAddress,
                 const std::optional<std::string>& userAgent,
                 bool success,
                 const std::optional<std::string>& errorMessage,
                 const std::chrono::system_clock::time_point& createdAt)
        : id_(id),
          userId_(userId),
          username_(username),
          eventType_(eventType),
          ipAddress_(ipAddress),
          userAgent_(userAgent),
          success_(success),
          errorMessage_(errorMessage),
          createdAt_(createdAt)
    {}

    // Getters
    std::string getId() const { return id_; }
    std::optional<std::string> getUserId() const { return userId_; }
    std::string getUsername() const { return username_; }
    std::string getEventType() const { return eventType_; }
    std::optional<std::string> getIpAddress() const { return ipAddress_; }
    std::optional<std::string> getUserAgent() const { return userAgent_; }
    bool isSuccess() const { return success_; }
    std::optional<std::string> getErrorMessage() const { return errorMessage_; }
    std::chrono::system_clock::time_point getCreatedAt() const { return createdAt_; }

    // Setters
    void setId(const std::string& id) { id_ = id; }
    void setUserId(const std::optional<std::string>& userId) { userId_ = userId; }
    void setUsername(const std::string& username) { username_ = username; }
    void setEventType(const std::string& eventType) { eventType_ = eventType; }
    void setIpAddress(const std::optional<std::string>& ipAddress) { ipAddress_ = ipAddress; }
    void setUserAgent(const std::optional<std::string>& userAgent) { userAgent_ = userAgent; }
    void setSuccess(bool success) { success_ = success; }
    void setErrorMessage(const std::optional<std::string>& errorMessage) { errorMessage_ = errorMessage; }
    void setCreatedAt(const std::chrono::system_clock::time_point& createdAt) { createdAt_ = createdAt; }

private:
    std::string id_;
    std::optional<std::string> userId_;
    std::string username_;
    std::string eventType_;
    std::optional<std::string> ipAddress_;
    std::optional<std::string> userAgent_;
    bool success_ = true;
    std::optional<std::string> errorMessage_;
    std::chrono::system_clock::time_point createdAt_;
};

} // namespace domain

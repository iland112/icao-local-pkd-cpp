#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>

/**
 * @file user.h
 * @brief User Domain Model
 *
 * Represents a user in the authentication system.
 *
 * @note Part of Phase 5.4: AuthHandler Repository Pattern Migration
 * @date 2026-02-08
 */

namespace domain {

/**
 * @brief User entity
 *
 * Represents a user account with authentication credentials and permissions.
 */
class User {
public:
    User() = default;

    /**
     * @brief Constructor with all fields
     */
    User(const std::string& id,
         const std::string& username,
         const std::string& passwordHash,
         const std::optional<std::string>& email,
         const std::optional<std::string>& fullName,
         const std::vector<std::string>& permissions,
         bool isActive,
         bool isAdmin,
         const std::chrono::system_clock::time_point& createdAt,
         const std::optional<std::chrono::system_clock::time_point>& lastLoginAt,
         const std::chrono::system_clock::time_point& updatedAt)
        : id_(id),
          username_(username),
          passwordHash_(passwordHash),
          email_(email),
          fullName_(fullName),
          permissions_(permissions),
          isActive_(isActive),
          isAdmin_(isAdmin),
          createdAt_(createdAt),
          lastLoginAt_(lastLoginAt),
          updatedAt_(updatedAt)
    {}

    // Getters
    std::string getId() const { return id_; }
    std::string getUsername() const { return username_; }
    std::string getPasswordHash() const { return passwordHash_; }
    std::optional<std::string> getEmail() const { return email_; }
    std::optional<std::string> getFullName() const { return fullName_; }
    std::vector<std::string> getPermissions() const { return permissions_; }
    bool isActive() const { return isActive_; }
    bool isAdmin() const { return isAdmin_; }
    std::chrono::system_clock::time_point getCreatedAt() const { return createdAt_; }
    std::optional<std::chrono::system_clock::time_point> getLastLoginAt() const { return lastLoginAt_; }
    std::chrono::system_clock::time_point getUpdatedAt() const { return updatedAt_; }

    // Setters
    void setId(const std::string& id) { id_ = id; }
    void setUsername(const std::string& username) { username_ = username; }
    void setPasswordHash(const std::string& passwordHash) { passwordHash_ = passwordHash; }
    void setEmail(const std::optional<std::string>& email) { email_ = email; }
    void setFullName(const std::optional<std::string>& fullName) { fullName_ = fullName; }
    void setPermissions(const std::vector<std::string>& permissions) { permissions_ = permissions; }
    void setIsActive(bool isActive) { isActive_ = isActive; }
    void setIsAdmin(bool isAdmin) { isAdmin_ = isAdmin; }
    void setCreatedAt(const std::chrono::system_clock::time_point& createdAt) { createdAt_ = createdAt; }
    void setLastLoginAt(const std::optional<std::chrono::system_clock::time_point>& lastLoginAt) { lastLoginAt_ = lastLoginAt; }
    void setUpdatedAt(const std::chrono::system_clock::time_point& updatedAt) { updatedAt_ = updatedAt; }

private:
    std::string id_;
    std::string username_;
    std::string passwordHash_;
    std::optional<std::string> email_;
    std::optional<std::string> fullName_;
    std::vector<std::string> permissions_;
    bool isActive_ = true;
    bool isAdmin_ = false;
    std::chrono::system_clock::time_point createdAt_;
    std::optional<std::chrono::system_clock::time_point> lastLoginAt_;
    std::chrono::system_clock::time_point updatedAt_;
};

} // namespace domain

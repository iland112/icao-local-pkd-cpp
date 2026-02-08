#pragma once

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"
#include "../domain/models/user.h"

/**
 * @file user_repository.h
 * @brief User Repository - Database Access Layer for users table
 *
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @note Part of Phase 5.4: AuthHandler Repository Pattern Migration
 * @date 2026-02-08
 */

namespace repositories {

class UserRepository {
public:
    /**
     * @brief Constructor
     * @param queryExecutor Query executor (PostgreSQL or Oracle, non-owning pointer)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit UserRepository(common::IQueryExecutor* queryExecutor);
    ~UserRepository() = default;

    /**
     * @brief Find user by username
     * @param username Username to search for
     * @return User domain object if found, std::nullopt otherwise
     */
    std::optional<domain::User> findByUsername(const std::string& username);

    /**
     * @brief Find user by ID
     * @param id User ID (UUID)
     * @return User domain object if found, std::nullopt otherwise
     */
    std::optional<domain::User> findById(const std::string& id);

    /**
     * @brief Find all users with optional filters
     * @param limit Maximum number of records
     * @param offset Offset for pagination
     * @param usernameFilter Filter by username (partial match, empty = all)
     * @param isActiveFilter Filter by active status ("true", "false", or empty = all)
     * @return JSON array of users
     */
    Json::Value findAll(
        int limit,
        int offset,
        const std::string& usernameFilter = "",
        const std::string& isActiveFilter = ""
    );

    /**
     * @brief Count users with filter
     * @param usernameFilter Filter by username (partial match, empty = all)
     * @param isActiveFilter Filter by active status ("true", "false", or empty = all)
     * @return Total count of matching users
     */
    int count(
        const std::string& usernameFilter = "",
        const std::string& isActiveFilter = ""
    );

    /**
     * @brief Create new user
     * @param user User domain object (id will be auto-generated if empty)
     * @return Generated user ID (UUID)
     * @throws std::runtime_error on database error
     */
    std::string create(const domain::User& user);

    /**
     * @brief Update existing user
     * @param id User ID to update
     * @param email New email (nullopt = no change)
     * @param fullName New full name (nullopt = no change)
     * @param isAdmin New admin status (nullopt = no change)
     * @param permissions New permissions (empty = no change)
     * @param isActive New active status (nullopt = no change)
     * @return true if update successful, false otherwise
     * @throws std::runtime_error on database error
     */
    bool update(
        const std::string& id,
        const std::optional<std::string>& email,
        const std::optional<std::string>& fullName,
        const std::optional<bool>& isAdmin,
        const std::vector<std::string>& permissions,
        const std::optional<bool>& isActive
    );

    /**
     * @brief Delete user by ID
     * @param id User ID to delete
     * @return Username of deleted user, std::nullopt if user not found
     * @throws std::runtime_error on database error
     */
    std::optional<std::string> remove(const std::string& id);

    /**
     * @brief Update last login timestamp
     * @param id User ID
     * @return true if update successful, false otherwise
     */
    bool updateLastLogin(const std::string& id);

    /**
     * @brief Update user password
     * @param id User ID
     * @param passwordHash New password hash
     * @return true if update successful, false otherwise
     */
    bool updatePassword(const std::string& id, const std::string& passwordHash);

private:
    common::IQueryExecutor* queryExecutor_;  // Query executor (non-owning)

    /**
     * @brief Convert JSON result to User domain object
     * @param json JSON object with user fields
     * @return User domain object
     */
    domain::User jsonToUser(const Json::Value& json);

    /**
     * @brief Parse ISO 8601 timestamp string to time_point
     * @param timestamp ISO 8601 timestamp string
     * @return time_point
     */
    std::chrono::system_clock::time_point parseTimestamp(const std::string& timestamp);

    /**
     * @brief Parse JSON array to vector of strings
     * @param json JSON array or string
     * @return Vector of strings
     */
    std::vector<std::string> parsePermissions(const Json::Value& json);
};

} // namespace repositories

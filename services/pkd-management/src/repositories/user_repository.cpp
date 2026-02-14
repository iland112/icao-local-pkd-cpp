/** @file user_repository.cpp
 *  @brief UserRepository implementation
 */

#include "user_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace repositories {

// --- Constructor ---

UserRepository::UserRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("UserRepository: queryExecutor cannot be nullptr");
    }

    std::string dbType = queryExecutor_->getDatabaseType();
    spdlog::debug("[UserRepository] Initialized (DB type: {})", dbType);
}

// --- Public Methods ---

std::optional<domain::User> UserRepository::findByUsername(const std::string& username)
{
    try {
        spdlog::debug("[UserRepository] Finding user by username: {}", username);

        const char* query =
            "SELECT id, username, password_hash, email, full_name, permissions, "
            "is_active, is_admin, created_at, last_login_at, updated_at "
            "FROM users WHERE username = $1";

        std::vector<std::string> params = {username};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::debug("[UserRepository] User not found: {}", username);
            return std::nullopt;
        }

        return jsonToUser(result[0]);

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] findByUsername failed: {}", e.what());
        throw std::runtime_error("Failed to find user by username: " + std::string(e.what()));
    }
}

std::optional<domain::User> UserRepository::findById(const std::string& id)
{
    try {
        spdlog::debug("[UserRepository] Finding user by ID: {}", id);

        const char* query =
            "SELECT id, username, password_hash, email, full_name, permissions, "
            "is_active, is_admin, created_at, last_login_at, updated_at "
            "FROM users WHERE id = $1";

        std::vector<std::string> params = {id};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::debug("[UserRepository] User not found: {}", id);
            return std::nullopt;
        }

        return jsonToUser(result[0]);

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] findById failed: {}", e.what());
        throw std::runtime_error("Failed to find user by ID: " + std::string(e.what()));
    }
}

Json::Value UserRepository::findAll(
    int limit,
    int offset,
    const std::string& usernameFilter,
    const std::string& isActiveFilter)
{
    try {
        spdlog::debug("[UserRepository] Finding all users (limit: {}, offset: {}, username: {}, active: {})",
                     limit, offset, usernameFilter, isActiveFilter);

        // Build WHERE clause
        std::string whereClause = "WHERE 1=1";
        std::vector<std::string> params;
        int paramIndex = 1;

        if (!usernameFilter.empty()) {
            whereClause += " AND username ILIKE $" + std::to_string(paramIndex++);
            params.push_back("%" + usernameFilter + "%");
        }

        if (!isActiveFilter.empty()) {
            whereClause += " AND is_active = $" + std::to_string(paramIndex++);
            params.push_back(isActiveFilter == "true" ? "true" : "false");
        }

        // Main query (LIMIT and OFFSET as literals, not parameters)
        std::string query =
            "SELECT id, username, email, full_name, is_admin, is_active, "
            "permissions, created_at, last_login_at, updated_at "
            "FROM users " + whereClause +
            " ORDER BY created_at DESC "
            "LIMIT " + std::to_string(limit) +
            " OFFSET " + std::to_string(offset);

        Json::Value result = queryExecutor_->executeQuery(query, params);
        spdlog::debug("[UserRepository] Found {} users", result.size());

        return result;

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] findAll failed: {}", e.what());
        throw std::runtime_error("Failed to find all users: " + std::string(e.what()));
    }
}

int UserRepository::count(
    const std::string& usernameFilter,
    const std::string& isActiveFilter)
{
    try {
        spdlog::debug("[UserRepository] Counting users (username: {}, active: {})",
                     usernameFilter, isActiveFilter);

        // Build WHERE clause
        std::string whereClause = "WHERE 1=1";
        std::vector<std::string> params;
        int paramIndex = 1;

        if (!usernameFilter.empty()) {
            whereClause += " AND username ILIKE $" + std::to_string(paramIndex++);
            params.push_back("%" + usernameFilter + "%");
        }

        if (!isActiveFilter.empty()) {
            whereClause += " AND is_active = $" + std::to_string(paramIndex++);
            params.push_back(isActiveFilter == "true" ? "true" : "false");
        }

        std::string query = "SELECT COUNT(*) FROM users " + whereClause;

        Json::Value result = queryExecutor_->executeScalar(query, params);

        int count = result.asInt();
        spdlog::debug("[UserRepository] Total users: {}", count);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] count failed: {}", e.what());
        throw std::runtime_error("Failed to count users: " + std::string(e.what()));
    }
}

std::string UserRepository::create(const domain::User& user)
{
    try {
        spdlog::debug("[UserRepository] Creating user: {}", user.getUsername());

        // Convert permissions vector to JSON array string
        Json::Value permissionsJson = Json::arrayValue;
        for (const auto& perm : user.getPermissions()) {
            permissionsJson.append(perm);
        }
        Json::StreamWriterBuilder writer;
        std::string permissionsStr = Json::writeString(writer, permissionsJson);

        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query;
        std::vector<std::string> params;
        std::string userId;

        if (dbType == "oracle") {
            // Oracle: Pre-generate UUID, no RETURNING clause
            Json::Value uuidResult = queryExecutor_->executeQuery(
                "SELECT uuid_generate_v4() AS id FROM DUAL", {});
            if (uuidResult.empty()) {
                throw std::runtime_error("Failed to generate UUID from Oracle");
            }
            userId = uuidResult[0]["id"].asString();

            query =
                "INSERT INTO users (id, username, password_hash, email, full_name, is_admin, permissions, is_active) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)";

            params = {
                userId,
                user.getUsername(),
                user.getPasswordHash(),
                user.getEmail().value_or(""),
                user.getFullName().value_or(""),
                user.isAdmin() ? "1" : "0",
                permissionsStr,
                user.isActive() ? "1" : "0"
            };

            queryExecutor_->executeCommand(query, params);

        } else {
            // PostgreSQL: Use RETURNING id
            query =
                "INSERT INTO users (username, password_hash, email, full_name, is_admin, permissions, is_active) "
                "VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7) "
                "RETURNING id";

            params = {
                user.getUsername(),
                user.getPasswordHash(),
                user.getEmail().value_or(""),
                user.getFullName().value_or(""),
                user.isAdmin() ? "true" : "false",
                permissionsStr,
                user.isActive() ? "true" : "false"
            };

            Json::Value result = queryExecutor_->executeQuery(query, params);

            if (result.empty() || !result[0].isMember("id")) {
                throw std::runtime_error("Failed to get generated user ID");
            }

            userId = result[0]["id"].asString();
        }

        spdlog::info("[UserRepository] User created successfully: {} (ID: {})",
                    user.getUsername(), userId);

        return userId;

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] create failed: {}", e.what());
        throw std::runtime_error("Failed to create user: " + std::string(e.what()));
    }
}

bool UserRepository::update(
    const std::string& id,
    const std::optional<std::string>& email,
    const std::optional<std::string>& fullName,
    const std::optional<bool>& isAdmin,
    const std::vector<std::string>& permissions,
    const std::optional<bool>& isActive)
{
    try {
        spdlog::debug("[UserRepository] Updating user: {}", id);

        // Build dynamic UPDATE query
        std::vector<std::string> setClauses;
        std::vector<std::string> params;
        int paramIndex = 1;

        if (email.has_value()) {
            setClauses.push_back("email = $" + std::to_string(paramIndex++));
            params.push_back(email.value());
        }

        if (fullName.has_value()) {
            setClauses.push_back("full_name = $" + std::to_string(paramIndex++));
            params.push_back(fullName.value());
        }

        if (isAdmin.has_value()) {
            setClauses.push_back("is_admin = $" + std::to_string(paramIndex++));
            params.push_back(isAdmin.value() ? "true" : "false");
        }

        if (!permissions.empty()) {
            // Convert permissions vector to JSON array string
            Json::Value permissionsJson = Json::arrayValue;
            for (const auto& perm : permissions) {
                permissionsJson.append(perm);
            }
            Json::StreamWriterBuilder writer;
            std::string permissionsStr = Json::writeString(writer, permissionsJson);

            std::string dbType = queryExecutor_->getDatabaseType();
            if (dbType == "postgres") {
                setClauses.push_back("permissions = $" + std::to_string(paramIndex++) + "::jsonb");
            } else {
                setClauses.push_back("permissions = $" + std::to_string(paramIndex++));
            }
            params.push_back(permissionsStr);
        }

        if (isActive.has_value()) {
            setClauses.push_back("is_active = $" + std::to_string(paramIndex++));
            params.push_back(isActive.value() ? "true" : "false");
        }

        if (setClauses.empty()) {
            spdlog::warn("[UserRepository] No fields to update for user: {}", id);
            return false;
        }

        // Add updated_at timestamp
        std::string dbType = queryExecutor_->getDatabaseType();
        if (dbType == "postgres") {
            setClauses.push_back("updated_at = NOW()");
        } else {
            setClauses.push_back("updated_at = SYSTIMESTAMP");
        }

        // Build final query
        std::string query = "UPDATE users SET ";
        for (size_t i = 0; i < setClauses.size(); ++i) {
            if (i > 0) query += ", ";
            query += setClauses[i];
        }
        query += " WHERE id = $" + std::to_string(paramIndex++);
        params.push_back(id);

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        if (rowsAffected == 0) {
            spdlog::warn("[UserRepository] User not found: {}", id);
            return false;
        }

        spdlog::info("[UserRepository] User updated successfully: {}", id);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] update failed: {}", e.what());
        throw std::runtime_error("Failed to update user: " + std::string(e.what()));
    }
}

std::optional<std::string> UserRepository::remove(const std::string& id)
{
    try {
        spdlog::debug("[UserRepository] Removing user: {}", id);

        std::string dbType = queryExecutor_->getDatabaseType();
        std::string username;

        if (dbType == "oracle") {
            // Oracle: Query username first, then delete (no RETURNING support)
            Json::Value userResult = queryExecutor_->executeQuery(
                "SELECT username FROM users WHERE id = $1", {id});
            if (userResult.empty()) {
                spdlog::warn("[UserRepository] User not found: {}", id);
                return std::nullopt;
            }
            username = userResult[0]["username"].asString();

            queryExecutor_->executeCommand(
                "DELETE FROM users WHERE id = $1", {id});
        } else {
            // PostgreSQL: DELETE with RETURNING
            const char* query = "DELETE FROM users WHERE id = $1 RETURNING username";
            Json::Value result = queryExecutor_->executeQuery(query, {id});

            if (result.empty()) {
                spdlog::warn("[UserRepository] User not found: {}", id);
                return std::nullopt;
            }
            username = result[0]["username"].asString();
        }

        spdlog::info("[UserRepository] User deleted: {} (ID: {})", username, id);

        return username;

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] remove failed: {}", e.what());
        throw std::runtime_error("Failed to remove user: " + std::string(e.what()));
    }
}

bool UserRepository::updateLastLogin(const std::string& id)
{
    try {
        spdlog::debug("[UserRepository] Updating last login for user: {}", id);

        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query;

        if (dbType == "postgres") {
            query = "UPDATE users SET last_login_at = NOW() WHERE id = $1";
        } else {
            query = "UPDATE users SET last_login_at = SYSTIMESTAMP WHERE id = :1";
        }

        std::vector<std::string> params = {id};
        int rowsAffected = queryExecutor_->executeCommand(query, params);

        if (rowsAffected == 0) {
            spdlog::warn("[UserRepository] User not found: {}", id);
            return false;
        }

        spdlog::debug("[UserRepository] Last login updated for user: {}", id);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] updateLastLogin failed: {}", e.what());
        throw std::runtime_error("Failed to update last login: " + std::string(e.what()));
    }
}

bool UserRepository::updatePassword(const std::string& id, const std::string& passwordHash)
{
    try {
        spdlog::debug("[UserRepository] Updating password for user: {}", id);

        const char* query = "UPDATE users SET password_hash = $1 WHERE id = $2";
        std::vector<std::string> params = {passwordHash, id};

        int rowsAffected = queryExecutor_->executeCommand(query, params);

        if (rowsAffected == 0) {
            spdlog::warn("[UserRepository] User not found: {}", id);
            return false;
        }

        spdlog::info("[UserRepository] Password updated for user: {}", id);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UserRepository] updatePassword failed: {}", e.what());
        throw std::runtime_error("Failed to update password: " + std::string(e.what()));
    }
}

// --- Private Helper Methods ---

domain::User UserRepository::jsonToUser(const Json::Value& json)
{
    domain::User user;

    user.setId(json["id"].asString());
    user.setUsername(json["username"].asString());
    user.setPasswordHash(json["password_hash"].asString());

    if (json.isMember("email") && !json["email"].isNull()) {
        user.setEmail(json["email"].asString());
    }

    if (json.isMember("full_name") && !json["full_name"].isNull()) {
        user.setFullName(json["full_name"].asString());
    }

    user.setPermissions(parsePermissions(json["permissions"]));

    // Handle is_active: PostgreSQL returns bool, Oracle returns "1"/"0" string
    bool isActive = false;
    spdlog::debug("[UserRepository] is_active parsing - isBool: {}, isString: {}, isInt: {}, isNull: {}, isUInt: {}, isDouble: {}",
                  json["is_active"].isBool(),
                  json["is_active"].isString(),
                  json["is_active"].isInt(),
                  json["is_active"].isNull(),
                  json["is_active"].isUInt(),
                  json["is_active"].isDouble());
    spdlog::debug("[UserRepository] is_active JSON type code: {}", static_cast<int>(json["is_active"].type()));

    if (json["is_active"].isNull()) {
        spdlog::warn("[UserRepository] is_active is NULL!");
    } else if (json["is_active"].isBool()) {
        isActive = json["is_active"].asBool();
        spdlog::debug("[UserRepository] Parsed is_active as bool: {}", isActive);
    } else if (json["is_active"].isString()) {
        std::string value = json["is_active"].asString();
        spdlog::debug("[UserRepository] is_active string value: '{}' (length: {})", value, value.length());
        isActive = (value == "1");
        spdlog::debug("[UserRepository] Parsed is_active as string: {}", isActive);
    } else if (json["is_active"].isInt()) {
        isActive = (json["is_active"].asInt() == 1);
        spdlog::debug("[UserRepository] Parsed is_active as int: {}", isActive);
    } else if (json["is_active"].isUInt()) {
        isActive = (json["is_active"].asUInt() == 1);
        spdlog::debug("[UserRepository] Parsed is_active as uint: {}", isActive);
    }
    user.setIsActive(isActive);
    spdlog::debug("[UserRepository] Final is_active value: {}", isActive);

    // Handle is_admin: PostgreSQL returns bool, Oracle returns "1"/"0" string
    bool isAdmin = false;
    if (json["is_admin"].isBool()) {
        isAdmin = json["is_admin"].asBool();
    } else if (json["is_admin"].isString()) {
        isAdmin = (json["is_admin"].asString() == "1");
    } else if (json["is_admin"].isInt()) {
        isAdmin = (json["is_admin"].asInt() == 1);
    }
    user.setIsAdmin(isAdmin);

    user.setCreatedAt(parseTimestamp(json["created_at"].asString()));

    if (json.isMember("last_login_at") && !json["last_login_at"].isNull()) {
        user.setLastLoginAt(parseTimestamp(json["last_login_at"].asString()));
    }

    user.setUpdatedAt(parseTimestamp(json["updated_at"].asString()));

    return user;
}

std::chrono::system_clock::time_point UserRepository::parseTimestamp(const std::string& timestamp)
{
    // Parse ISO 8601 timestamp (simplified version)
    // Format: "YYYY-MM-DD HH:MM:SS" or "YYYY-MM-DDTHH:MM:SSZ"

    std::tm tm = {};
    std::istringstream ss(timestamp);

    // Try different formats
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        ss.clear();
        ss.str(timestamp);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    }

    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::vector<std::string> UserRepository::parsePermissions(const Json::Value& json)
{
    std::vector<std::string> permissions;

    if (json.isArray()) {
        for (const auto& perm : json) {
            permissions.push_back(perm.asString());
        }
    } else if (json.isString()) {
        // If stored as JSON string, parse it
        Json::CharReaderBuilder builder;
        Json::Value parsed;
        std::istringstream stream(json.asString());
        std::string errs;

        if (Json::parseFromStream(builder, stream, &parsed, &errs)) {
            for (const auto& perm : parsed) {
                permissions.push_back(perm.asString());
            }
        }
    }

    return permissions;
}

} // namespace repositories

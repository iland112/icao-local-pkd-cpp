/**
 * @file config_manager.h
 * @brief Centralized Configuration Management
 *
 * Provides unified access to environment variables and configuration files.
 * Features:
 * - Environment variable access with defaults
 * - Type-safe configuration retrieval
 * - Configuration validation
 * - Thread-safe singleton pattern
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include <string>
#include <map>
#include <optional>
#include <mutex>
#include <memory>

namespace common {

/**
 * @brief Configuration Manager (Singleton)
 *
 * Centralized configuration management for all services
 */
class ConfigManager {
private:
    std::map<std::string, std::string> config_;
    mutable std::mutex mutex_;

    // Singleton instance
    static std::unique_ptr<ConfigManager> instance_;
    static std::once_flag initFlag_;

    // Private constructor (singleton)
    ConfigManager();

public:
    /**
     * @brief Get singleton instance
     */
    static ConfigManager& getInstance();

    // Delete copy and move
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    /**
     * @brief Get string configuration value
     * @param key Configuration key
     * @param defaultValue Default value if key not found
     * @return Configuration value
     */
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;

    /**
     * @brief Get integer configuration value
     * @param key Configuration key
     * @param defaultValue Default value if key not found
     * @return Configuration value
     */
    int getInt(const std::string& key, int defaultValue = 0) const;

    /**
     * @brief Get boolean configuration value
     * @param key Configuration key
     * @param defaultValue Default value if key not found
     * @return Configuration value
     */
    bool getBool(const std::string& key, bool defaultValue = false) const;

    /**
     * @brief Check if configuration key exists
     */
    bool has(const std::string& key) const;

    /**
     * @brief Set configuration value
     */
    void set(const std::string& key, const std::string& value);

    /**
     * @brief Load configuration from environment
     */
    void loadFromEnvironment();

    /**
     * @brief Get environment variable
     * @param key Environment variable name
     * @param defaultValue Default if not found
     * @return Environment variable value
     */
    static std::string getEnv(const std::string& key, const std::string& defaultValue = "");

    /// @name Predefined Configuration Keys

    // Database
    static constexpr const char* DB_HOST = "DB_HOST";
    static constexpr const char* DB_PORT = "DB_PORT";
    static constexpr const char* DB_NAME = "DB_NAME";
    static constexpr const char* DB_USER = "DB_USER";
    static constexpr const char* DB_PASSWORD = "DB_PASSWORD";
    static constexpr const char* DB_POOL_MIN = "DB_POOL_MIN";
    static constexpr const char* DB_POOL_MAX = "DB_POOL_MAX";

    // LDAP
    static constexpr const char* LDAP_HOST = "LDAP_HOST";
    static constexpr const char* LDAP_PORT = "LDAP_PORT";
    static constexpr const char* LDAP_BASE_DN = "LDAP_BASE_DN";
    static constexpr const char* LDAP_BIND_DN = "LDAP_BIND_DN";
    static constexpr const char* LDAP_BIND_PASSWORD = "LDAP_BIND_PASSWORD";
    static constexpr const char* LDAP_POOL_MIN = "LDAP_POOL_MIN";
    static constexpr const char* LDAP_POOL_MAX = "LDAP_POOL_MAX";

    // Service
    static constexpr const char* SERVICE_PORT = "SERVICE_PORT";
    static constexpr const char* SERVICE_THREADS = "SERVICE_THREADS";
    static constexpr const char* LOG_LEVEL = "LOG_LEVEL";
};

} // namespace common

/**
 * @file config_manager.cpp
 * @brief Implementation of Configuration Manager
 */

#include "config_manager.h"
#include <cstdlib>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace common {

// Static members
std::unique_ptr<ConfigManager> ConfigManager::instance_ = nullptr;
std::once_flag ConfigManager::initFlag_;

ConfigManager::ConfigManager() {
    // Load configuration from environment on construction
    loadFromEnvironment();
    spdlog::info("ConfigManager initialized");
}

ConfigManager& ConfigManager::getInstance() {
    std::call_once(initFlag_, []() {
        instance_.reset(new ConfigManager());
    });
    return *instance_;
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = config_.find(key);
    if (it != config_.end()) {
        return it->second;
    }

    // Try environment variable
    const char* env = std::getenv(key.c_str());
    if (env) {
        return std::string(env);
    }

    return defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
    std::string value = getString(key);
    if (value.empty()) {
        return defaultValue;
    }

    try {
        return std::stoi(value);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to parse integer config '{}': {} (using default: {})",
                     key, e.what(), defaultValue);
        return defaultValue;
    }
}

bool ConfigManager::getBool(const std::string& key, bool defaultValue) const {
    std::string value = getString(key);
    if (value.empty()) {
        return defaultValue;
    }

    // Convert to lowercase for comparison
    std::string lowerValue = value;
    std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);

    if (lowerValue == "true" || lowerValue == "1" || lowerValue == "yes" || lowerValue == "on") {
        return true;
    } else if (lowerValue == "false" || lowerValue == "0" || lowerValue == "no" || lowerValue == "off") {
        return false;
    }

    spdlog::warn("Invalid boolean config '{}': {} (using default: {})",
                 key, value, defaultValue);
    return defaultValue;
}

bool ConfigManager::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.find(key) != config_.end() || std::getenv(key.c_str()) != nullptr;
}

void ConfigManager::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_[key] = value;
    spdlog::debug("Config set: {} = {}", key, value);
}

void ConfigManager::loadFromEnvironment() {
    spdlog::info("Loading configuration from environment");

    // Database configuration
    if (const char* env = std::getenv(DB_HOST)) {
        set(DB_HOST, env);
    }
    if (const char* env = std::getenv(DB_PORT)) {
        set(DB_PORT, env);
    }
    if (const char* env = std::getenv(DB_NAME)) {
        set(DB_NAME, env);
    }
    if (const char* env = std::getenv(DB_USER)) {
        set(DB_USER, env);
    }
    if (const char* env = std::getenv(DB_PASSWORD)) {
        set(DB_PASSWORD, env);
    }
    if (const char* env = std::getenv(DB_POOL_MIN)) {
        set(DB_POOL_MIN, env);
    }
    if (const char* env = std::getenv(DB_POOL_MAX)) {
        set(DB_POOL_MAX, env);
    }

    // LDAP configuration
    if (const char* env = std::getenv(LDAP_HOST)) {
        set(LDAP_HOST, env);
    }
    if (const char* env = std::getenv(LDAP_PORT)) {
        set(LDAP_PORT, env);
    }
    if (const char* env = std::getenv(LDAP_BASE_DN)) {
        set(LDAP_BASE_DN, env);
    }
    if (const char* env = std::getenv(LDAP_BIND_DN)) {
        set(LDAP_BIND_DN, env);
    }
    if (const char* env = std::getenv(LDAP_BIND_PASSWORD)) {
        set(LDAP_BIND_PASSWORD, env);
    }
    if (const char* env = std::getenv(LDAP_POOL_MIN)) {
        set(LDAP_POOL_MIN, env);
    }
    if (const char* env = std::getenv(LDAP_POOL_MAX)) {
        set(LDAP_POOL_MAX, env);
    }

    // Service configuration
    if (const char* env = std::getenv(SERVICE_PORT)) {
        set(SERVICE_PORT, env);
    }
    if (const char* env = std::getenv(SERVICE_THREADS)) {
        set(SERVICE_THREADS, env);
    }
    if (const char* env = std::getenv(LOG_LEVEL)) {
        set(LOG_LEVEL, env);
    }

    spdlog::info("Configuration loaded from environment");
}

std::string ConfigManager::getEnv(const std::string& key, const std::string& defaultValue) {
    const char* env = std::getenv(key.c_str());
    return env ? std::string(env) : defaultValue;
}

} // namespace common

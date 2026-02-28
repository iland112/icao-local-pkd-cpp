#pragma once

/**
 * @file app_config.h
 * @brief Application configuration loaded from environment variables
 *
 * Extracted from main.cpp for reuse by ServiceContainer.
 */

#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <spdlog/spdlog.h>

struct AppConfig {
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "localpkd";
    std::string dbUser = "localpkd";
    std::string dbPassword;  // Must be set via environment variable

    // LDAP Read: Application-level load balancing
    // Format: "host1:port1,host2:port2,..."
    std::string ldapReadHosts = "openldap1:389,openldap2:389";
    std::vector<std::string> ldapReadHostList;  // Parsed from ldapReadHosts
    // Note: ldapReadRoundRobinIndex is defined as a global variable (atomic cannot be copied)

    // Legacy single host support (for backward compatibility)
    std::string ldapHost = "openldap1";
    int ldapPort = 389;

    // LDAP Write: Direct connection to primary master (openldap1) for write operations
    std::string ldapWriteHost = "openldap1";
    int ldapWritePort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword;  // Must be set via environment variable
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    // LDAP Container names (configurable via environment variables)
    std::string ldapDataContainer = "dc=data";        // For CSCA, DSC, LC, CRL
    std::string ldapNcDataContainer = "dc=nc-data";  // For non-conformant DSC

    // Trust Anchor for Master List CMS signature verification
    std::string trustAnchorPath = "/app/data/cert/UN_CSCA_2.pem";

    // ICAO Auto Sync Configuration
    std::string icaoPortalUrl = "https://pkddownloadsg.icao.int/";
    std::string notificationEmail = "admin@localhost";
    bool icaoAutoNotify = true;
    int icaoHttpTimeout = 10;  // seconds

    // ICAO Scheduler Configuration
    int icaoCheckScheduleHour = 9;   // 0-23, default 9 AM
    bool icaoSchedulerEnabled = true;

    // ASN.1 Parser Configuration
    int asn1MaxLines = 100;  // Default max lines for Master List structure parsing

    int serverPort = 8081;
    int threadNum = 4;
    int maxBodySizeMB = 100;  // HTTP upload body size limit (MB)

    // Safe environment variable integer parser with range clamping
    static int envStoi(const char* val, int defaultVal, int minVal, int maxVal) {
        try {
            int v = std::stoi(val);
            return std::clamp(v, minVal, maxVal);
        } catch (...) {
            spdlog::warn("Invalid integer env value '{}', using default {}", val, defaultVal);
            return defaultVal;
        }
    }

    static AppConfig fromEnvironment() {
        AppConfig config;

        if (auto val = std::getenv("DB_HOST")) config.dbHost = val;
        if (auto val = std::getenv("DB_PORT")) config.dbPort = envStoi(val, 5432, 1, 65535);
        if (auto val = std::getenv("DB_NAME")) config.dbName = val;
        if (auto val = std::getenv("DB_USER")) config.dbUser = val;
        if (auto val = std::getenv("DB_PASSWORD")) config.dbPassword = val;

        // LDAP Read Hosts (Application-level load balancing)
        if (auto val = std::getenv("LDAP_READ_HOSTS")) {
            config.ldapReadHosts = val;
            // Parse comma-separated host:port list
            std::stringstream ss(config.ldapReadHosts);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // Trim whitespace
                item.erase(0, item.find_first_not_of(" \t"));
                item.erase(item.find_last_not_of(" \t") + 1);
                if (!item.empty()) {
                    config.ldapReadHostList.push_back(item);
                }
            }
            if (config.ldapReadHostList.empty()) {
                throw std::runtime_error("LDAP_READ_HOSTS is empty or invalid");
            }
            spdlog::info("LDAP Read: {} hosts configured for load balancing", config.ldapReadHostList.size());
            for (const auto& host : config.ldapReadHostList) {
                spdlog::info("  - {}", host);
            }
        } else {
            // Fallback to single host for backward compatibility
            if (auto val = std::getenv("LDAP_HOST")) config.ldapHost = val;
            if (auto val = std::getenv("LDAP_PORT")) config.ldapPort = envStoi(val, 389, 1, 65535);
            config.ldapReadHostList.push_back(config.ldapHost + ":" + std::to_string(config.ldapPort));
            spdlog::warn("LDAP_READ_HOSTS not set, using single host: {}", config.ldapReadHostList[0]);
        }

        if (auto val = std::getenv("LDAP_WRITE_HOST")) config.ldapWriteHost = val;
        if (auto val = std::getenv("LDAP_WRITE_PORT")) config.ldapWritePort = envStoi(val, 389, 1, 65535);
        if (auto val = std::getenv("LDAP_BIND_DN")) config.ldapBindDn = val;
        if (auto val = std::getenv("LDAP_BIND_PASSWORD")) config.ldapBindPassword = val;
        if (auto val = std::getenv("LDAP_BASE_DN")) config.ldapBaseDn = val;
        if (auto val = std::getenv("LDAP_DATA_CONTAINER")) config.ldapDataContainer = val;
        if (auto val = std::getenv("LDAP_NC_DATA_CONTAINER")) config.ldapNcDataContainer = val;

        if (auto val = std::getenv("SERVER_PORT")) config.serverPort = envStoi(val, 8081, 1, 65535);
        if (auto val = std::getenv("THREAD_NUM")) config.threadNum = envStoi(val, 4, 1, 128);
        if (auto val = std::getenv("TRUST_ANCHOR_PATH")) config.trustAnchorPath = val;

        // ICAO Auto Sync environment variables
        if (auto val = std::getenv("ICAO_PORTAL_URL")) config.icaoPortalUrl = val;
        if (auto val = std::getenv("ICAO_NOTIFICATION_EMAIL")) config.notificationEmail = val;
        if (auto val = std::getenv("ICAO_AUTO_NOTIFY")) config.icaoAutoNotify = (std::string(val) == "true");
        if (auto val = std::getenv("ICAO_HTTP_TIMEOUT")) config.icaoHttpTimeout = envStoi(val, 30, 1, 300);

        // ASN.1 Parser Configuration
        if (auto val = std::getenv("ASN1_MAX_LINES")) config.asn1MaxLines = envStoi(val, 100, 10, 10000);

        // HTTP upload body size limit
        if (auto val = std::getenv("MAX_BODY_SIZE_MB")) config.maxBodySizeMB = envStoi(val, 100, 1, 500);

        // ICAO Scheduler Configuration
        if (auto val = std::getenv("ICAO_CHECK_SCHEDULE_HOUR")) {
            config.icaoCheckScheduleHour = envStoi(val, 9, 0, 23);
        }
        if (auto val = std::getenv("ICAO_SCHEDULER_ENABLED"))
            config.icaoSchedulerEnabled = (std::string(val) == "true");

        return config;
    }

    // Validate required credentials are set
    void validateRequiredCredentials() const {
        if (dbPassword.empty()) {
            throw std::runtime_error("FATAL: DB_PASSWORD environment variable not set");
        }
        if (ldapBindPassword.empty()) {
            throw std::runtime_error("FATAL: LDAP_BIND_PASSWORD environment variable not set");
        }
        spdlog::info("All required credentials loaded from environment");
    }
};

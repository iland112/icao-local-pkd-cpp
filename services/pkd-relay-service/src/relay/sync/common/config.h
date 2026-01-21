#pragma once

#include <string>
#include <cstdlib>

namespace icao {
namespace relay {

// =============================================================================
// Global Configuration
// =============================================================================
struct Config {
    // Server
    int serverPort = 8083;

    // Database
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "pkd";
    std::string dbUser = "pkd";
    std::string dbPassword = "pkd123";

    // LDAP (read)
    std::string ldapHost = "haproxy";
    int ldapPort = 389;

    // LDAP (write - for reconciliation)
    std::string ldapWriteHost = "openldap1";
    int ldapWritePort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword = "admin";
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    // Sync settings
    bool autoReconcile = true;
    int maxReconcileBatchSize = 100;

    // Daily scheduler settings
    bool dailySyncEnabled = true;
    int dailySyncHour = 0;      // 00:00 (midnight)
    int dailySyncMinute = 0;
    bool revalidateCertsOnSync = true;

    void loadFromEnv() {
        if (auto e = std::getenv("SERVER_PORT")) serverPort = std::stoi(e);
        if (auto e = std::getenv("DB_HOST")) dbHost = e;
        if (auto e = std::getenv("DB_PORT")) dbPort = std::stoi(e);
        if (auto e = std::getenv("DB_NAME")) dbName = e;
        if (auto e = std::getenv("DB_USER")) dbUser = e;
        if (auto e = std::getenv("DB_PASSWORD")) dbPassword = e;
        if (auto e = std::getenv("LDAP_HOST")) ldapHost = e;
        if (auto e = std::getenv("LDAP_PORT")) ldapPort = std::stoi(e);
        if (auto e = std::getenv("LDAP_WRITE_HOST")) ldapWriteHost = e;
        if (auto e = std::getenv("LDAP_WRITE_PORT")) ldapWritePort = std::stoi(e);
        if (auto e = std::getenv("LDAP_BIND_DN")) ldapBindDn = e;
        if (auto e = std::getenv("LDAP_BIND_PASSWORD")) ldapBindPassword = e;
        if (auto e = std::getenv("LDAP_BASE_DN")) ldapBaseDn = e;
        if (auto e = std::getenv("AUTO_RECONCILE")) autoReconcile = (std::string(e) == "true");
        if (auto e = std::getenv("MAX_RECONCILE_BATCH_SIZE")) maxReconcileBatchSize = std::stoi(e);
        if (auto e = std::getenv("DAILY_SYNC_ENABLED")) dailySyncEnabled = (std::string(e) == "true");
        if (auto e = std::getenv("DAILY_SYNC_HOUR")) dailySyncHour = std::stoi(e);
        if (auto e = std::getenv("DAILY_SYNC_MINUTE")) dailySyncMinute = std::stoi(e);
        if (auto e = std::getenv("REVALIDATE_CERTS_ON_SYNC")) revalidateCertsOnSync = (std::string(e) == "true");
    }

    // Load user-configurable settings from database (implemented in config.cpp)
    bool loadFromDatabase();
};

} // namespace relay
} // namespace icao

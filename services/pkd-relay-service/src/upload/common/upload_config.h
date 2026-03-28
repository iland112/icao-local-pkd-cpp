/**
 * @file upload_config.h
 * @brief Lightweight AppConfig-compatible struct for upload module
 *
 * LdapStorageService (ported from pkd-management) expects AppConfig.
 * This provides the same fields, populated from relay's icao::relay::Config
 * and environment variables.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdlib>

struct AppConfig {
    // LDAP Write
    std::string ldapWriteHost = "openldap1";
    int ldapWritePort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword;
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    // LDAP Read (round-robin)
    std::vector<std::string> ldapReadHostList;

    // LDAP Container names
    std::string ldapDataContainer = "dc=data";
    std::string ldapNcDataContainer = "dc=nc-data";

    // Trust Anchor (for Master List CMS signature verification)
    std::string trustAnchorPath = "/app/data/cert/UN_CSCA_2.pem";

    // ASN.1 Parser Configuration
    int asn1MaxLines = 100;

    /** @brief Load upload-specific settings from environment variables with validation */
    void loadFromEnv() {
        if (auto e = std::getenv("TRUST_ANCHOR_PATH")) {
            std::string path(e);
            // Path traversal prevention: reject ".." and ensure absolute path
            if (path.find("..") == std::string::npos && !path.empty() && path[0] == '/') {
                trustAnchorPath = path;
            }
        }
        if (auto e = std::getenv("ASN1_MAX_LINES")) {
            try { asn1MaxLines = std::max(10, std::min(100000, std::stoi(e))); } catch (...) {}
        }
        if (auto e = std::getenv("LDAP_WRITE_HOST")) {
            std::string host(e);
            // Hostname validation: alphanumeric, dots, hyphens only
            bool valid = !host.empty() && host.size() <= 253;
            for (char c : host) { if (!std::isalnum(c) && c != '.' && c != '-') { valid = false; break; } }
            if (valid) ldapWriteHost = host;
        }
        if (auto e = std::getenv("LDAP_WRITE_PORT")) {
            try { int p = std::stoi(e); if (p >= 1 && p <= 65535) ldapWritePort = p; } catch (...) {}
        }
        if (auto e = std::getenv("LDAP_BIND_DN")) ldapBindDn = e;
        if (auto e = std::getenv("LDAP_BIND_PASSWORD")) ldapBindPassword = e;
        if (auto e = std::getenv("LDAP_BASE_DN")) ldapBaseDn = e;
        if (auto e = std::getenv("LDAP_DATA_CONTAINER")) ldapDataContainer = e;
        if (auto e = std::getenv("LDAP_NC_DATA_CONTAINER")) ldapNcDataContainer = e;
    }
};

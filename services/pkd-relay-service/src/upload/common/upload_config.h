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

    /** @brief Load upload-specific settings from environment variables */
    void loadFromEnv() {
        if (auto e = std::getenv("TRUST_ANCHOR_PATH")) trustAnchorPath = e;
        if (auto e = std::getenv("ASN1_MAX_LINES")) {
            try { asn1MaxLines = std::stoi(e); } catch (...) {}
        }
        if (auto e = std::getenv("LDAP_WRITE_HOST")) ldapWriteHost = e;
        if (auto e = std::getenv("LDAP_WRITE_PORT")) {
            try { ldapWritePort = std::stoi(e); } catch (...) {}
        }
        if (auto e = std::getenv("LDAP_BIND_DN")) ldapBindDn = e;
        if (auto e = std::getenv("LDAP_BIND_PASSWORD")) ldapBindPassword = e;
        if (auto e = std::getenv("LDAP_BASE_DN")) ldapBaseDn = e;
        if (auto e = std::getenv("LDAP_DATA_CONTAINER")) ldapDataContainer = e;
        if (auto e = std::getenv("LDAP_NC_DATA_CONTAINER")) ldapNcDataContainer = e;
    }
};

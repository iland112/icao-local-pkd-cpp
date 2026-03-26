/**
 * @file upload_config.h
 * @brief Lightweight AppConfig-compatible struct for upload module
 *
 * LdapStorageService (ported from pkd-management) expects AppConfig.
 * This provides the same fields, populated from relay's icao::relay::Config.
 */
#pragma once

#include <string>
#include <vector>

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

    // Trust Anchor (for Master List CMS signature)
    std::string trustAnchorPath = "/app/data/cert/UN_CSCA_2.pem";
};

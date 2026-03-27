/**
 * @file ldap_storage_factory.cpp
 * @brief Factory for LdapStorageService — avoids namespace ambiguity in main.cpp
 */
#include "upload/upload_services.h"
#include "upload/services/ldap_storage_service.h"
#include "upload/common/upload_config.h"
#include <memory>
#include <string>

void initLdapStorageService(infrastructure::UploadServiceContainer* sc,
    const std::string& writeHost, int writePort,
    const std::string& bindDn, const std::string& bindPassword,
    const std::string& baseDn,
    const std::string& dataContainer, const std::string& ncDataContainer)
{
    AppConfig cfg;
    cfg.ldapWriteHost = writeHost;
    cfg.ldapWritePort = writePort;
    cfg.ldapBindDn = bindDn;
    cfg.ldapBindPassword = bindPassword;
    cfg.ldapBaseDn = baseDn;
    cfg.ldapDataContainer = dataContainer;
    cfg.ldapNcDataContainer = ncDataContainer;
    sc->setLdapStorageService(std::make_shared<services::LdapStorageService>(cfg));
}

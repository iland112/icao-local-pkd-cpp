/**
 * @file icao_ldap_client.h
 * @brief LDAP V3 client for connecting to ICAO PKD (simulation or production)
 *
 * Connects to the ICAO PKD LDAP server and retrieves certificates/CRLs
 * using standard LDAP V3 search operations.
 */
#pragma once

#include "icao_ldap_types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declare LDAP type
typedef struct ldap LDAP;

namespace icao {
namespace relay {

class IcaoLdapClient {
public:
    IcaoLdapClient(const std::string& host, int port,
                   const std::string& bindDn, const std::string& bindPassword,
                   const std::string& baseDn);
    ~IcaoLdapClient();

    // Non-copyable
    IcaoLdapClient(const IcaoLdapClient&) = delete;
    IcaoLdapClient& operator=(const IcaoLdapClient&) = delete;

    /// Connect and bind to ICAO PKD LDAP
    bool connect();

    /// Disconnect
    void disconnect();

    /// Check if connected
    bool isConnected() const { return ldap_ != nullptr; }

    /// Search for all DSC certificates under dc=data
    std::vector<IcaoLdapCertEntry> searchDscCertificates(int maxResults = 0);

    /// Search for all CSCA certificates (from o=csca branches)
    std::vector<IcaoLdapCertEntry> searchCscaCertificates(int maxResults = 0);

    /// Search for all CRLs under dc=data
    std::vector<IcaoLdapCertEntry> searchCrls(int maxResults = 0);

    /// Search for all non-conformant DSC under dc=nc-data
    std::vector<IcaoLdapCertEntry> searchNcDscCertificates(int maxResults = 0);

    /// Search for Master Lists under dc=data
    std::vector<IcaoLdapCertEntry> searchMasterLists(int maxResults = 0);

    /// Get total entry count (quick estimate)
    int getTotalEntryCount();

private:
    /// Generic LDAP search with result parsing
    std::vector<IcaoLdapCertEntry> searchEntries(
        const std::string& searchBase,
        const std::string& filter,
        const std::string& certType,
        int maxResults);

    /// Extract binary attribute value from LDAP entry
    std::vector<uint8_t> extractBinaryAttribute(void* entry, const std::string& attrName);

    /// Extract string attribute value from LDAP entry
    std::string extractStringAttribute(void* entry, const std::string& attrName);

    /// Extract country code from DN
    std::string extractCountryFromDn(const std::string& dn);

    std::string host_;
    int port_;
    std::string bindDn_;
    std::string bindPassword_;
    std::string baseDn_;
    LDAP* ldap_ = nullptr;
};

} // namespace relay
} // namespace icao

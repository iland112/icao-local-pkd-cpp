/**
 * @file icao_ldap_client.h
 * @brief LDAP V3 client for connecting to ICAO PKD (simulation or production)
 *
 * Supports two authentication modes:
 * 1. Simple Bind (simulation): DN + password
 * 2. TLS Mutual Auth (production): Client certificate + SASL EXTERNAL
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

/// TLS configuration for ICAO PKD LDAP client certificate authentication
struct IcaoLdapTlsConfig {
    bool enabled = false;
    std::string certFile;    // Client certificate PEM file path
    std::string keyFile;     // Client private key PEM file path
    std::string caCertFile;  // CA certificate PEM file path (D-Trust CA)
    // Fallback: Simple Bind over TLS (LDAPS + DN/password)
    std::string bindDn;      // If set, use Simple Bind over LDAPS instead of SASL EXTERNAL
    std::string bindPassword;
};

class IcaoLdapClient {
public:
    /// Constructor for Simple Bind mode (simulation)
    IcaoLdapClient(const std::string& host, int port,
                   const std::string& bindDn, const std::string& bindPassword,
                   const std::string& baseDn);

    /// Constructor for TLS Mutual Auth mode (production ICAO PKD)
    IcaoLdapClient(const std::string& host, int port,
                   const std::string& baseDn,
                   const IcaoLdapTlsConfig& tlsConfig);

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

    /// Get the last connection error message (empty if no error)
    const std::string& lastError() const { return lastError_; }

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
    /// Connect with Simple Bind (simulation mode)
    bool connectSimpleBind();

    /// Connect with TLS + SASL EXTERNAL (production mode)
    bool connectTlsMutualAuth();

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
    IcaoLdapTlsConfig tlsConfig_;
    LDAP* ldap_ = nullptr;
    std::string lastError_;
};

} // namespace relay
} // namespace icao

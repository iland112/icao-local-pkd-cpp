/**
 * @file ldap_csca_provider.h
 * @brief ICscaProvider adapter for LDAP-backed CSCA lookup (pool-based)
 *
 * Uses LdapConnectionPool for thread-safe, per-request LDAP connections.
 * Enables real-time CSCA lookup for PA Lookup endpoint (ICAO Doc 9303 compliant).
 *
 * @date 2026-03-10
 */

#pragma once

#include <icao/validation/providers.h>
#include <ldap_connection_pool.h>
#include <string>

namespace adapters {

class LdapCscaProvider : public icao::validation::ICscaProvider {
public:
    /**
     * @brief Constructor
     * @param ldapPool LDAP connection pool (non-owning, must outlive this object)
     * @param baseDn LDAP base DN (e.g., "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com")
     */
    LdapCscaProvider(common::LdapConnectionPool* ldapPool, const std::string& baseDn);

    std::vector<X509*> findAllCscasByIssuerDn(const std::string& issuerDn) override;
    X509* findCscaByIssuerDn(const std::string& issuerDn, const std::string& countryCode) override;

private:
    common::LdapConnectionPool* ldapPool_;
    std::string baseDn_;

    /**
     * @brief Search LDAP for CSCA certificates in a specific OU for a country
     * @param conn LDAP connection
     * @param ou Organization unit ("csca" or "lc")
     * @param countryCode Country code
     * @return Vector of X509* (caller owns)
     */
    std::vector<X509*> searchCscas(LDAP* conn, const std::string& ou, const std::string& countryCode);

    /**
     * @brief Extract country code from DN string
     */
    std::string extractCountryFromDn(const std::string& dn);
};

} // namespace adapters

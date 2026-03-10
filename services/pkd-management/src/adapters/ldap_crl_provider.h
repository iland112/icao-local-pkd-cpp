/**
 * @file ldap_crl_provider.h
 * @brief ICrlProvider adapter for LDAP-backed CRL lookup (pool-based)
 *
 * Uses LdapConnectionPool for thread-safe, per-request LDAP connections.
 * Enables real-time CRL check for PA Lookup endpoint (ICAO Doc 9303 compliant).
 *
 * @date 2026-03-10
 */

#pragma once

#include <icao/validation/providers.h>
#include <ldap_connection_pool.h>
#include <string>

namespace adapters {

class LdapCrlProvider : public icao::validation::ICrlProvider {
public:
    /**
     * @brief Constructor
     * @param ldapPool LDAP connection pool (non-owning, must outlive this object)
     * @param baseDn LDAP base DN
     */
    LdapCrlProvider(common::LdapConnectionPool* ldapPool, const std::string& baseDn);

    X509_CRL* findCrlByCountry(const std::string& countryCode) override;

private:
    common::LdapConnectionPool* ldapPool_;
    std::string baseDn_;
};

} // namespace adapters

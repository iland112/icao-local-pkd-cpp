#pragma once

#include <string>
#include <optional>
#include <vector>
#include <openssl/x509.h>

namespace pa::domain::port {

/**
 * Port interface for CSCA (Country Signing CA) lookup from LDAP.
 *
 * Provides CSCA certificate retrieval for trust chain validation.
 */
class LdapCscaPort {
public:
    virtual ~LdapCscaPort() = default;

    /**
     * Find CSCA certificate by subject DN.
     *
     * @param subjectDn Subject DN of CSCA
     * @return X509* certificate if found (caller takes ownership), nullptr otherwise
     */
    virtual X509* findBySubjectDn(const std::string& subjectDn) = 0;

    /**
     * Find all CSCA certificates for a country.
     *
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @return vector of X509* certificates (caller takes ownership of each)
     */
    virtual std::vector<X509*> findByCountry(const std::string& countryCode) = 0;

    /**
     * Check if CSCA exists in LDAP.
     *
     * @param subjectDn Subject DN of CSCA
     * @return true if exists
     */
    virtual bool existsBySubjectDn(const std::string& subjectDn) = 0;
};

} // namespace pa::domain::port

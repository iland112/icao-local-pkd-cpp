/**
 * @file ldap_crl_provider.h
 * @brief ICrlProvider adapter for LDAP-backed CRL lookup
 *
 * Bridges icao::validation::ICrlProvider to LdapCrlRepository.
 */

#pragma once

#include <icao/validation/providers.h>
#include "../repositories/ldap_crl_repository.h"

namespace adapters {

class LdapCrlProvider : public icao::validation::ICrlProvider {
public:
    explicit LdapCrlProvider(repositories::LdapCrlRepository* crlRepo);

    X509_CRL* findCrlByCountry(const std::string& countryCode) override;

private:
    repositories::LdapCrlRepository* crlRepo_;
};

} // namespace adapters

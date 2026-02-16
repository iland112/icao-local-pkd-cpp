/**
 * @file ldap_csca_provider.h
 * @brief ICscaProvider adapter for LDAP-backed CSCA lookup
 *
 * Bridges icao::validation::ICscaProvider to LdapCertificateRepository.
 */

#pragma once

#include <icao/validation/providers.h>
#include "../repositories/ldap_certificate_repository.h"

namespace adapters {

class LdapCscaProvider : public icao::validation::ICscaProvider {
public:
    explicit LdapCscaProvider(repositories::LdapCertificateRepository* certRepo);

    std::vector<X509*> findAllCscasByIssuerDn(const std::string& issuerDn) override;
    X509* findCscaByIssuerDn(const std::string& issuerDn, const std::string& countryCode) override;

private:
    repositories::LdapCertificateRepository* certRepo_;
};

} // namespace adapters

/**
 * @file ldap_csca_provider.cpp
 * @brief ICscaProvider adapter implementation for LDAP-backed CSCA lookup
 */

#include "ldap_csca_provider.h"
#include <icao/validation/cert_ops.h>
#include <stdexcept>

namespace adapters {

LdapCscaProvider::LdapCscaProvider(repositories::LdapCertificateRepository* certRepo)
    : certRepo_(certRepo)
{
    if (!certRepo_) {
        throw std::invalid_argument("LdapCscaProvider: certRepo cannot be nullptr");
    }
}

std::vector<X509*> LdapCscaProvider::findAllCscasByIssuerDn(const std::string& issuerDn) {
    // Extract country code from issuer DN for LDAP search scope
    std::string countryCode = icao::validation::extractDnAttribute(issuerDn, "C");
    if (countryCode.empty()) {
        return {};
    }
    // Convert to uppercase for LDAP country code
    for (char& c : countryCode) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return certRepo_->findAllCscasByCountry(countryCode);
}

X509* LdapCscaProvider::findCscaByIssuerDn(
    const std::string& issuerDn, const std::string& countryCode)
{
    std::string cc = countryCode;
    if (cc.empty()) {
        cc = icao::validation::extractDnAttribute(issuerDn, "C");
        for (char& c : cc) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return certRepo_->findCscaByIssuerDn(issuerDn, cc);
}

} // namespace adapters

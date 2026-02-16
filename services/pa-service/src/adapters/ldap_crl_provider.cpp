/**
 * @file ldap_crl_provider.cpp
 * @brief ICrlProvider adapter implementation for LDAP-backed CRL lookup
 */

#include "ldap_crl_provider.h"
#include <stdexcept>

namespace adapters {

LdapCrlProvider::LdapCrlProvider(repositories::LdapCrlRepository* crlRepo)
    : crlRepo_(crlRepo)
{
    if (!crlRepo_) {
        throw std::invalid_argument("LdapCrlProvider: crlRepo cannot be nullptr");
    }
}

X509_CRL* LdapCrlProvider::findCrlByCountry(const std::string& countryCode) {
    return crlRepo_->findCrlByCountry(countryCode);
}

} // namespace adapters

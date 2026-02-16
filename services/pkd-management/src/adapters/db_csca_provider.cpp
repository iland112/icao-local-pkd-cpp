/**
 * @file db_csca_provider.cpp
 * @brief ICscaProvider adapter implementation for database-backed CSCA lookup
 */

#include "db_csca_provider.h"
#include <stdexcept>

namespace adapters {

DbCscaProvider::DbCscaProvider(repositories::CertificateRepository* certRepo)
    : certRepo_(certRepo)
{
    if (!certRepo_) {
        throw std::invalid_argument("DbCscaProvider: certRepo cannot be nullptr");
    }
}

std::vector<X509*> DbCscaProvider::findAllCscasByIssuerDn(const std::string& issuerDn) {
    // CertificateRepository::findAllCscasBySubjectDn searches by subject DN
    // which matches the issuer DN of the child certificate
    return certRepo_->findAllCscasBySubjectDn(issuerDn);
}

X509* DbCscaProvider::findCscaByIssuerDn(
    const std::string& issuerDn, const std::string& /*countryCode*/)
{
    return certRepo_->findCscaByIssuerDn(issuerDn);
}

} // namespace adapters

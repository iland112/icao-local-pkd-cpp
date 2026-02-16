/**
 * @file db_csca_provider.h
 * @brief ICscaProvider adapter for database-backed CSCA lookup
 *
 * Bridges icao::validation::ICscaProvider to CertificateRepository.
 */

#pragma once

#include <icao/validation/providers.h>
#include "../repositories/certificate_repository.h"

namespace adapters {

class DbCscaProvider : public icao::validation::ICscaProvider {
public:
    explicit DbCscaProvider(repositories::CertificateRepository* certRepo);

    std::vector<X509*> findAllCscasByIssuerDn(const std::string& issuerDn) override;
    X509* findCscaByIssuerDn(const std::string& issuerDn, const std::string& countryCode) override;

private:
    repositories::CertificateRepository* certRepo_;
};

} // namespace adapters

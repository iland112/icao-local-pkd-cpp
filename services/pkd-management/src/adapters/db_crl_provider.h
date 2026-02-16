/**
 * @file db_crl_provider.h
 * @brief ICrlProvider adapter for database-backed CRL lookup
 *
 * Bridges icao::validation::ICrlProvider to CrlRepository.
 */

#pragma once

#include <icao/validation/providers.h>
#include "../repositories/crl_repository.h"

namespace adapters {

class DbCrlProvider : public icao::validation::ICrlProvider {
public:
    explicit DbCrlProvider(repositories::CrlRepository* crlRepo);

    X509_CRL* findCrlByCountry(const std::string& countryCode) override;

private:
    repositories::CrlRepository* crlRepo_;
};

} // namespace adapters

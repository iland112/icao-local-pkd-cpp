/**
 * @file relay_crl_provider.h
 * @brief ICrlProvider adapter for PKD Relay Service
 *
 * Bridges icao::validation::ICrlProvider to relay CrlRepository.
 */

#pragma once

#include <icao/validation/providers.h>
#include "../repositories/crl_repository.h"

namespace icao::relay::adapters {

class RelayCrlProvider : public icao::validation::ICrlProvider {
public:
    explicit RelayCrlProvider(repositories::CrlRepository* crlRepo);

    X509_CRL* findCrlByCountry(const std::string& countryCode) override;

private:
    repositories::CrlRepository* crlRepo_;
};

} // namespace icao::relay::adapters

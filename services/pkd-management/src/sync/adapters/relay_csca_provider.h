/**
 * @file relay_csca_provider.h
 * @brief ICscaProvider adapter for PKD Relay Service
 *
 * Bridges icao::validation::ICscaProvider to relay CertificateRepository.
 * In-memory CSCA cache for bulk DSC re-validation performance.
 */

#pragma once

#include <icao/validation/providers.h>
#include "../repositories/certificate_repository.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

namespace icao::relay::adapters {

class RelayCscaProvider : public icao::validation::ICscaProvider {
public:
    explicit RelayCscaProvider(repositories::CertificateRepository* certRepo);

    std::vector<X509*> findAllCscasByIssuerDn(const std::string& issuerDn) override;
    X509* findCscaByIssuerDn(const std::string& issuerDn, const std::string& countryCode) override;

    /**
     * @brief Preload all CSCA certificates into memory cache
     *
     * Loads all ~845 CSCAs in a single DB query. Subsequent findAllCscasByIssuerDn()
     * calls use the cache instead of per-DSC DB queries.
     */
    void preloadAllCscas();

private:
    repositories::CertificateRepository* certRepo_;

    // In-memory cache: normalizedDn -> vector of DER-encoded certificate bytes
    std::unordered_map<std::string, std::vector<std::vector<uint8_t>>> cscaCache_;
    bool cacheLoaded_ = false;
};

} // namespace icao::relay::adapters

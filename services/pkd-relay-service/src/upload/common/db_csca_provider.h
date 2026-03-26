/**
 * @file db_csca_provider.h
 * @brief ICscaProvider adapter for database-backed CSCA lookup
 *
 * Bridges icao::validation::ICscaProvider to CertificateRepository.
 * Supports in-memory CSCA cache for LDIF bulk processing performance.
 */

#pragma once

#include <icao/validation/providers.h>
#include "upload/repositories/certificate_repository.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

namespace adapters {

class DbCscaProvider : public icao::validation::ICscaProvider {
public:
    explicit DbCscaProvider(repositories::CertificateRepository* certRepo);

    std::vector<X509*> findAllCscasByIssuerDn(const std::string& issuerDn) override;
    X509* findCscaByIssuerDn(const std::string& issuerDn, const std::string& countryCode) override;

    /**
     * @brief Preload all CSCA certificates into memory cache
     *
     * Loads all ~845 CSCAs in a single DB query. Subsequent findAllCscasByIssuerDn()
     * calls use the cache instead of per-DSC DB queries (~30K queries eliminated).
     * Also eliminates Oracle LOB session drop overhead for CSCA BLOB reads.
     */
    void preloadAllCscas();

    /**
     * @brief Invalidate the CSCA cache (lazy reload on next access)
     *
     * Call when new CSCAs are added to DB during processing.
     * Next findAllCscasByIssuerDn() call will trigger automatic preload.
     */
    void invalidateCache();

private:
    repositories::CertificateRepository* certRepo_;

    // In-memory cache: normalizedDn → vector of DER-encoded certificate bytes
    std::unordered_map<std::string, std::vector<std::vector<uint8_t>>> cscaCache_;
    bool cacheLoaded_ = false;
};

} // namespace adapters

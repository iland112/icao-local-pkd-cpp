/**
 * @file relay_csca_provider.cpp
 * @brief ICscaProvider adapter for PKD Relay Service
 *
 * In-memory CSCA cache: preloadAllCscas() loads all ~845 CSCAs once,
 * eliminating per-DSC DB queries during bulk re-validation.
 */

#include "relay_csca_provider.h"
#include <icao/validation/cert_ops.h>
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <stdexcept>

namespace icao::relay::adapters {

RelayCscaProvider::RelayCscaProvider(repositories::CertificateRepository* certRepo)
    : certRepo_(certRepo)
{
    if (!certRepo_) {
        throw std::invalid_argument("RelayCscaProvider: certRepo cannot be nullptr");
    }
}

void RelayCscaProvider::preloadAllCscas() {
    auto allCscas = certRepo_->findAllCscas();
    cscaCache_.clear();

    for (auto& [subjectDn, derBytes] : allCscas) {
        std::string normalizedDn = icao::validation::normalizeDnForComparison(subjectDn);
        cscaCache_[normalizedDn].push_back(std::move(derBytes));
    }

    cacheLoaded_ = true;
    spdlog::info("[RelayCscaProvider] Preloaded {} CSCA entries ({} unique DNs)",
                 allCscas.size(), cscaCache_.size());
}

std::vector<X509*> RelayCscaProvider::findAllCscasByIssuerDn(const std::string& issuerDn) {
    if (!cacheLoaded_) {
        preloadAllCscas();
    }

    std::string normalizedDn = icao::validation::normalizeDnForComparison(issuerDn);
    auto it = cscaCache_.find(normalizedDn);
    if (it != cscaCache_.end()) {
        std::vector<X509*> result;
        for (const auto& derBytes : it->second) {
            const unsigned char* p = derBytes.data();
            X509* cert = d2i_X509(nullptr, &p, static_cast<long>(derBytes.size()));
            if (cert) {
                result.push_back(cert);
            }
        }
        return result;
    }
    return {};
}

X509* RelayCscaProvider::findCscaByIssuerDn(
    const std::string& issuerDn, const std::string& /*countryCode*/)
{
    auto cscas = findAllCscasByIssuerDn(issuerDn);
    if (!cscas.empty()) {
        // Free all but the first
        for (size_t i = 1; i < cscas.size(); i++) {
            X509_free(cscas[i]);
        }
        return cscas[0];
    }
    return nullptr;
}

} // namespace icao::relay::adapters

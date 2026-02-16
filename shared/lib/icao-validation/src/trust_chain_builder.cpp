/**
 * @file trust_chain_builder.cpp
 * @brief Trust chain builder implementation
 *
 * Consolidated from:
 *   - services/pkd-management/src/services/validation_service.cpp:467-629 (buildTrustChain)
 *   - services/pkd-management/src/services/validation_service.cpp:682-734 (validateTrustChainInternal)
 *
 * ICAO Doc 9303 Part 12 hybrid chain model:
 *   - Signature verification: HARD requirement
 *   - Expiration: informational only
 */

#include "icao/validation/trust_chain_builder.h"
#include "icao/validation/cert_ops.h"

#include <set>
#include <stdexcept>
#include <ctime>
#include <openssl/evp.h>

namespace icao::validation {

TrustChainBuilder::TrustChainBuilder(ICscaProvider* cscaProvider)
    : cscaProvider_(cscaProvider)
{
    if (!cscaProvider_) {
        throw std::invalid_argument("TrustChainBuilder: cscaProvider cannot be nullptr");
    }
}

TrustChainResult TrustChainBuilder::build(X509* leafCert, int maxDepth) {
    TrustChainResult result;

    if (!leafCert) {
        result.message = "Leaf certificate is null";
        return result;
    }

    // Step 1: Get issuer DN from leaf certificate
    std::string leafIssuerDn = getIssuerDn(leafCert);
    if (leafIssuerDn.empty()) {
        result.message = "Failed to extract issuer DN from leaf certificate";
        return result;
    }

    // Check DSC expiration (informational per ICAO hybrid model)
    result.dscExpired = isCertificateExpired(leafCert);

    // Step 2: Find ALL CSCAs matching the issuer DN (key rollover support)
    std::vector<X509*> allCscas = cscaProvider_->findAllCscasByIssuerDn(leafIssuerDn);
    if (allCscas.empty()) {
        result.message = "No CSCA found for issuer: " + leafIssuerDn.substr(0, 80);
        return result;
    }

    // Step 3: Build chain iteratively
    // certificates tracks the chain path; we don't own leafCert but we own CSCA pointers
    struct ChainEntry {
        X509* cert;
        bool owned;  // true if we need to free this (CSCA from provider)
    };
    std::vector<ChainEntry> chain;
    chain.push_back({leafCert, false});

    X509* current = leafCert;
    std::set<std::string> visitedDns;
    int depth = 0;

    while (depth < maxDepth) {
        depth++;

        // Check if current certificate is self-signed (root)
        if (isSelfSigned(current)) {
            // Verify self-signature (RFC 5280 Section 6.1)
            if (!verifyCertificateSignature(current, current)) {
                result.message = "Root CSCA self-signature verification failed at depth " + std::to_string(depth);
                // Cleanup owned certs
                for (auto& entry : chain) {
                    if (entry.owned) X509_free(entry.cert);
                }
                // Free remaining allCscas not in chain
                for (X509* csca : allCscas) {
                    bool inChain = false;
                    for (auto& entry : chain) {
                        if (entry.cert == csca) { inChain = true; break; }
                    }
                    if (!inChain) X509_free(csca);
                }
                return result;
            }
            result.valid = true;
            result.cscaSubjectDn = getSubjectDn(current);
            result.cscaFingerprint = getCertificateFingerprint(current);
            break;
        }

        // Get issuer DN of current certificate
        std::string currentIssuerDn = getIssuerDn(current);
        if (currentIssuerDn.empty()) {
            result.message = "Failed to extract issuer DN at depth " + std::to_string(depth);
            break;
        }

        // Prevent circular references
        if (visitedDns.count(currentIssuerDn) > 0) {
            result.message = "Circular reference detected at depth " + std::to_string(depth);
            break;
        }
        visitedDns.insert(currentIssuerDn);

        // Find issuer in CSCA list by signature verification (key rollover)
        X509* issuer = nullptr;
        X509* dnMatchFallback = nullptr;

        for (X509* csca : allCscas) {
            std::string cscaSubjectDn = getSubjectDn(csca);
            if (strcasecmp(currentIssuerDn.c_str(), cscaSubjectDn.c_str()) == 0) {
                // DN matches — verify signature to confirm correct key pair
                if (verifyCertificateSignature(current, csca)) {
                    issuer = csca;
                    break;
                } else {
                    if (!dnMatchFallback) dnMatchFallback = csca;
                }
            }
        }

        if (!issuer && dnMatchFallback) {
            issuer = dnMatchFallback;
        }

        if (!issuer) {
            // Try fetching from provider with the new issuer DN (for link cert chains)
            std::vector<X509*> moreCscas = cscaProvider_->findAllCscasByIssuerDn(currentIssuerDn);
            for (X509* csca : moreCscas) {
                if (verifyCertificateSignature(current, csca)) {
                    issuer = csca;
                    allCscas.push_back(csca);  // Track for cleanup
                    break;
                } else {
                    // Not the right one, but keep for potential DN match
                    if (!dnMatchFallback) dnMatchFallback = csca;
                    allCscas.push_back(csca);
                }
            }
            if (!issuer && dnMatchFallback) {
                issuer = dnMatchFallback;
            }
        }

        if (!issuer) {
            result.message = "Chain broken: Issuer not found at depth " +
                             std::to_string(depth) + " (issuer: " +
                             currentIssuerDn.substr(0, 80) + ")";
            break;
        }

        chain.push_back({issuer, true});
        current = issuer;
    }

    if (depth >= maxDepth && !result.valid) {
        result.message = "Maximum chain depth exceeded (" + std::to_string(maxDepth) + ")";
    }

    // Step 4: Validate signatures in chain and check expiration (ICAO hybrid model)
    if (result.valid && chain.size() >= 2) {
        time_t now = time(nullptr);

        for (size_t i = 0; i + 1 < chain.size(); i++) {
            X509* cert = chain[i].cert;
            X509* issuerCert = chain[i + 1].cert;

            // Verify signature (HARD requirement)
            if (!verifyCertificateSignature(cert, issuerCert)) {
                result.valid = false;
                result.message = "Signature verification failed at depth " + std::to_string(i);
                break;
            }
        }

        // Check CSCA expiration (informational)
        for (size_t i = 1; i < chain.size(); i++) {
            if (X509_cmp_time(X509_get0_notAfter(chain[i].cert), &now) < 0) {
                result.cscaExpired = true;
            }
        }
    }

    // Step 5: Build human-readable path
    result.depth = static_cast<int>(chain.size());
    result.path = "DSC";
    for (size_t i = 1; i < chain.size(); i++) {
        if (isSelfSigned(chain[i].cert)) {
            result.path += " -> Root";
        } else if (isLinkCertificate(chain[i].cert)) {
            result.path += " -> Link";
        } else {
            result.path += " -> CSCA";
        }
    }

    // Cleanup: free CSCAs not in the chain
    for (X509* csca : allCscas) {
        bool inChain = false;
        for (auto& entry : chain) {
            if (entry.cert == csca) { inChain = true; break; }
        }
        if (!inChain) X509_free(csca);
    }

    // Free owned chain entries (CSCAs we got from provider)
    for (auto& entry : chain) {
        if (entry.owned) X509_free(entry.cert);
    }

    return result;
}

} // namespace icao::validation

/**
 * @file trust_chain_builder.h
 * @brief ICAO Doc 9303 Part 12 Trust Chain Builder
 *
 * Builds and validates DSC -> (Link) -> Root CSCA trust chains.
 * Uses ICscaProvider interface for infrastructure abstraction.
 *
 * ICAO hybrid chain model:
 *   - Signature verification: HARD requirement (must pass)
 *   - Certificate expiration: informational only (does not fail validation)
 */

#pragma once

#include <openssl/x509.h>
#include "types.h"
#include "providers.h"

namespace icao::validation {

/**
 * @brief Trust chain builder with ICAO Doc 9303 hybrid chain model
 *
 * Usage:
 * @code
 *   DbCscaProvider provider(&certRepo);
 *   TrustChainBuilder builder(&provider);
 *   TrustChainResult result = builder.build(dscCert);
 * @endcode
 */
class TrustChainBuilder {
public:
    /**
     * @brief Constructor
     * @param cscaProvider CSCA lookup provider (non-owning)
     * @throws std::invalid_argument if cscaProvider is nullptr
     */
    explicit TrustChainBuilder(ICscaProvider* cscaProvider);

    /**
     * @brief Build and validate trust chain from leaf certificate to root CSCA
     *
     * Algorithm:
     * 1. Start with leaf certificate (DSC)
     * 2. Find all CSCAs matching issuer DN
     * 3. Select CSCA by signature verification (key rollover support)
     * 4. If CSCA is Link Certificate, recurse to find root
     * 5. Verify root CSCA self-signature
     * 6. Validate all signatures in chain (HARD requirement)
     * 7. Check expiration (informational per ICAO hybrid model)
     *
     * @param leafCert Leaf certificate (DSC) — non-owning, not freed
     * @param maxDepth Maximum chain depth (default: 10)
     * @return TrustChainResult with validation details
     */
    TrustChainResult build(X509* leafCert, int maxDepth = 10);

private:
    ICscaProvider* cscaProvider_;
};

} // namespace icao::validation

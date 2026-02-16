/**
 * @file crl_checker.h
 * @brief CRL revocation checker (RFC 5280 Section 5.3.1)
 *
 * Uses ICrlProvider interface for infrastructure abstraction.
 * Checks certificate serial number against CRL and extracts revocation reason.
 */

#pragma once

#include <openssl/x509.h>
#include "types.h"
#include "providers.h"

namespace icao::validation {

/**
 * @brief CRL-based certificate revocation checker
 *
 * Usage:
 * @code
 *   DbCrlProvider provider(&crlRepo);
 *   CrlChecker checker(&provider);
 *   CrlCheckResult result = checker.check(dscCert, "KR");
 * @endcode
 */
class CrlChecker {
public:
    /**
     * @brief Constructor
     * @param crlProvider CRL lookup provider (non-owning)
     * @throws std::invalid_argument if crlProvider is nullptr
     */
    explicit CrlChecker(ICrlProvider* crlProvider);

    /**
     * @brief Check certificate revocation status via CRL
     *
     * Algorithm:
     * 1. Fetch CRL for the given country code
     * 2. Check CRL expiration (CRL_EXPIRED if nextUpdate < now)
     * 3. Look up certificate serial number in CRL
     * 4. Extract revocation reason code (RFC 5280 Section 5.3.1)
     *
     * @param cert Certificate to check (non-owning)
     * @param countryCode ISO 3166-1 alpha-2 country code for CRL lookup
     * @return CrlCheckResult with revocation details
     */
    CrlCheckResult check(X509* cert, const std::string& countryCode);

private:
    ICrlProvider* crlProvider_;
};

} // namespace icao::validation

/**
 * @file algorithm_compliance.h
 * @brief ICAO Doc 9303 Part 12 + BSI TR-03110 — Algorithm compliance check
 *
 * Pure function — operates only on X509* certificate, no I/O.
 * Validates signature algorithm, key size, and ECDSA curve against
 * Doc 9303 Part 12 (NIST) and BSI TR-03110 (Brainpool) approved lists.
 */

#pragma once

#include <openssl/x509.h>
#include "types.h"

namespace icao::validation {

/**
 * @brief Validate signature algorithm and key size against ICAO 9303 Part 12 Appendix A
 *
 * Doc 9303 Part 12 approved algorithms:
 *   - SHA-256/384/512 with RSA or ECDSA
 *   - RSA-PSS
 *
 * BSI TR-03110 supported (warning):
 *   - SHA-224 with RSA or ECDSA
 *   - Brainpool curves (brainpoolP256r1, P384r1, P512r1)
 *
 * Deprecated (warning):
 *   - SHA-1 with RSA or ECDSA (ICAO NTWG phasing out)
 *
 * Key size requirements:
 *   - RSA: minimum 2048 bits
 *
 * ECDSA curve requirements:
 *   - Doc 9303: P-256, P-384, P-521 (NIST)
 *   - BSI TR-03110: brainpoolP256r1, P384r1, P512r1 (warning)
 *
 * @param cert X509 certificate to check (non-owning)
 * @return AlgorithmComplianceResult with compliance details
 */
AlgorithmComplianceResult validateAlgorithmCompliance(X509* cert);

} // namespace icao::validation

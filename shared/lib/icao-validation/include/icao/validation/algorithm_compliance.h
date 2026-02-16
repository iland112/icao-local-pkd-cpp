/**
 * @file algorithm_compliance.h
 * @brief ICAO Doc 9303 Part 12 Appendix A — Algorithm compliance check
 *
 * Pure function — operates only on X509* certificate, no I/O.
 * Validates signature algorithm and key size against ICAO approved list.
 */

#pragma once

#include <openssl/x509.h>
#include "types.h"

namespace icao::validation {

/**
 * @brief Validate signature algorithm and key size against ICAO 9303 Part 12 Appendix A
 *
 * Approved algorithms:
 *   - SHA-256/384/512 with RSA or ECDSA
 *   - RSA-PSS
 *
 * Deprecated (warning):
 *   - SHA-1 with RSA or ECDSA
 *
 * Key size requirements:
 *   - RSA: minimum 2048 bits
 *
 * @param cert X509 certificate to check (non-owning)
 * @return AlgorithmComplianceResult with compliance details
 */
AlgorithmComplianceResult validateAlgorithmCompliance(X509* cert);

} // namespace icao::validation

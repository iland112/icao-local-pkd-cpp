/**
 * @file extension_validator.h
 * @brief X.509 extension validation per ICAO Doc 9303 Part 12 Section 4.6
 *
 * Pure function — operates only on X509* certificate, no I/O.
 * Validates Key Usage, Basic Constraints, and unknown critical extensions.
 */

#pragma once

#include <string>
#include <openssl/x509.h>
#include "types.h"

namespace icao::validation {

/**
 * @brief Validate certificate extensions per RFC 5280 and ICAO 9303 Part 12
 *
 * Checks:
 *   - No unknown critical extensions (RFC 5280 Section 4.2)
 *   - DSC: must have digitalSignature key usage (bit 0)
 *   - CSCA: must have keyCertSign key usage (bit 5), should have cRLSign (bit 6)
 *
 * @param cert X509 certificate to validate (non-owning)
 * @param role Certificate role: "DSC", "CSCA", or "MLSC"
 * @return ExtensionValidationResult with warnings list
 */
ExtensionValidationResult validateExtensions(X509* cert, const std::string& role);

} // namespace icao::validation

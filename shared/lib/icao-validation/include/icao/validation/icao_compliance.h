/**
 * @file icao_compliance.h
 * @brief ICAO Doc 9303 compliance checking for X.509 certificates
 *
 * Validates certificates against ICAO Doc 9303 Part 12 requirements:
 * - Key Usage, Signature Algorithm, Key Size, Validity Period, DN Format, Extensions
 *
 * Shared library: used by both PKD Management and PKD Relay services.
 */
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <openssl/x509.h>

namespace icao {
namespace validation {

/**
 * @brief ICAO Doc 9303 compliance check result
 */
struct IcaoComplianceResult {
    bool isCompliant = true;
    std::string complianceLevel = "CONFORMANT"; // CONFORMANT, NON_CONFORMANT, WARNING

    // Per-category compliance
    bool keyUsageCompliant = true;
    bool algorithmCompliant = true;
    bool keySizeCompliant = true;
    bool validityPeriodCompliant = true;
    bool dnFormatCompliant = true;
    bool extensionsCompliant = true;

    // Violation details
    std::vector<std::string> violations;

    // PKD conformance (for DSC_NC)
    std::optional<std::string> pkdConformanceCode;
    std::optional<std::string> pkdConformanceText;

    /** @brief Get pipe-separated violations string */
    std::string violationsString() const;
};

/**
 * @brief Check ICAO Doc 9303 compliance for a certificate
 *
 * @param cert OpenSSL X509 certificate
 * @param certType Certificate type: "CSCA", "DSC", "DSC_NC", "MLSC"
 * @return IcaoComplianceResult with detailed check results
 */
IcaoComplianceResult checkIcaoCompliance(X509* cert, const std::string& certType);

} // namespace validation
} // namespace icao

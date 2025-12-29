/**
 * @file CertificateType.hpp
 * @brief Enum for certificate types in ICAO PKD
 */

#pragma once

#include <string>

namespace certificatevalidation::domain::model {

/**
 * @brief Certificate type in ICAO PKD hierarchy
 *
 * ICAO PKD defines the following certificate types:
 * - CSCA: Country Signing CA (root, self-signed)
 * - DSC: Document Signer Certificate (issued by CSCA)
 * - DSC_NC: Document Signer Certificate Non-Conformant
 * - DS: Document Signer (legacy term)
 */
enum class CertificateType {
    CSCA,       ///< Country Signing CA - Trust anchor
    DSC,        ///< Document Signer Certificate
    DSC_NC,     ///< Document Signer Certificate Non-Conformant
    DS,         ///< Document Signer (legacy)
    UNKNOWN     ///< Unknown certificate type
};

/**
 * @brief Convert CertificateType to display string
 */
inline std::string toString(CertificateType type) {
    switch (type) {
        case CertificateType::CSCA:    return "CSCA";
        case CertificateType::DSC:     return "DSC";
        case CertificateType::DSC_NC:  return "DSC_NC";
        case CertificateType::DS:      return "DS";
        case CertificateType::UNKNOWN: return "Unknown";
        default:                       return "Unknown";
    }
}

/**
 * @brief Get display name for certificate type
 */
inline std::string getDisplayName(CertificateType type) {
    switch (type) {
        case CertificateType::CSCA:    return "Country Signing CA";
        case CertificateType::DSC:     return "Document Signer Certificate";
        case CertificateType::DSC_NC:  return "Document Signer Certificate (Non-Conformant)";
        case CertificateType::DS:      return "Document Signer";
        case CertificateType::UNKNOWN: return "Unknown";
        default:                       return "Unknown";
    }
}

/**
 * @brief Check if certificate type is a CA
 */
inline bool isCA(CertificateType type) {
    return type == CertificateType::CSCA;
}

/**
 * @brief Parse CertificateType from string
 */
inline CertificateType parseCertificateType(const std::string& str) {
    if (str == "CSCA" || str == "csca") return CertificateType::CSCA;
    if (str == "DSC" || str == "dsc") return CertificateType::DSC;
    if (str == "DSC_NC" || str == "dsc_nc") return CertificateType::DSC_NC;
    if (str == "DS" || str == "ds") return CertificateType::DS;
    return CertificateType::UNKNOWN;
}

} // namespace certificatevalidation::domain::model

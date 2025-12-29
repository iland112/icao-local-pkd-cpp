/**
 * @file CertificateType.hpp
 * @brief Enum for certificate types
 */

#pragma once

#include <string>
#include <stdexcept>

namespace fileparsing::domain::model {

/**
 * @brief Types of certificates in PKD
 */
enum class CertificateType {
    CSCA,      // Country Signing CA (self-signed, root)
    DSC,       // Document Signer Certificate
    DSC_NC     // Document Signer Certificate - Non-Conformant
};

/**
 * @brief Convert CertificateType to string
 */
inline std::string toString(CertificateType type) {
    switch (type) {
        case CertificateType::CSCA: return "CSCA";
        case CertificateType::DSC: return "DSC";
        case CertificateType::DSC_NC: return "DSC_NC";
        default: throw std::invalid_argument("Unknown CertificateType");
    }
}

/**
 * @brief Parse string to CertificateType
 */
inline CertificateType parseCertificateType(const std::string& str) {
    if (str == "CSCA") return CertificateType::CSCA;
    if (str == "DSC") return CertificateType::DSC;
    if (str == "DSC_NC") return CertificateType::DSC_NC;
    throw std::invalid_argument("Unknown certificate type: " + str);
}

} // namespace fileparsing::domain::model

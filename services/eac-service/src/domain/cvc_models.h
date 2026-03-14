#pragma once

/**
 * @file cvc_models.h
 * @brief CVC database record models (DB ↔ domain mapping)
 */

#include <string>
#include <vector>

namespace eac::domain {

struct CvcCertificateRecord {
    std::string id;
    std::string uploadId;

    std::string cvcType;       // CVCA, DV_DOMESTIC, DV_FOREIGN, IS
    std::string countryCode;

    std::string car;           // Certification Authority Reference
    std::string chr;           // Certificate Holder Reference

    std::string chatOid;
    std::string chatRole;
    std::string chatPermissions;  // JSON array string

    std::string publicKeyOid;
    std::string publicKeyAlgorithm;

    std::string effectiveDate;
    std::string expirationDate;

    std::string fingerprintSha256;

    bool signatureValid = false;
    std::string validationStatus;  // VALID, INVALID, PENDING, EXPIRED
    std::string validationMessage;

    std::string issuerCvcId;
    std::string sourceType;
    std::string createdAt;
    std::string updatedAt;
};

struct EacTrustChainRecord {
    std::string id;
    std::string isCertificateId;
    std::string dvCertificateId;
    std::string cvcaCertificateId;
    bool chainValid = false;
    std::string chainPath;
    int chainDepth = 0;
    std::string validationMessage;
    std::string validatedAt;
};

} // namespace eac::domain

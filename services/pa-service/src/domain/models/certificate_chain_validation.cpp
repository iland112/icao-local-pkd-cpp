/**
 * @file certificate_chain_validation.cpp
 * @brief Implementation of CertificateChainValidation domain model
 */

#include "certificate_chain_validation.h"
#include <spdlog/spdlog.h>

namespace domain {
namespace models {

Json::Value CertificateChainValidation::toJson() const {
    Json::Value json;

    // Overall validation result
    json["valid"] = valid;
    json["validationStatus"] = getValidationStatus();

    // DSC certificate information
    json["dscSubject"] = dscSubject;
    json["dscSerialNumber"] = dscSerialNumber;
    json["dscIssuer"] = dscIssuer;
    if (dscNotBefore) json["dscNotBefore"] = *dscNotBefore;
    if (dscNotAfter) json["dscNotAfter"] = *dscNotAfter;
    json["dscExpired"] = dscExpired;

    // CSCA certificate information
    json["cscaSubject"] = cscaSubject;
    json["cscaSerialNumber"] = cscaSerialNumber;
    if (cscaNotBefore) json["cscaNotBefore"] = *cscaNotBefore;
    if (cscaNotAfter) json["cscaNotAfter"] = *cscaNotAfter;
    json["cscaExpired"] = cscaExpired;

    // Trust chain
    json["trustChainPath"] = trustChainPath;
    json["trustChainDepth"] = trustChainDepth;

    // Expiration status
    json["validAtSigningTime"] = validAtSigningTime;
    json["expirationStatus"] = expirationStatus;
    if (expirationMessage) json["expirationMessage"] = *expirationMessage;
    if (signingTime) json["signingTime"] = *signingTime;

    // CRL checking
    json["crlChecked"] = crlChecked;
    json["revoked"] = revoked;
    json["crlStatus"] = crlStatusToString(crlStatus);
    if (crlMessage) json["crlMessage"] = *crlMessage;
    if (crlStatusDescription) json["crlStatusDescription"] = *crlStatusDescription;
    if (crlStatusDetailedDescription) json["crlStatusDetailedDescription"] = *crlStatusDetailedDescription;
    json["crlStatusSeverity"] = crlStatusSeverity;
    if (crlThisUpdate) json["crlThisUpdate"] = *crlThisUpdate;
    if (crlNextUpdate) json["crlNextUpdate"] = *crlNextUpdate;
    if (crlRevocationReason) json["crlRevocationReason"] = *crlRevocationReason;

    // Validation errors
    if (validationErrors) json["validationErrors"] = *validationErrors;

    // Signature verification
    json["signatureVerified"] = signatureVerified;
    if (signatureAlgorithm) json["signatureAlgorithm"] = *signatureAlgorithm;

    // DSC conformance status (only include if non-conformant)
    if (dscNonConformant) {
        json["dscNonConformant"] = true;
        json["pkdConformanceCode"] = pkdConformanceCode;
        json["pkdConformanceText"] = pkdConformanceText;
    }

    // Additional status flags
    json["fullyValid"] = isFullyValid();

    return json;
}

CertificateChainValidation CertificateChainValidation::createValid(
    const std::string& dscSubject,
    const std::string& dscSerial,
    const std::string& cscaSubject,
    const std::string& cscaSerial)
{
    CertificateChainValidation result;

    result.valid = true;
    result.dscSubject = dscSubject;
    result.dscSerialNumber = dscSerial;
    result.cscaSubject = cscaSubject;
    result.cscaSerialNumber = cscaSerial;

    result.expirationStatus = "VALID";
    result.validAtSigningTime = true;

    result.signatureVerified = true;
    result.crlStatus = CrlStatus::NOT_CHECKED;
    result.crlStatusSeverity = "INFO";

    return result;
}

CertificateChainValidation CertificateChainValidation::createInvalid(
    const std::string& errorMessage)
{
    CertificateChainValidation result;

    result.valid = false;
    result.validationErrors = errorMessage;
    result.expirationStatus = "INVALID";
    result.crlStatus = CrlStatus::NOT_CHECKED;
    result.crlStatusSeverity = "CRITICAL";

    return result;
}

} // namespace models
} // namespace domain

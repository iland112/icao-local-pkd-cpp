/**
 * @file pa_verification.cpp
 * @brief Implementation of PaVerification domain model
 */

#include "pa_verification.h"
#include <spdlog/spdlog.h>

namespace domain {
namespace models {

Json::Value PaVerification::toJson() const {
    Json::Value json;

    // Primary identification
    json["id"] = id;

    // Document information
    json["documentNumber"] = documentNumber;
    json["countryCode"] = countryCode;

    // Verification status
    json["verificationStatus"] = verificationStatus;

    // SOD information
    json["sodHash"] = sodHash;

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

    // Validation results
    json["certificateChainValid"] = certificateChainValid;
    json["sodSignatureValid"] = sodSignatureValid;
    json["dataGroupsValid"] = dataGroupsValid;

    // CRL checking
    json["crlChecked"] = crlChecked;
    json["revoked"] = revoked;
    json["crlStatus"] = crlStatus;
    if (crlMessage) json["crlMessage"] = *crlMessage;

    // Additional validation details
    if (validationErrors) json["validationErrors"] = *validationErrors;
    json["expirationStatus"] = expirationStatus;
    if (expirationMessage) json["expirationMessage"] = *expirationMessage;

    // Metadata
    if (metadata) json["metadata"] = *metadata;

    // Timestamps
    json["createdAt"] = createdAt;
    if (updatedAt) json["updatedAt"] = *updatedAt;

    // Client information
    if (ipAddress) json["ipAddress"] = *ipAddress;
    if (userAgent) json["userAgent"] = *userAgent;

    return json;
}

PaVerification PaVerification::fromJson(const Json::Value& json) {
    PaVerification pv;

    if (json.isMember("id")) pv.id = json["id"].asString();
    if (json.isMember("documentNumber")) pv.documentNumber = json["documentNumber"].asString();
    if (json.isMember("countryCode")) pv.countryCode = json["countryCode"].asString();
    if (json.isMember("verificationStatus")) pv.verificationStatus = json["verificationStatus"].asString();
    if (json.isMember("sodHash")) pv.sodHash = json["sodHash"].asString();

    if (json.isMember("dscSubject")) pv.dscSubject = json["dscSubject"].asString();
    if (json.isMember("dscSerialNumber")) pv.dscSerialNumber = json["dscSerialNumber"].asString();
    if (json.isMember("dscIssuer")) pv.dscIssuer = json["dscIssuer"].asString();
    if (json.isMember("dscNotBefore")) pv.dscNotBefore = json["dscNotBefore"].asString();
    if (json.isMember("dscNotAfter")) pv.dscNotAfter = json["dscNotAfter"].asString();
    if (json.isMember("dscExpired")) pv.dscExpired = json["dscExpired"].asBool();

    if (json.isMember("cscaSubject")) pv.cscaSubject = json["cscaSubject"].asString();
    if (json.isMember("cscaSerialNumber")) pv.cscaSerialNumber = json["cscaSerialNumber"].asString();
    if (json.isMember("cscaNotBefore")) pv.cscaNotBefore = json["cscaNotBefore"].asString();
    if (json.isMember("cscaNotAfter")) pv.cscaNotAfter = json["cscaNotAfter"].asString();
    if (json.isMember("cscaExpired")) pv.cscaExpired = json["cscaExpired"].asBool();

    if (json.isMember("certificateChainValid")) pv.certificateChainValid = json["certificateChainValid"].asBool();
    if (json.isMember("sodSignatureValid")) pv.sodSignatureValid = json["sodSignatureValid"].asBool();
    if (json.isMember("dataGroupsValid")) pv.dataGroupsValid = json["dataGroupsValid"].asBool();

    if (json.isMember("crlChecked")) pv.crlChecked = json["crlChecked"].asBool();
    if (json.isMember("revoked")) pv.revoked = json["revoked"].asBool();
    if (json.isMember("crlStatus")) pv.crlStatus = json["crlStatus"].asString();
    if (json.isMember("crlMessage")) pv.crlMessage = json["crlMessage"].asString();

    if (json.isMember("validationErrors")) pv.validationErrors = json["validationErrors"].asString();
    if (json.isMember("expirationStatus")) pv.expirationStatus = json["expirationStatus"].asString();
    if (json.isMember("expirationMessage")) pv.expirationMessage = json["expirationMessage"].asString();

    if (json.isMember("metadata")) pv.metadata = json["metadata"];

    if (json.isMember("createdAt")) pv.createdAt = json["createdAt"].asString();
    if (json.isMember("updatedAt")) pv.updatedAt = json["updatedAt"].asString();

    if (json.isMember("ipAddress")) pv.ipAddress = json["ipAddress"].asString();
    if (json.isMember("userAgent")) pv.userAgent = json["userAgent"].asString();

    return pv;
}

bool PaVerification::isValid() const {
    // Required fields check
    if (id.empty()) {
        spdlog::error("PaVerification validation failed: id is empty");
        return false;
    }

    if (documentNumber.empty()) {
        spdlog::error("PaVerification validation failed: documentNumber is empty");
        return false;
    }

    if (countryCode.empty()) {
        spdlog::error("PaVerification validation failed: countryCode is empty");
        return false;
    }

    if (verificationStatus.empty()) {
        spdlog::error("PaVerification validation failed: verificationStatus is empty");
        return false;
    }

    // Verification status must be one of: VALID, INVALID, ERROR
    if (verificationStatus != "VALID" &&
        verificationStatus != "INVALID" &&
        verificationStatus != "ERROR") {
        spdlog::error("PaVerification validation failed: invalid verificationStatus: {}", verificationStatus);
        return false;
    }

    return true;
}

} // namespace models
} // namespace domain

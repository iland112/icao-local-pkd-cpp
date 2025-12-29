/**
 * @file ValidateCertificateResponse.hpp
 * @brief Response DTOs for certificate validation
 */

#pragma once

#include "certificatevalidation/domain/model/CertificateStatus.hpp"
#include "certificatevalidation/domain/model/ValidationError.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace certificatevalidation::application::response {

using namespace certificatevalidation::domain::model;

/**
 * @brief Response for certificate validation
 */
struct ValidateCertificateResponse {
    std::string certificateId;
    std::string status;
    bool valid;
    bool signatureValid;
    bool chainValid;
    bool notRevoked;
    bool validityValid;
    bool constraintsValid;
    long validationDurationMillis;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    static ValidateCertificateResponse success(
        const std::string& certificateId,
        long durationMillis
    ) {
        ValidateCertificateResponse resp;
        resp.certificateId = certificateId;
        resp.status = "VALID";
        resp.valid = true;
        resp.signatureValid = true;
        resp.chainValid = true;
        resp.notRevoked = true;
        resp.validityValid = true;
        resp.constraintsValid = true;
        resp.validationDurationMillis = durationMillis;
        return resp;
    }

    static ValidateCertificateResponse failure(
        const std::string& certificateId,
        CertificateStatus status,
        const std::vector<ValidationError>& validationErrors,
        long durationMillis
    ) {
        ValidateCertificateResponse resp;
        resp.certificateId = certificateId;
        resp.status = toDbString(status);
        resp.valid = false;
        resp.validationDurationMillis = durationMillis;

        for (const auto& error : validationErrors) {
            if (error.isCritical()) {
                resp.errors.push_back(error.toString());
            } else {
                resp.warnings.push_back(error.toString());
            }

            // Set individual validation flags based on error code
            if (error.getErrorCode() == "SIGNATURE_INVALID") {
                resp.signatureValid = false;
            } else if (error.getErrorCode() == "CHAIN_INVALID" ||
                       error.getErrorCode() == "ISSUER_NOT_FOUND") {
                resp.chainValid = false;
            } else if (error.getErrorCode() == "CERTIFICATE_REVOKED") {
                resp.notRevoked = false;
            } else if (error.getErrorCode() == "CERTIFICATE_EXPIRED" ||
                       error.getErrorCode() == "CERTIFICATE_NOT_YET_VALID") {
                resp.validityValid = false;
            } else if (error.getErrorCode() == "BASIC_CONSTRAINTS_INVALID" ||
                       error.getErrorCode() == "KEY_USAGE_INVALID") {
                resp.constraintsValid = false;
            }
        }

        return resp;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["certificateId"] = certificateId;
        j["status"] = status;
        j["valid"] = valid;
        j["signatureValid"] = signatureValid;
        j["chainValid"] = chainValid;
        j["notRevoked"] = notRevoked;
        j["validityValid"] = validityValid;
        j["constraintsValid"] = constraintsValid;
        j["validationDurationMillis"] = validationDurationMillis;
        j["errors"] = errors;
        j["warnings"] = warnings;
        return j;
    }
};

/**
 * @brief Response for revocation check
 */
struct CheckRevocationResponse {
    std::string certificateId;
    bool revoked;
    std::string crlId;
    std::string crlIssuer;
    std::string message;

    static CheckRevocationResponse notRevoked(
        const std::string& certificateId,
        const std::string& crlId = "",
        const std::string& crlIssuer = ""
    ) {
        CheckRevocationResponse resp;
        resp.certificateId = certificateId;
        resp.revoked = false;
        resp.crlId = crlId;
        resp.crlIssuer = crlIssuer;
        resp.message = "Certificate is not revoked";
        return resp;
    }

    static CheckRevocationResponse revoked(
        const std::string& certificateId,
        const std::string& crlId,
        const std::string& crlIssuer
    ) {
        CheckRevocationResponse resp;
        resp.certificateId = certificateId;
        resp.revoked = true;
        resp.crlId = crlId;
        resp.crlIssuer = crlIssuer;
        resp.message = "Certificate has been revoked";
        return resp;
    }

    static CheckRevocationResponse crlNotFound(const std::string& certificateId) {
        CheckRevocationResponse resp;
        resp.certificateId = certificateId;
        resp.revoked = false;
        resp.message = "CRL not available - revocation status unknown";
        return resp;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["certificateId"] = certificateId;
        j["revoked"] = revoked;
        j["crlId"] = crlId;
        j["crlIssuer"] = crlIssuer;
        j["message"] = message;
        return j;
    }
};

/**
 * @brief Response for trust chain verification
 */
struct VerifyTrustChainResponse {
    std::string dscId;
    std::string cscaId;
    bool valid;
    std::string status;
    std::vector<std::string> chain;
    std::string message;

    static VerifyTrustChainResponse success(
        const std::string& dscId,
        const std::string& cscaId,
        const std::vector<std::string>& chain
    ) {
        VerifyTrustChainResponse resp;
        resp.dscId = dscId;
        resp.cscaId = cscaId;
        resp.valid = true;
        resp.status = "VALID";
        resp.chain = chain;
        resp.message = "Trust chain verification successful";
        return resp;
    }

    static VerifyTrustChainResponse failure(
        const std::string& dscId,
        const std::string& message
    ) {
        VerifyTrustChainResponse resp;
        resp.dscId = dscId;
        resp.valid = false;
        resp.status = "INVALID";
        resp.message = message;
        return resp;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["dscId"] = dscId;
        j["cscaId"] = cscaId;
        j["valid"] = valid;
        j["status"] = status;
        j["chain"] = chain;
        j["message"] = message;
        return j;
    }
};

} // namespace certificatevalidation::application::response

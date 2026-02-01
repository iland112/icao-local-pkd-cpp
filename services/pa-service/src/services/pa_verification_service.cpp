/**
 * @file pa_verification_service.cpp
 * @brief Implementation of PaVerificationService
 */

#include "pa_verification_service.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace services {

PaVerificationService::PaVerificationService(
    repositories::PaVerificationRepository* paRepo,
    SodParserService* sodParser,
    CertificateValidationService* certValidator,
    DataGroupParserService* dgParser)
    : paRepo_(paRepo),
      sodParser_(sodParser),
      certValidator_(certValidator),
      dgParser_(dgParser)
{
    if (!paRepo_ || !sodParser_ || !certValidator_ || !dgParser_) {
        throw std::invalid_argument("Service dependencies cannot be null");
    }
    spdlog::debug("PaVerificationService initialized");
}

Json::Value PaVerificationService::verifyPassiveAuthentication(
    const std::vector<uint8_t>& sodData,
    const std::map<std::string, std::vector<uint8_t>>& dataGroups,
    const std::string& documentNumber,
    const std::string& countryCode)
{
    spdlog::info("Starting PA verification for document: {}, country: {}", documentNumber, countryCode);

    Json::Value response;

    try {
        // Step 1: Parse SOD
        domain::models::SodData sod = sodParser_->parseSod(sodData);
        if (!sod.parsingSuccess || !sod.dscCertificate) {
            response["success"] = false;
            response["error"] = "SOD parsing failed: " + sod.parsingErrors.value_or("Unknown error");
            return response;
        }

        // Step 2: Validate certificate chain
        auto certValidation = certValidator_->validateCertificateChain(
            sod.dscCertificate,
            countryCode
        );

        // Step 3: Verify SOD signature
        bool sodSignatureValid = sodParser_->verifySodSignature(sodData, sod.dscCertificate);

        // Step 4: Verify data group hashes
        int totalDgs = dataGroups.size();
        int validDgs = 0;
        Json::Value dgResults = Json::arrayValue;

        for (const auto& [dgNum, dgData] : dataGroups) {
            std::string expectedHash = sod.getDataGroupHash(dgNum);
            if (expectedHash.empty()) continue;

            bool hashValid = dgParser_->verifyDataGroupHash(dgData, expectedHash, sod.hashAlgorithm);
            if (hashValid) validDgs++;

            Json::Value dgResult;
            dgResult["dataGroup"] = dgNum;
            dgResult["valid"] = hashValid;
            dgResults.append(dgResult);
        }

        bool dataGroupsValid = (validDgs == totalDgs);

        // Step 5: Create PA verification record
        domain::models::PaVerification verification;
        verification.documentNumber = documentNumber;
        verification.countryCode = countryCode;
        verification.verificationStatus = (certValidation.valid && sodSignatureValid && dataGroupsValid) ? "VALID" : "INVALID";

        verification.dscSubject = certValidation.dscSubject;
        verification.dscSerialNumber = certValidation.dscSerialNumber;
        verification.dscExpired = certValidation.dscExpired;

        verification.cscaSubject = certValidation.cscaSubject;
        verification.cscaSerialNumber = certValidation.cscaSerialNumber;
        verification.cscaExpired = certValidation.cscaExpired;

        verification.certificateChainValid = certValidation.valid;
        verification.sodSignatureValid = sodSignatureValid;
        verification.dataGroupsValid = dataGroupsValid;

        verification.crlChecked = certValidation.crlChecked;
        verification.revoked = certValidation.revoked;
        verification.crlStatus = domain::models::crlStatusToString(certValidation.crlStatus);

        verification.expirationStatus = certValidation.expirationStatus;

        // Save to database
        std::string verificationId = paRepo_->insert(verification);

        // Build response
        response["success"] = true;
        response["verificationId"] = verificationId;
        response["status"] = verification.verificationStatus;
        response["certificateChain"] = certValidation.toJson();
        response["sodSignature"] = Json::Value();
        response["sodSignature"]["valid"] = sodSignatureValid;
        response["sodSignature"]["algorithm"] = sod.signatureAlgorithm;
        response["dataGroups"] = dgResults;
        response["dataGroups"]["total"] = totalDgs;
        response["dataGroups"]["valid"] = validDgs;

        spdlog::info("PA verification completed: {}", verification.verificationStatus);

    } catch (const std::exception& e) {
        spdlog::error("PA verification failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
    }

    return response;
}

Json::Value PaVerificationService::getVerificationHistory(
    int limit,
    int offset,
    const std::string& status,
    const std::string& countryCode)
{
    spdlog::debug("Getting PA verification history");
    return paRepo_->findAll(limit, offset, status, countryCode);
}

Json::Value PaVerificationService::getVerificationById(const std::string& id) {
    spdlog::debug("Getting PA verification by ID: {}", id);
    return paRepo_->findById(id);
}

Json::Value PaVerificationService::getStatistics() {
    spdlog::debug("Getting PA verification statistics");
    return paRepo_->getStatistics();
}

Json::Value PaVerificationService::getDataGroupsByVerificationId(const std::string& id) {
    // Placeholder - would need to store DG results separately
    Json::Value response;
    response["success"] = false;
    response["message"] = "Data groups retrieval not yet implemented";
    return response;
}

} // namespace services

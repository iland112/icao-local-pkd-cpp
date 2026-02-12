/**
 * @file pa_verification_service.cpp
 * @brief Implementation of PaVerificationService
 */

#include "pa_verification_service.h"
#include <data_group.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace services {

PaVerificationService::PaVerificationService(
    repositories::PaVerificationRepository* paRepo,
    repositories::DataGroupRepository* dgRepo,
    icao::SodParser* sodParser,
    CertificateValidationService* certValidator,
    icao::DgParser* dgParser)
    : paRepo_(paRepo),
      dgRepo_(dgRepo),
      sodParser_(sodParser),
      certValidator_(certValidator),
      dgParser_(dgParser)
{
    if (!paRepo_ || !sodParser_ || !certValidator_ || !dgParser_) {
        throw std::invalid_argument("Service dependencies cannot be null");
    }
    spdlog::debug("PaVerificationService initialized (dgRepo={})", dgRepo_ ? "yes" : "no");
}

Json::Value PaVerificationService::verifyPassiveAuthentication(
    const std::vector<uint8_t>& sodData,
    const std::map<std::string, std::vector<uint8_t>>& dataGroups,
    const std::string& documentNumber,
    const std::string& countryCode)
{
    spdlog::info("Starting PA verification for document: {}, country: {}", documentNumber, countryCode);

    auto startTime = std::chrono::steady_clock::now();
    Json::Value response;

    try {
        // Step 1: Parse SOD
        icao::models::SodData sod = sodParser_->parseSod(sodData);
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
        Json::Value dgResults = Json::objectValue;  // Changed to object

        for (const auto& [dgNum, dgData] : dataGroups) {
            std::string expectedHash = sod.getDataGroupHash(dgNum);
            if (expectedHash.empty()) continue;

            // Compute actual hash
            std::string computedHash = dgParser_->computeHash(dgData, sod.hashAlgorithm);
            bool hashValid = (computedHash == expectedHash);
            if (hashValid) validDgs++;

            // Create DG key in format "DG1", "DG2", etc.
            std::string dgKey = "DG" + dgNum;

            Json::Value dgResult;
            dgResult["valid"] = hashValid;
            dgResult["expectedHash"] = expectedHash;
            dgResult["actualHash"] = computedHash;  // Frontend expects "actualHash"
            dgResults[dgKey] = dgResult;  // Use key-based assignment
        }

        bool dataGroupsValid = (validDgs == totalDgs);

        // Step 5: Create PA verification record
        domain::models::PaVerification verification;
        verification.documentNumber = documentNumber;
        // Use country code extracted from DSC issuer if not provided in request
        verification.countryCode = countryCode.empty() ? certValidation.countryCode : countryCode;
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

        // Save data groups to database for later retrieval
        if (dgRepo_) {
            for (const auto& [dgNum, dgData] : dataGroups) {
                icao::models::DataGroup dg;
                dg.dgNumber = dgNum;
                dg.expectedHash = sod.getDataGroupHash(dgNum);
                dg.actualHash = dgParser_->computeHash(dgData, sod.hashAlgorithm);
                dg.hashValid = (dg.actualHash == dg.expectedHash);
                dg.hashAlgorithm = sod.hashAlgorithm;
                dg.rawData = dgData;
                dg.dataSize = dgData.size();
                dgRepo_->insert(dg, verificationId);
            }
            spdlog::info("Saved {} data groups for verification {}", dataGroups.size(), verificationId);
        }

        // Calculate processing time
        auto endTime = std::chrono::steady_clock::now();
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        // Generate ISO 8601 timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream tsStream;
        tsStream << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");

        // Build response data
        Json::Value data;
        data["verificationId"] = verificationId;
        data["status"] = verification.verificationStatus;
        data["verificationTimestamp"] = tsStream.str();
        data["processingDurationMs"] = static_cast<Json::Int64>(durationMs);
        data["issuingCountry"] = verification.countryCode;
        data["documentNumber"] = documentNumber;
        data["certificateChainValidation"] = certValidation.toJson();
        data["sodSignatureValidation"] = Json::Value();
        data["sodSignatureValidation"]["valid"] = sodSignatureValid;
        data["sodSignatureValidation"]["algorithm"] = sod.signatureAlgorithm;
        data["sodSignatureValidation"]["hashAlgorithm"] = sod.hashAlgorithm;
        data["sodSignatureValidation"]["signatureAlgorithm"] = sod.signatureAlgorithm;
        data["dataGroupValidation"] = Json::Value();
        data["dataGroupValidation"]["details"] = dgResults;
        data["dataGroupValidation"]["totalGroups"] = totalDgs;
        data["dataGroupValidation"]["validGroups"] = validDgs;
        data["dataGroupValidation"]["invalidGroups"] = totalDgs - validDgs;

        // Wrap in ApiResponse format
        response["success"] = true;
        response["data"] = data;

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

} // namespace services

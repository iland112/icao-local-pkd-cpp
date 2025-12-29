#pragma once

#include "passiveauthentication/application/command/PerformPassiveAuthenticationCommand.hpp"
#include "passiveauthentication/application/response/PassiveAuthenticationResponse.hpp"
#include "passiveauthentication/application/response/CertificateChainValidationDto.hpp"
#include "passiveauthentication/application/response/SodSignatureValidationDto.hpp"
#include "passiveauthentication/application/response/DataGroupValidationDto.hpp"
#include "passiveauthentication/domain/model/PassportData.hpp"
#include "passiveauthentication/domain/model/DataGroup.hpp"
#include "passiveauthentication/domain/model/DataGroupHash.hpp"
#include "passiveauthentication/domain/model/SecurityObjectDocument.hpp"
#include "passiveauthentication/domain/model/PassiveAuthenticationError.hpp"
#include "passiveauthentication/domain/model/PassiveAuthenticationStatus.hpp"
#include "passiveauthentication/domain/model/RequestMetadata.hpp"
#include "passiveauthentication/domain/model/CrlCheckResult.hpp"
#include "passiveauthentication/domain/port/SodParserPort.hpp"
#include "passiveauthentication/domain/port/LdapCscaPort.hpp"
#include "passiveauthentication/domain/port/CrlLdapPort.hpp"
#include "passiveauthentication/domain/repository/PassportDataRepository.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <vector>
#include <sstream>
#include <iomanip>
#include <openssl/x509.h>
#include <spdlog/spdlog.h>

namespace pa::application::usecase {

/**
 * Use Case for performing Passive Authentication (PA) verification on ePassport data.
 *
 * This use case orchestrates the complete PA verification process according to
 * ICAO 9303 Part 11 standard, including:
 * 1. Certificate Chain Validation (DSC -> CSCA)
 * 2. SOD Signature Verification
 * 3. Data Group Hash Verification
 */
class PerformPassiveAuthenticationUseCase {
private:
    std::shared_ptr<domain::port::SodParserPort> sodParser_;
    std::shared_ptr<domain::port::LdapCscaPort> ldapCscaPort_;
    std::shared_ptr<domain::port::CrlLdapPort> crlLdapPort_;
    std::shared_ptr<domain::repository::PassportDataRepository> passportDataRepository_;

    /**
     * Retrieve CSCA from LDAP using issuer DN.
     */
    X509* retrieveCscaFromLdap(const std::string& issuerDn) {
        spdlog::debug("Looking up CSCA from LDAP with DN: {}", issuerDn);

        // Try exact match first
        X509* csca = ldapCscaPort_->findBySubjectDn(issuerDn);
        if (csca) {
            spdlog::debug("Found CSCA with exact DN match");
            return csca;
        }

        // Try normalized DN formats
        // TODO: Implement DN normalization for RFC2253/RFC1779 conversion

        throw shared::exception::ApplicationException(
            "CSCA_NOT_FOUND",
            "CSCA not found in LDAP for issuer DN: " + issuerDn +
            ". Ensure CSCA is uploaded to LDAP before performing PA verification."
        );
    }

    /**
     * Extract hex string from serial number.
     */
    std::string getSerialNumberHex(X509* cert) {
        ASN1_INTEGER* serial = X509_get_serialNumber(cert);
        BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
        if (!bn) return "";

        char* hex = BN_bn2hex(bn);
        std::string result(hex);
        OPENSSL_free(hex);
        BN_free(bn);
        return result;
    }

    /**
     * Get subject DN from certificate.
     */
    std::string getSubjectDn(X509* cert) {
        X509_NAME* name = X509_get_subject_name(cert);
        char* dn = X509_NAME_oneline(name, nullptr, 0);
        std::string result(dn);
        OPENSSL_free(dn);
        return result;
    }

    /**
     * Validate certificate chain (DSC -> CSCA).
     */
    response::CertificateChainValidationDto validateCertificateChain(
        X509* dscX509,
        X509* cscaX509,
        const std::string& countryCode,
        std::vector<domain::model::PassiveAuthenticationError>& errors
    ) {
        spdlog::debug("Validating certificate chain: DSC (from SOD) -> CSCA (from LDAP)");

        bool chainValid = false;
        bool crlChecked = false;
        bool revoked = false;
        std::string validationErrors;

        // Extract DSC information
        std::string dscSubjectDn = getSubjectDn(dscX509);
        std::string dscSerialNumber = getSerialNumberHex(dscX509);

        // Extract CSCA information
        std::string cscaSubjectDn = getSubjectDn(cscaX509);
        std::string cscaSerialNumber = getSerialNumberHex(cscaX509);

        // Get DSC validity period
        std::optional<std::chrono::system_clock::time_point> dscNotBefore;
        std::optional<std::chrono::system_clock::time_point> dscNotAfter;

        try {
            // Validate trust chain: DSC signature with CSCA public key
            EVP_PKEY* cscaPubKey = X509_get_pubkey(cscaX509);
            if (cscaPubKey) {
                int result = X509_verify(dscX509, cscaPubKey);
                EVP_PKEY_free(cscaPubKey);

                if (result == 1) {
                    chainValid = true;
                    spdlog::debug("Certificate chain validation passed");
                } else {
                    validationErrors += "Trust chain validation failed; ";
                    errors.push_back(domain::model::PassiveAuthenticationError::critical(
                        "CHAIN_VALIDATION_FAILED",
                        "Certificate chain validation failed"
                    ));
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Certificate chain validation failed: {}", e.what());
            validationErrors += std::string("Trust chain validation failed: ") + e.what() + "; ";
            errors.push_back(domain::model::PassiveAuthenticationError::critical(
                "CHAIN_VALIDATION_FAILED",
                std::string("Certificate chain validation failed: ") + e.what()
            ));
        }

        // CRL Check
        domain::model::CrlCheckResult crlCheckResult = performCrlCheck(dscX509, cscaX509, cscaSubjectDn, countryCode);
        crlChecked = !crlCheckResult.hasCrlVerificationFailed();
        revoked = crlCheckResult.isCertificateRevoked();

        std::string crlStatus = toString(crlCheckResult.getStatus());
        std::string crlStatusDescription = crlCheckResult.getStatusDescription();
        std::string crlStatusSeverity = crlCheckResult.getStatusSeverity();
        std::string crlMessage = buildCrlMessage(crlCheckResult, cscaSubjectDn, countryCode);

        if (crlCheckResult.isCertificateRevoked()) {
            spdlog::warn("DSC certificate is REVOKED");
            validationErrors += "Certificate is revoked; ";
            errors.push_back(domain::model::PassiveAuthenticationError::critical(
                "CERTIFICATE_REVOKED",
                "DSC certificate is revoked: " + crlCheckResult.getRevocationReasonText()
            ));
            chainValid = false;
        }

        return response::CertificateChainValidationDto(
            chainValid,
            dscSubjectDn,
            dscSerialNumber,
            cscaSubjectDn,
            cscaSerialNumber,
            dscNotBefore,
            dscNotAfter,
            crlChecked,
            revoked,
            crlStatus,
            crlStatusDescription,
            "",  // detailedDescription
            crlStatusSeverity,
            crlMessage,
            validationErrors.empty() ? std::nullopt : std::optional<std::string>(validationErrors)
        );
    }

    /**
     * Validate SOD signature.
     */
    response::SodSignatureValidationDto validateSodSignature(
        const domain::model::SecurityObjectDocument& sod,
        X509* dscX509,
        std::vector<domain::model::PassiveAuthenticationError>& errors
    ) {
        spdlog::debug("Validating SOD signature with DSC certificate");

        std::optional<std::string> signatureAlgorithm;
        std::optional<std::string> hashAlgorithm;
        bool signatureValid = false;
        std::string validationErrors;

        try {
            signatureAlgorithm = sodParser_->extractSignatureAlgorithm(sod.getEncodedData());
            hashAlgorithm = sodParser_->extractHashAlgorithm(sod.getEncodedData());

            spdlog::debug("SOD algorithms - Signature: {}, Hash: {}",
                signatureAlgorithm.value_or("unknown"),
                hashAlgorithm.value_or("unknown"));

            signatureValid = sodParser_->verifySignature(sod.getEncodedData(), dscX509);

            if (!signatureValid) {
                validationErrors += "SOD signature verification failed; ";
                errors.push_back(domain::model::PassiveAuthenticationError::critical(
                    "SOD_SIGNATURE_INVALID",
                    "SOD signature verification failed with DSC certificate"
                ));
                spdlog::warn("SOD signature invalid");
            } else {
                spdlog::debug("SOD signature validation passed");
            }
        } catch (const std::exception& e) {
            spdlog::error("SOD signature validation error: {}", e.what());
            validationErrors += std::string("SOD validation error: ") + e.what() + "; ";
            errors.push_back(domain::model::PassiveAuthenticationError::critical(
                "SOD_VALIDATION_ERROR",
                std::string("SOD signature validation error: ") + e.what()
            ));
        }

        return response::SodSignatureValidationDto(
            signatureValid,
            signatureAlgorithm,
            hashAlgorithm,
            validationErrors.empty() ? std::nullopt : std::optional<std::string>(validationErrors)
        );
    }

    /**
     * Validate data group hashes.
     */
    response::DataGroupValidationDto validateDataGroupHashes(
        const std::map<domain::model::DataGroupNumber, std::vector<uint8_t>>& dataGroupsFromCommand,
        const domain::model::SecurityObjectDocument& sod,
        std::vector<domain::model::PassiveAuthenticationError>& errors
    ) {
        spdlog::debug("Validating {} data groups", dataGroupsFromCommand.size());

        std::map<domain::model::DataGroupNumber, domain::model::DataGroupHash> expectedHashes;
        try {
            expectedHashes = sodParser_->parseDataGroupHashes(sod.getEncodedData());
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse data group hashes from SOD: {}", e.what());
            errors.push_back(domain::model::PassiveAuthenticationError::critical(
                "SOD_PARSE_ERROR",
                std::string("Failed to parse data group hashes from SOD: ") + e.what()
            ));
            return response::DataGroupValidationDto(0, 0, 0, {});
        }

        std::map<domain::model::DataGroupNumber, response::DataGroupDetailDto> details;
        int validCount = 0;
        int invalidCount = 0;

        std::string hashAlg = sodParser_->extractHashAlgorithm(sod.getEncodedData());

        for (const auto& [dgNumber, dgContent] : dataGroupsFromCommand) {
            auto it = expectedHashes.find(dgNumber);
            if (it == expectedHashes.end()) {
                spdlog::warn("No expected hash found in SOD for {}", toString(dgNumber));
                errors.push_back(domain::model::PassiveAuthenticationError::warning(
                    "DG_HASH_MISSING",
                    "No expected hash in SOD for " + toString(dgNumber)
                ));
                invalidCount++;
                continue;
            }

            domain::model::DataGroupHash expectedHash = it->second;
            domain::model::DataGroupHash actualHash = domain::model::DataGroupHash::calculate(dgContent, hashAlg);

            bool hashMatches = (expectedHash == actualHash);

            if (hashMatches) {
                validCount++;
                spdlog::debug("{} hash validation passed", toString(dgNumber));
            } else {
                invalidCount++;
                spdlog::warn("{} hash mismatch", toString(dgNumber));
                errors.push_back(domain::model::PassiveAuthenticationError::critical(
                    "DG_HASH_MISMATCH",
                    toString(dgNumber) + " hash mismatch"
                ));
            }

            details[dgNumber] = response::DataGroupDetailDto(
                hashMatches,
                expectedHash.getValue(),
                actualHash.getValue()
            );
        }

        spdlog::info("Data group validation completed - Valid: {}, Invalid: {}", validCount, invalidCount);

        return response::DataGroupValidationDto(
            static_cast<int>(dataGroupsFromCommand.size()),
            validCount,
            invalidCount,
            details
        );
    }

    /**
     * Perform CRL check.
     */
    domain::model::CrlCheckResult performCrlCheck(
        X509* dscX509,
        X509* cscaX509,
        const std::string& cscaSubjectDn,
        const std::string& countryCode
    ) {
        spdlog::debug("Starting CRL check for DSC certificate");

        try {
            X509_CRL* crl = crlLdapPort_->getCrl(cscaSubjectDn, countryCode);
            if (!crl) {
                spdlog::debug("CRL not available for CSCA: {}", cscaSubjectDn);
                return domain::model::CrlCheckResult::unavailable(
                    "CRL not found in LDAP for CSCA: " + cscaSubjectDn
                );
            }

            auto result = crlLdapPort_->checkRevocation(dscX509, crl, cscaX509);
            X509_CRL_free(crl);
            return result;

        } catch (const std::exception& e) {
            spdlog::error("CRL check failed: {}", e.what());
            return domain::model::CrlCheckResult::invalid(
                std::string("CRL verification failed: ") + e.what()
            );
        }
    }

    /**
     * Build user-friendly CRL message.
     */
    std::string buildCrlMessage(
        const domain::model::CrlCheckResult& crlResult,
        const std::string& cscaSubjectDn,
        const std::string& countryCode
    ) {
        switch (crlResult.getStatus()) {
            case domain::model::CrlCheckStatus::VALID:
                return "CRL check passed - DSC certificate is not revoked";
            case domain::model::CrlCheckStatus::REVOKED:
                return "Certificate revoked - Reason: " + crlResult.getRevocationReasonText();
            case domain::model::CrlCheckStatus::CRL_UNAVAILABLE:
                return "CRL not found in LDAP for CSCA (country: " + countryCode + ")";
            case domain::model::CrlCheckStatus::CRL_EXPIRED:
                return "CRL has expired - nextUpdate time has passed";
            case domain::model::CrlCheckStatus::CRL_INVALID:
                return "CRL verification failed";
            case domain::model::CrlCheckStatus::NOT_CHECKED:
                return "CRL verification was not performed";
            default:
                return "Unknown CRL status";
        }
    }

    /**
     * Determine overall verification status.
     */
    domain::model::PassiveAuthenticationStatus determineOverallStatus(
        const response::CertificateChainValidationDto& chainValidation,
        const response::SodSignatureValidationDto& sodValidation,
        const response::DataGroupValidationDto& dgValidation,
        const std::vector<domain::model::PassiveAuthenticationError>& errors
    ) {
        // Check for critical errors
        for (const auto& error : errors) {
            if (error.isCritical()) {
                return domain::model::PassiveAuthenticationStatus::INVALID;
            }
        }

        if (!chainValidation.valid) {
            return domain::model::PassiveAuthenticationStatus::INVALID;
        }

        if (!sodValidation.valid) {
            return domain::model::PassiveAuthenticationStatus::INVALID;
        }

        if (dgValidation.invalidGroups > 0) {
            return domain::model::PassiveAuthenticationStatus::INVALID;
        }

        return domain::model::PassiveAuthenticationStatus::VALID;
    }

public:
    PerformPassiveAuthenticationUseCase(
        std::shared_ptr<domain::port::SodParserPort> sodParser,
        std::shared_ptr<domain::port::LdapCscaPort> ldapCscaPort,
        std::shared_ptr<domain::port::CrlLdapPort> crlLdapPort,
        std::shared_ptr<domain::repository::PassportDataRepository> passportDataRepository
    ) : sodParser_(std::move(sodParser)),
        ldapCscaPort_(std::move(ldapCscaPort)),
        crlLdapPort_(std::move(crlLdapPort)),
        passportDataRepository_(std::move(passportDataRepository)) {}

    /**
     * Execute the Passive Authentication verification process.
     */
    response::PassiveAuthenticationResponse execute(const command::PerformPassiveAuthenticationCommand& cmd) {
        spdlog::info("Starting Passive Authentication for document: {}-{}",
            cmd.getIssuingCountry(), cmd.getDocumentNumber());

        auto startTime = std::chrono::system_clock::now();
        std::vector<domain::model::PassiveAuthenticationError> errors;

        try {
            // Step 1: Extract DSC from SOD
            X509* dscX509 = sodParser_->extractDscCertificate(cmd.getSodBytes());
            if (!dscX509) {
                throw shared::exception::ApplicationException(
                    "DSC_EXTRACTION_FAILED",
                    "Failed to extract DSC certificate from SOD"
                );
            }
            spdlog::debug("Extracted DSC from SOD: {}", getSubjectDn(dscX509));

            // Step 2: Get CSCA issuer DN from DSC
            X509_NAME* issuerName = X509_get_issuer_name(dscX509);
            char* issuerDn = X509_NAME_oneline(issuerName, nullptr, 0);
            std::string cscaDn(issuerDn);
            OPENSSL_free(issuerDn);

            // Step 3: Retrieve CSCA from LDAP
            X509* cscaX509 = retrieveCscaFromLdap(cscaDn);
            spdlog::debug("Retrieved CSCA from LDAP: {}", getSubjectDn(cscaX509));

            // Step 4: Validate Certificate Chain
            auto chainValidation = validateCertificateChain(
                dscX509, cscaX509, cmd.getIssuingCountry(), errors
            );

            // Step 5: Parse SOD and validate signature
            domain::model::SecurityObjectDocument sod = domain::model::SecurityObjectDocument::of(cmd.getSodBytes());
            auto sodValidation = validateSodSignature(sod, dscX509, errors);

            // Step 6: Validate Data Group Hashes
            auto dgValidation = validateDataGroupHashes(cmd.getDataGroups(), sod, errors);

            // Step 7: Create PassportData aggregate and save
            std::vector<domain::model::DataGroup> dataGroupList;
            for (const auto& [dgNum, dgContent] : cmd.getDataGroups()) {
                dataGroupList.push_back(domain::model::DataGroup::of(dgNum, dgContent));
            }

            domain::model::RequestMetadata metadata = domain::model::RequestMetadata::of(
                cmd.getRequestIpAddress(),
                cmd.getRequestUserAgent(),
                cmd.getRequestedBy()
            );

            std::string rawRequestData = "{\"country\":\"" + cmd.getIssuingCountry() +
                "\",\"documentNumber\":\"" + cmd.getDocumentNumber() +
                "\",\"dataGroupCount\":" + std::to_string(cmd.getDataGroups().size()) + "}";

            domain::model::PassportData passportData = domain::model::PassportData::create(
                sod,
                dataGroupList,
                metadata,
                rawRequestData,
                cmd.getIssuingCountry(),
                cmd.getDocumentNumber()
            );

            // Record result
            auto status = determineOverallStatus(chainValidation, sodValidation, dgValidation, errors);
            domain::model::PassiveAuthenticationResult result = domain::model::PassiveAuthenticationResult::withStatistics(
                chainValidation.valid,
                sodValidation.valid,
                dgValidation.totalGroups,
                dgValidation.validGroups,
                errors
            );
            passportData.recordResult(result);

            passportDataRepository_->save(passportData);

            // Calculate duration
            auto endTime = std::chrono::system_clock::now();
            long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

            spdlog::info("Passive Authentication completed with status: {} in {}ms",
                toString(status), durationMs);

            // Cleanup
            X509_free(dscX509);
            X509_free(cscaX509);

            if (status == domain::model::PassiveAuthenticationStatus::VALID) {
                return response::PassiveAuthenticationResponse::valid(
                    passportData.getId().getId(),
                    endTime,
                    cmd.getIssuingCountry(),
                    cmd.getDocumentNumber(),
                    chainValidation,
                    sodValidation,
                    dgValidation,
                    durationMs
                );
            } else {
                return response::PassiveAuthenticationResponse::invalid(
                    passportData.getId().getId(),
                    endTime,
                    cmd.getIssuingCountry(),
                    cmd.getDocumentNumber(),
                    chainValidation,
                    sodValidation,
                    dgValidation,
                    durationMs,
                    errors
                );
            }

        } catch (const std::exception& e) {
            spdlog::error("Passive Authentication failed with exception: {}", e.what());
            auto endTime = std::chrono::system_clock::now();
            long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

            errors.push_back(domain::model::PassiveAuthenticationError::critical(
                "PA_EXECUTION_ERROR",
                std::string("Passive Authentication execution failed: ") + e.what()
            ));

            return response::PassiveAuthenticationResponse::error(
                domain::model::PassportDataId::newId().getId(),
                endTime,
                cmd.getIssuingCountry(),
                cmd.getDocumentNumber(),
                durationMs,
                errors
            );
        }
    }
};

} // namespace pa::application::usecase

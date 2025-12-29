#pragma once

#include "CertificateChainValidationDto.hpp"
#include "SodSignatureValidationDto.hpp"
#include "DataGroupValidationDto.hpp"
#include "passiveauthentication/domain/model/PassiveAuthenticationStatus.hpp"
#include "passiveauthentication/domain/model/PassiveAuthenticationError.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace pa::application::response {

/**
 * Response for Passive Authentication verification.
 */
class PassiveAuthenticationResponse {
private:
    domain::model::PassiveAuthenticationStatus status_;
    std::string verificationId_;
    std::chrono::system_clock::time_point verificationTimestamp_;
    std::string issuingCountry_;
    std::string documentNumber_;
    std::optional<CertificateChainValidationDto> certificateChainValidation_;
    std::optional<SodSignatureValidationDto> sodSignatureValidation_;
    std::optional<DataGroupValidationDto> dataGroupValidation_;
    long processingDurationMs_;
    std::vector<domain::model::PassiveAuthenticationError> errors_;

    PassiveAuthenticationResponse() = default;

public:
    /**
     * Create VALID response.
     */
    static PassiveAuthenticationResponse valid(
        const std::string& verificationId,
        const std::chrono::system_clock::time_point& timestamp,
        const std::string& issuingCountry,
        const std::string& documentNumber,
        const CertificateChainValidationDto& chainValidation,
        const SodSignatureValidationDto& sodValidation,
        const DataGroupValidationDto& dgValidation,
        long durationMs
    ) {
        PassiveAuthenticationResponse response;
        response.status_ = domain::model::PassiveAuthenticationStatus::VALID;
        response.verificationId_ = verificationId;
        response.verificationTimestamp_ = timestamp;
        response.issuingCountry_ = issuingCountry;
        response.documentNumber_ = documentNumber;
        response.certificateChainValidation_ = chainValidation;
        response.sodSignatureValidation_ = sodValidation;
        response.dataGroupValidation_ = dgValidation;
        response.processingDurationMs_ = durationMs;
        return response;
    }

    /**
     * Create INVALID response.
     */
    static PassiveAuthenticationResponse invalid(
        const std::string& verificationId,
        const std::chrono::system_clock::time_point& timestamp,
        const std::string& issuingCountry,
        const std::string& documentNumber,
        const CertificateChainValidationDto& chainValidation,
        const SodSignatureValidationDto& sodValidation,
        const DataGroupValidationDto& dgValidation,
        long durationMs,
        const std::vector<domain::model::PassiveAuthenticationError>& errors
    ) {
        PassiveAuthenticationResponse response;
        response.status_ = domain::model::PassiveAuthenticationStatus::INVALID;
        response.verificationId_ = verificationId;
        response.verificationTimestamp_ = timestamp;
        response.issuingCountry_ = issuingCountry;
        response.documentNumber_ = documentNumber;
        response.certificateChainValidation_ = chainValidation;
        response.sodSignatureValidation_ = sodValidation;
        response.dataGroupValidation_ = dgValidation;
        response.processingDurationMs_ = durationMs;
        response.errors_ = errors;
        return response;
    }

    /**
     * Create ERROR response.
     */
    static PassiveAuthenticationResponse error(
        const std::string& verificationId,
        const std::chrono::system_clock::time_point& timestamp,
        const std::string& issuingCountry,
        const std::string& documentNumber,
        long durationMs,
        const std::vector<domain::model::PassiveAuthenticationError>& errors
    ) {
        PassiveAuthenticationResponse response;
        response.status_ = domain::model::PassiveAuthenticationStatus::ERROR;
        response.verificationId_ = verificationId;
        response.verificationTimestamp_ = timestamp;
        response.issuingCountry_ = issuingCountry;
        response.documentNumber_ = documentNumber;
        response.processingDurationMs_ = durationMs;
        response.errors_ = errors;
        return response;
    }

    domain::model::PassiveAuthenticationStatus getStatus() const { return status_; }
    const std::string& getVerificationId() const { return verificationId_; }
    const std::chrono::system_clock::time_point& getVerificationTimestamp() const { return verificationTimestamp_; }
    const std::string& getIssuingCountry() const { return issuingCountry_; }
    const std::string& getDocumentNumber() const { return documentNumber_; }
    const std::optional<CertificateChainValidationDto>& getCertificateChainValidation() const { return certificateChainValidation_; }
    const std::optional<SodSignatureValidationDto>& getSodSignatureValidation() const { return sodSignatureValidation_; }
    const std::optional<DataGroupValidationDto>& getDataGroupValidation() const { return dataGroupValidation_; }
    long getProcessingDurationMs() const { return processingDurationMs_; }
    const std::vector<domain::model::PassiveAuthenticationError>& getErrors() const { return errors_; }

    bool isValid() const { return status_ == domain::model::PassiveAuthenticationStatus::VALID; }
    bool isInvalid() const { return status_ == domain::model::PassiveAuthenticationStatus::INVALID; }
    bool isError() const { return status_ == domain::model::PassiveAuthenticationStatus::ERROR; }
};

} // namespace pa::application::response

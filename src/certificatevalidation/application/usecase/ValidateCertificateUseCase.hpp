/**
 * @file ValidateCertificateUseCase.hpp
 * @brief Use case for validating a certificate
 */

#pragma once

#include "certificatevalidation/application/command/ValidateCertificateCommand.hpp"
#include "certificatevalidation/application/response/ValidateCertificateResponse.hpp"
#include "certificatevalidation/domain/repository/ICertificateRepository.hpp"
#include "certificatevalidation/domain/repository/ICrlRepository.hpp"
#include "certificatevalidation/domain/port/ICertificateValidationPort.hpp"
#include "certificatevalidation/domain/service/TrustChainValidator.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <spdlog/spdlog.h>

namespace certificatevalidation::application::usecase {

using namespace certificatevalidation::application::command;
using namespace certificatevalidation::application::response;
using namespace certificatevalidation::domain::model;
using namespace certificatevalidation::domain::repository;
using namespace certificatevalidation::domain::port;
using namespace certificatevalidation::domain::service;

/**
 * @brief Use case for validating a certificate
 *
 * Performs full certificate validation including:
 * - Signature verification
 * - Validity period check
 * - Basic Constraints validation
 * - Key Usage validation
 * - Trust chain verification
 * - CRL revocation check
 */
class ValidateCertificateUseCase {
private:
    std::shared_ptr<ICertificateRepository> certificateRepository_;
    std::shared_ptr<ICrlRepository> crlRepository_;
    std::shared_ptr<ICertificateValidationPort> validationPort_;
    std::shared_ptr<TrustChainValidator> trustChainValidator_;

public:
    ValidateCertificateUseCase(
        std::shared_ptr<ICertificateRepository> certificateRepository,
        std::shared_ptr<ICrlRepository> crlRepository,
        std::shared_ptr<ICertificateValidationPort> validationPort,
        std::shared_ptr<TrustChainValidator> trustChainValidator
    ) : certificateRepository_(std::move(certificateRepository)),
        crlRepository_(std::move(crlRepository)),
        validationPort_(std::move(validationPort)),
        trustChainValidator_(std::move(trustChainValidator)) {}

    /**
     * @brief Execute certificate validation
     */
    ValidateCertificateResponse execute(const ValidateCertificateCommand& command) {
        spdlog::info("ValidateCertificateUseCase: certificateId={}",
            command.certificateId);

        auto startTime = std::chrono::steady_clock::now();

        // 1. Find certificate
        auto certOpt = certificateRepository_->findById(
            CertificateId::of(command.certificateId)
        );

        if (!certOpt) {
            throw shared::exception::ApplicationException(
                "CERTIFICATE_NOT_FOUND",
                "Certificate not found: " + command.certificateId
            );
        }

        Certificate& certificate = *certOpt;

        // 2. Find trust anchor if specified
        std::optional<Certificate> trustAnchor;
        if (command.trustAnchorId) {
            auto trustAnchorOpt = certificateRepository_->findById(
                CertificateId::of(*command.trustAnchorId)
            );
            if (trustAnchorOpt) {
                trustAnchor = *trustAnchorOpt;
            }
        }

        // 3. Perform full validation
        auto errors = validationPort_->performFullValidation(
            certificate,
            trustAnchor,
            command.checkRevocation
        );

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime
        ).count();

        // 4. Determine overall status
        CertificateStatus status = CertificateStatus::VALID;
        if (!errors.empty()) {
            for (const auto& error : errors) {
                if (error.getErrorCode() == "CERTIFICATE_EXPIRED") {
                    status = CertificateStatus::EXPIRED;
                    break;
                } else if (error.getErrorCode() == "CERTIFICATE_REVOKED") {
                    status = CertificateStatus::REVOKED;
                    break;
                } else if (error.getErrorCode() == "CERTIFICATE_NOT_YET_VALID") {
                    status = CertificateStatus::NOT_YET_VALID;
                    break;
                } else if (error.isCritical()) {
                    status = CertificateStatus::INVALID;
                }
            }
        }

        // 5. Create validation result and record it
        ValidationResult validationResult = ValidationResult::of(
            status,
            !hasError(errors, "SIGNATURE_INVALID"),
            !hasError(errors, "CHAIN_INVALID") && !hasError(errors, "ISSUER_NOT_FOUND"),
            !hasError(errors, "CERTIFICATE_REVOKED"),
            !hasError(errors, "CERTIFICATE_EXPIRED") && !hasError(errors, "CERTIFICATE_NOT_YET_VALID"),
            !hasError(errors, "BASIC_CONSTRAINTS_INVALID") && !hasError(errors, "KEY_USAGE_INVALID"),
            duration
        );

        certificate.recordValidation(validationResult);
        for (const auto& error : errors) {
            certificate.addValidationError(error);
        }

        // 6. Save updated certificate
        certificateRepository_->save(certificate);

        // 7. Return response
        if (errors.empty()) {
            return ValidateCertificateResponse::success(command.certificateId, duration);
        } else {
            return ValidateCertificateResponse::failure(
                command.certificateId, status, errors, duration
            );
        }
    }

private:
    bool hasError(
        const std::vector<ValidationError>& errors,
        const std::string& errorCode
    ) {
        for (const auto& error : errors) {
            if (error.getErrorCode() == errorCode) {
                return true;
            }
        }
        return false;
    }
};

} // namespace certificatevalidation::application::usecase

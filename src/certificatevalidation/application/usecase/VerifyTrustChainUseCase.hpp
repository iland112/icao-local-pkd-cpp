/**
 * @file VerifyTrustChainUseCase.hpp
 * @brief Use case for verifying trust chain
 */

#pragma once

#include "certificatevalidation/application/command/ValidateCertificateCommand.hpp"
#include "certificatevalidation/application/response/ValidateCertificateResponse.hpp"
#include "certificatevalidation/domain/repository/ICertificateRepository.hpp"
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
 * @brief Use case for verifying trust chain (DSC -> CSCA)
 *
 * Verifies that a DSC certificate chains up to a valid CSCA.
 */
class VerifyTrustChainUseCase {
private:
    std::shared_ptr<ICertificateRepository> certificateRepository_;
    std::shared_ptr<ICertificateValidationPort> validationPort_;
    std::shared_ptr<TrustChainValidator> trustChainValidator_;

public:
    VerifyTrustChainUseCase(
        std::shared_ptr<ICertificateRepository> certificateRepository,
        std::shared_ptr<ICertificateValidationPort> validationPort,
        std::shared_ptr<TrustChainValidator> trustChainValidator
    ) : certificateRepository_(std::move(certificateRepository)),
        validationPort_(std::move(validationPort)),
        trustChainValidator_(std::move(trustChainValidator)) {}

    /**
     * @brief Execute trust chain verification
     */
    VerifyTrustChainResponse execute(const VerifyTrustChainCommand& command) {
        spdlog::info("VerifyTrustChainUseCase: dscId={}", command.dscId);

        // 1. Find DSC certificate
        auto dscOpt = certificateRepository_->findById(CertificateId::of(command.dscId));
        if (!dscOpt) {
            throw shared::exception::ApplicationException(
                "DSC_NOT_FOUND",
                "DSC certificate not found: " + command.dscId
            );
        }

        const Certificate& dsc = *dscOpt;

        // Verify DSC type
        if (!dsc.isDsc()) {
            throw shared::exception::ApplicationException(
                "INVALID_CERTIFICATE_TYPE",
                "Certificate is not a DSC: " + command.dscId
            );
        }

        // 2. Find CSCA certificate
        Certificate csca;
        if (command.cscaId) {
            // Use specified CSCA
            auto cscaOpt = certificateRepository_->findById(
                CertificateId::of(*command.cscaId)
            );
            if (!cscaOpt) {
                throw shared::exception::ApplicationException(
                    "CSCA_NOT_FOUND",
                    "CSCA certificate not found: " + *command.cscaId
                );
            }
            csca = *cscaOpt;
        } else {
            // Find CSCA by issuer DN
            std::string issuerDn = dsc.getIssuerInfo().getDistinguishedName();
            auto cscaOpt = certificateRepository_->findBySubjectDn(issuerDn);
            if (!cscaOpt) {
                return VerifyTrustChainResponse::failure(
                    command.dscId,
                    "CSCA not found for issuer: " + issuerDn
                );
            }
            csca = *cscaOpt;
        }

        // Verify CSCA type
        if (!csca.isCsca()) {
            throw shared::exception::ApplicationException(
                "INVALID_CERTIFICATE_TYPE",
                "Certificate is not a CSCA: " + csca.getId().getValue()
            );
        }

        // 3. Validate trust chain
        try {
            validationPort_->validateTrustChain(dsc, csca);
        } catch (const std::exception& e) {
            spdlog::error("Trust chain validation failed: {}", e.what());
            return VerifyTrustChainResponse::failure(command.dscId, e.what());
        }

        // 4. Validate CSCA
        auto cscaResult = trustChainValidator_->validateCsca(csca);
        if (!cscaResult.isValid()) {
            return VerifyTrustChainResponse::failure(
                command.dscId,
                "CSCA validation failed: " + cscaResult.getSummary()
            );
        }

        // 5. Validate DSC with CSCA
        auto dscResult = trustChainValidator_->validateDsc(dsc, csca);
        if (!dscResult.isValid()) {
            return VerifyTrustChainResponse::failure(
                command.dscId,
                "DSC validation failed: " + dscResult.getSummary()
            );
        }

        // 6. Build chain for response
        std::vector<std::string> chain;
        chain.push_back(dsc.getId().getValue());
        chain.push_back(csca.getId().getValue());

        spdlog::info("Trust chain verification successful: DSC={} -> CSCA={}",
            dsc.getId().getValue(), csca.getId().getValue());

        return VerifyTrustChainResponse::success(
            command.dscId,
            csca.getId().getValue(),
            chain
        );
    }
};

} // namespace certificatevalidation::application::usecase

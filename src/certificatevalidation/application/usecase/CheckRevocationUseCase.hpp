/**
 * @file CheckRevocationUseCase.hpp
 * @brief Use case for checking certificate revocation status
 */

#pragma once

#include "certificatevalidation/application/command/ValidateCertificateCommand.hpp"
#include "certificatevalidation/application/response/ValidateCertificateResponse.hpp"
#include "certificatevalidation/domain/repository/ICertificateRepository.hpp"
#include "certificatevalidation/domain/repository/ICrlRepository.hpp"
#include "certificatevalidation/domain/model/IssuerName.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <regex>
#include <spdlog/spdlog.h>

namespace certificatevalidation::application::usecase {

using namespace certificatevalidation::application::command;
using namespace certificatevalidation::application::response;
using namespace certificatevalidation::domain::model;
using namespace certificatevalidation::domain::repository;

/**
 * @brief Use case for checking certificate revocation status
 *
 * Checks if a certificate has been revoked by looking up
 * the corresponding CRL in the database.
 */
class CheckRevocationUseCase {
private:
    std::shared_ptr<ICertificateRepository> certificateRepository_;
    std::shared_ptr<ICrlRepository> crlRepository_;

    std::string extractIssuerName(const std::string& issuerDn) {
        std::regex pattern("CN=([^,]+)");
        std::smatch match;
        if (std::regex_search(issuerDn, match, pattern)) {
            return match[1].str();
        }
        return "";
    }

    std::string extractCountryCode(const std::string& issuerDn) {
        std::regex pattern("C=([A-Z]{2})");
        std::smatch match;
        if (std::regex_search(issuerDn, match, pattern)) {
            return match[1].str();
        }
        return "";
    }

public:
    CheckRevocationUseCase(
        std::shared_ptr<ICertificateRepository> certificateRepository,
        std::shared_ptr<ICrlRepository> crlRepository
    ) : certificateRepository_(std::move(certificateRepository)),
        crlRepository_(std::move(crlRepository)) {}

    /**
     * @brief Execute revocation check
     */
    CheckRevocationResponse execute(const CheckRevocationCommand& command) {
        spdlog::info("CheckRevocationUseCase: certificateId={}",
            command.certificateId);

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

        const Certificate& certificate = *certOpt;

        // 2. Extract issuer information
        std::string issuerDn = certificate.getIssuerInfo().getDistinguishedName();
        std::string issuerName = extractIssuerName(issuerDn);
        std::string countryCode = extractCountryCode(issuerDn);

        if (issuerName.empty() || countryCode.empty()) {
            spdlog::warn("Could not extract issuer info: issuerDn={}", issuerDn);
            return CheckRevocationResponse::crlNotFound(command.certificateId);
        }

        // 3. Find CRL
        auto crlOpt = crlRepository_->findByIssuerNameAndCountry(issuerName, countryCode);

        if (!crlOpt) {
            spdlog::warn("CRL not found for issuer={}, country={}",
                issuerName, countryCode);
            return CheckRevocationResponse::crlNotFound(command.certificateId);
        }

        const auto& crl = *crlOpt;

        // 4. Check CRL validity
        if (!crl.isValid()) {
            spdlog::warn("CRL is not valid (expired or not yet valid)");
            return CheckRevocationResponse::crlNotFound(command.certificateId);
        }

        // 5. Check revocation status
        std::string serialNumber = certificate.getX509Data().getSerialNumber();
        bool isRevoked = crl.isRevoked(serialNumber);

        if (isRevoked) {
            spdlog::info("Certificate is revoked: serialNumber={}", serialNumber);
            return CheckRevocationResponse::revoked(
                command.certificateId,
                crl.getId().getValue(),
                issuerName
            );
        }

        spdlog::info("Certificate is not revoked: serialNumber={}", serialNumber);
        return CheckRevocationResponse::notRevoked(
            command.certificateId,
            crl.getId().getValue(),
            issuerName
        );
    }
};

} // namespace certificatevalidation::application::usecase

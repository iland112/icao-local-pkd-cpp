/**
 * @file CrlChecker.hpp
 * @brief CRL Revocation Checker Domain Service
 */

#pragma once

#include "certificatevalidation/domain/model/Certificate.hpp"
#include "certificatevalidation/domain/model/CertificateRevocationList.hpp"
#include "certificatevalidation/domain/repository/ICrlRepository.hpp"
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>

namespace certificatevalidation::domain::service {

using namespace certificatevalidation::domain::model;
using namespace certificatevalidation::domain::repository;

/**
 * @brief CRL-based revocation checking service
 *
 * Checks certificate revocation status against available CRLs.
 */
class CrlChecker {
private:
    std::shared_ptr<ICrlRepository> crlRepository_;

public:
    explicit CrlChecker(std::shared_ptr<ICrlRepository> crlRepository)
        : crlRepository_(std::move(crlRepository)) {}

    /**
     * @brief Check if certificate is revoked
     * @param certificate Certificate to check
     * @return true if NOT revoked (valid), false if revoked
     */
    bool checkRevocationStatus(const Certificate& certificate) {
        spdlog::debug("Checking revocation status for certificate: {}",
            certificate.getId().getValue());

        // Get issuer info to find CRL
        std::string issuerDn = certificate.getIssuerInfo().getDistinguishedName();
        std::string countryCode = certificate.getSubjectInfo().getCountryCode();

        // Find CRL for this issuer
        auto crlOpt = crlRepository_->findByIssuerNameAndCountry(issuerDn, countryCode);
        if (!crlOpt) {
            spdlog::warn("No CRL found for issuer: {}, country: {}", issuerDn, countryCode);
            return true;  // Assume not revoked if no CRL available
        }

        const auto& crl = *crlOpt;

        // Check CRL validity
        if (!crl.isValid()) {
            spdlog::warn("CRL is not valid (expired or not yet valid)");
            return true;  // Assume not revoked if CRL is invalid
        }

        // Check if certificate is in revoked list
        std::string serialNumber = certificate.getX509Data().getSerialNumber();
        bool isRevoked = crl.isRevoked(serialNumber);

        if (isRevoked) {
            spdlog::error("Certificate is REVOKED: serialNumber={}", serialNumber);
            return false;
        }

        spdlog::debug("Certificate is not revoked");
        return true;
    }

    /**
     * @brief Check revocation with specific CRL
     * @param certificate Certificate to check
     * @param crl CRL to check against
     * @return true if NOT revoked (valid), false if revoked
     */
    bool checkRevocationStatus(
        const Certificate& certificate,
        const CertificateRevocationList& crl
    ) {
        if (!crl.isValid()) {
            spdlog::warn("CRL is not valid");
            return true;
        }

        std::string serialNumber = certificate.getX509Data().getSerialNumber();
        return !crl.isRevoked(serialNumber);
    }

    /**
     * @brief Get CRL for a certificate's issuer
     */
    std::optional<CertificateRevocationList> getCrlForCertificate(
        const Certificate& certificate
    ) {
        std::string issuerDn = certificate.getIssuerInfo().getDistinguishedName();
        std::string countryCode = certificate.getSubjectInfo().getCountryCode();

        return crlRepository_->findByIssuerNameAndCountry(issuerDn, countryCode);
    }
};

} // namespace certificatevalidation::domain::service

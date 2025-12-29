/**
 * @file TrustChainValidator.hpp
 * @brief Domain Service for Trust Chain validation
 */

#pragma once

#include "certificatevalidation/domain/model/Certificate.hpp"
#include "certificatevalidation/domain/model/CertificateRevocationList.hpp"
#include "certificatevalidation/domain/model/ValidationResult.hpp"
#include "certificatevalidation/domain/repository/ICertificateRepository.hpp"
#include "certificatevalidation/domain/repository/ICrlRepository.hpp"
#include "certificatevalidation/domain/port/ICertificateValidationPort.hpp"
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>

namespace certificatevalidation::domain::service {

using namespace certificatevalidation::domain::model;
using namespace certificatevalidation::domain::repository;
using namespace certificatevalidation::domain::port;

/**
 * @brief Trust Chain Validator Domain Service
 *
 * Validates ICAO PKD certificate hierarchy:
 * - CSCA (Root): Self-signed, CA flag, keyCertSign/cRLSign
 * - DSC (Intermediate): Issued by CSCA, digitalSignature
 *
 * Validation Algorithm:
 * 1. Validate CSCA (self-signed, CA flag, signature)
 * 2. Validate DSC (issuer, signature, validity, CRL)
 * 3. Return overall validation result
 */
class TrustChainValidator {
private:
    std::shared_ptr<ICertificateRepository> certificateRepository_;
    std::shared_ptr<ICrlRepository> crlRepository_;
    std::shared_ptr<ICertificateValidationPort> validationPort_;

public:
    TrustChainValidator(
        std::shared_ptr<ICertificateRepository> certificateRepository,
        std::shared_ptr<ICrlRepository> crlRepository,
        std::shared_ptr<ICertificateValidationPort> validationPort
    ) : certificateRepository_(std::move(certificateRepository)),
        crlRepository_(std::move(crlRepository)),
        validationPort_(std::move(validationPort)) {}

    /**
     * @brief Validate a single certificate
     */
    ValidationResult validateSingle(const Certificate& certificate) {
        spdlog::debug("Validating single certificate: {}",
            certificate.getSubjectInfo().getCommonNameOrDefault());

        auto startTime = std::chrono::steady_clock::now();

        bool validityValid = certificate.isCurrentlyValid();
        bool constraintsValid = true;

        if (!validityValid) {
            spdlog::warn("Certificate validity period check failed");
        }

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        CertificateStatus status = validityValid && constraintsValid
            ? CertificateStatus::VALID
            : (certificate.isExpired() ? CertificateStatus::EXPIRED : CertificateStatus::INVALID);

        return ValidationResult::of(
            status,
            true,   // signatureValid (not checked in single validation)
            false,  // chainValid (no chain validation)
            true,   // notRevoked (not checked)
            validityValid,
            constraintsValid,
            duration
        );
    }

    /**
     * @brief Validate CSCA certificate
     */
    ValidationResult validateCsca(const Certificate& csca) {
        spdlog::debug("=== CSCA Validation Started ===");
        spdlog::debug("CSCA Subject: {}", csca.getSubjectInfo().getDistinguishedName());

        auto startTime = std::chrono::steady_clock::now();

        // 1. Self-Signed Check
        if (!csca.isSelfSigned()) {
            spdlog::error("CSCA is not self-signed");
            return ValidationResult::of(
                CertificateStatus::INVALID,
                false, false, true, true, false,
                getDuration(startTime)
            );
        }

        // 2. CA Flag Check
        if (!csca.isCA()) {
            spdlog::error("CSCA does not have CA flag");
            return ValidationResult::of(
                CertificateStatus::INVALID,
                true, false, true, true, false,
                getDuration(startTime)
            );
        }

        // 3. Validity Period Check
        bool validityValid = csca.isCurrentlyValid();
        if (!validityValid) {
            spdlog::warn("CSCA validity period check failed");
        }

        // 4. Signature Self-Verification
        bool signatureValid = validationPort_->validateSignature(csca, std::nullopt);
        if (!signatureValid) {
            spdlog::error("CSCA self-signature verification failed");
        }

        CertificateStatus status = signatureValid && validityValid
            ? CertificateStatus::VALID
            : (csca.isExpired() ? CertificateStatus::EXPIRED : CertificateStatus::INVALID);

        return ValidationResult::of(
            status,
            signatureValid,
            true,   // chainValid (self-signed = root)
            true,   // notRevoked (CSCA cannot be revoked)
            validityValid,
            true,   // constraintsValid (CA flag checked)
            getDuration(startTime)
        );
    }

    /**
     * @brief Validate DSC certificate with CSCA
     */
    ValidationResult validateDsc(const Certificate& dsc, const Certificate& csca) {
        spdlog::debug("=== DSC Validation Started ===");
        spdlog::debug("DSC Subject: {}", dsc.getSubjectInfo().getDistinguishedName());
        spdlog::debug("CSCA Subject: {}", csca.getSubjectInfo().getDistinguishedName());

        auto startTime = std::chrono::steady_clock::now();

        // 1. Issuer Check
        std::string dscIssuerDn = dsc.getIssuerInfo().getDistinguishedName();
        std::string cscaSubjectDn = csca.getSubjectInfo().getDistinguishedName();

        if (dscIssuerDn != cscaSubjectDn) {
            spdlog::error("DSC Issuer DN does not match CSCA Subject DN");
            return ValidationResult::of(
                CertificateStatus::INVALID,
                false, false, true, true, false,
                getDuration(startTime)
            );
        }

        // 2. Signature Verification
        bool signatureValid = validationPort_->validateSignature(dsc, csca);
        if (!signatureValid) {
            spdlog::error("DSC signature verification failed using CSCA public key");
        }

        // 3. Validity Period Check
        bool validityValid = dsc.isCurrentlyValid();
        if (!validityValid) {
            spdlog::warn("DSC validity period check failed");
        }

        // 4. CRL Check (Revocation)
        bool notRevoked = validationPort_->checkRevocation(dsc);
        if (!notRevoked) {
            spdlog::error("DSC is revoked according to CRL");
        }

        CertificateStatus status = signatureValid && validityValid && notRevoked
            ? CertificateStatus::VALID
            : (dsc.isExpired() ? CertificateStatus::EXPIRED
                : (!notRevoked ? CertificateStatus::REVOKED : CertificateStatus::INVALID));

        return ValidationResult::of(
            status,
            signatureValid,
            true,   // chainValid (issuer relationship verified)
            notRevoked,
            validityValid,
            true,   // constraintsValid
            getDuration(startTime)
        );
    }

    /**
     * @brief Validate issuer relationship between two certificates
     */
    ValidationResult validateIssuerRelationship(
        const Certificate& child,
        const Certificate& parent
    ) {
        spdlog::debug("=== Issuer Relationship Validation ===");
        spdlog::debug("Child: {}", child.getSubjectInfo().getCommonNameOrDefault());
        spdlog::debug("Parent: {}", parent.getSubjectInfo().getCommonNameOrDefault());

        auto startTime = std::chrono::steady_clock::now();

        // 1. Issuer DN Check
        std::string childIssuerDn = child.getIssuerInfo().getDistinguishedName();
        std::string parentSubjectDn = parent.getSubjectInfo().getDistinguishedName();

        bool chainValid = (childIssuerDn == parentSubjectDn);
        if (!chainValid) {
            spdlog::error("Issuer DN mismatch");
        }

        // 2. Signature Verification
        bool signatureValid = validationPort_->validateSignature(child, parent);
        if (!signatureValid) {
            spdlog::error("Signature verification failed: child signed by parent");
        }

        CertificateStatus status = signatureValid && chainValid
            ? CertificateStatus::VALID
            : CertificateStatus::INVALID;

        return ValidationResult::of(
            status,
            signatureValid,
            chainValid,
            true,   // notRevoked (not checked)
            true,   // validityValid (not checked)
            true,   // constraintsValid (not checked)
            getDuration(startTime)
        );
    }

private:
    long getDuration(std::chrono::steady_clock::time_point startTime) {
        auto endTime = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    }
};

} // namespace certificatevalidation::domain::service

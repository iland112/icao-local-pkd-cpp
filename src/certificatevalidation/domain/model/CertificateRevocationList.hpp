/**
 * @file CertificateRevocationList.hpp
 * @brief CRL Aggregate Root
 */

#pragma once

#include "shared/domain/AggregateRoot.hpp"
#include "shared/exception/DomainException.hpp"
#include "CrlId.hpp"
#include "IssuerName.hpp"
#include "CountryCode.hpp"
#include "ValidityPeriod.hpp"
#include "X509CrlData.hpp"
#include "RevokedCertificates.hpp"
#include <string>
#include <chrono>
#include <optional>

namespace certificatevalidation::domain::model {

/**
 * @brief CertificateRevocationList Aggregate Root
 *
 * X.509 CRL (Certificate Revocation List) aggregate root that manages
 * revocation information for a specific CSCA.
 *
 * Lifecycle:
 * 1. LDIF parsing extracts CRL from cRLDistributionPoint entry
 * 2. CertificateRevocationList.create() creates aggregate
 * 3. Repository.save() persists to database
 * 4. Certificate.checkRevocation() queries revocation status
 */
class CertificateRevocationList : public shared::domain::AggregateRoot<CrlId> {
private:
    CrlId id_;
    std::string uploadId_;  // Cross-context reference to FileUpload
    IssuerName issuerName_;
    CountryCode countryCode_;
    std::optional<std::string> crlNumber_;
    ValidityPeriod validityPeriod_;
    X509CrlData x509CrlData_;
    RevokedCertificates revokedCertificates_;
    bool isValidCrl_;
    std::chrono::system_clock::time_point createdAt_;
    std::chrono::system_clock::time_point updatedAt_;

    CertificateRevocationList(
        CrlId id,
        std::string uploadId,
        IssuerName issuerName,
        CountryCode countryCode,
        ValidityPeriod validityPeriod,
        X509CrlData x509CrlData,
        RevokedCertificates revokedCertificates
    ) : id_(std::move(id)),
        uploadId_(std::move(uploadId)),
        issuerName_(std::move(issuerName)),
        countryCode_(std::move(countryCode)),
        validityPeriod_(std::move(validityPeriod)),
        x509CrlData_(std::move(x509CrlData)),
        revokedCertificates_(std::move(revokedCertificates)),
        isValidCrl_(true),
        createdAt_(std::chrono::system_clock::now()),
        updatedAt_(std::chrono::system_clock::now()) {}

public:
    /**
     * @brief Create CertificateRevocationList
     */
    static CertificateRevocationList create(
        const std::string& uploadId,
        const CrlId& id,
        IssuerName issuerName,
        CountryCode countryCode,
        ValidityPeriod validityPeriod,
        X509CrlData x509CrlData,
        RevokedCertificates revokedCertificates
    ) {
        // Validate issuer name and country code match
        if (!issuerName.isCountry(countryCode.getValue())) {
            throw shared::exception::DomainException(
                "ISSUER_COUNTRY_MISMATCH",
                "Issuer country (" + issuerName.getCountryCode() +
                ") does not match Country code (" + countryCode.getValue() + ")"
            );
        }

        return CertificateRevocationList(
            id,
            uploadId,
            std::move(issuerName),
            std::move(countryCode),
            std::move(validityPeriod),
            std::move(x509CrlData),
            std::move(revokedCertificates)
        );
    }

    /**
     * @brief Reconstruct from database
     */
    static CertificateRevocationList reconstitute(
        const CrlId& id,
        const std::string& uploadId,
        IssuerName issuerName,
        CountryCode countryCode,
        const std::optional<std::string>& crlNumber,
        ValidityPeriod validityPeriod,
        X509CrlData x509CrlData,
        RevokedCertificates revokedCertificates,
        bool isValidCrl,
        std::chrono::system_clock::time_point createdAt
    ) {
        CertificateRevocationList crl(
            id, uploadId, std::move(issuerName), std::move(countryCode),
            std::move(validityPeriod), std::move(x509CrlData), std::move(revokedCertificates)
        );
        crl.crlNumber_ = crlNumber;
        crl.isValidCrl_ = isValidCrl;
        crl.createdAt_ = createdAt;
        return crl;
    }

    // ========== Getters ==========
    [[nodiscard]] const CrlId& getId() const noexcept override { return id_; }
    [[nodiscard]] const std::string& getUploadId() const noexcept { return uploadId_; }
    [[nodiscard]] const IssuerName& getIssuerName() const noexcept { return issuerName_; }
    [[nodiscard]] const CountryCode& getCountryCode() const noexcept { return countryCode_; }
    [[nodiscard]] const std::optional<std::string>& getCrlNumber() const noexcept { return crlNumber_; }
    [[nodiscard]] const ValidityPeriod& getValidityPeriod() const noexcept { return validityPeriod_; }
    [[nodiscard]] const X509CrlData& getX509CrlData() const noexcept { return x509CrlData_; }
    [[nodiscard]] const RevokedCertificates& getRevokedCertificates() const noexcept { return revokedCertificates_; }
    [[nodiscard]] bool getIsValidCrl() const noexcept { return isValidCrl_; }
    [[nodiscard]] std::chrono::system_clock::time_point getCreatedAt() const noexcept { return createdAt_; }

    // ========== Business Logic ==========

    /**
     * @brief Check if a certificate serial number is revoked
     */
    [[nodiscard]] bool isRevoked(const std::string& serialNumber) const {
        if (serialNumber.empty()) {
            throw shared::exception::DomainException(
                "INVALID_SERIAL_NUMBER",
                "Serial number cannot be null or blank"
            );
        }
        return revokedCertificates_.contains(serialNumber);
    }

    /**
     * @brief Check if CRL has expired
     */
    [[nodiscard]] bool isExpired() const noexcept {
        return validityPeriod_.isExpired();
    }

    /**
     * @brief Check if CRL is currently valid
     */
    [[nodiscard]] bool isValid() const noexcept {
        return isValidCrl_ && validityPeriod_.isCurrentlyValid();
    }

    /**
     * @brief Check if CRL is not yet valid
     */
    [[nodiscard]] bool isNotYetValid() const noexcept {
        return validityPeriod_.isNotYetValid();
    }

    /**
     * @brief Get revoked certificate count
     */
    [[nodiscard]] size_t getRevokedCount() const noexcept {
        return revokedCertificates_.calculateCount();
    }

    /**
     * @brief Get CRL binary data size
     */
    [[nodiscard]] size_t calculateSize() const noexcept {
        return x509CrlData_.calculateSize();
    }

    /**
     * @brief Get CRL binary data
     */
    [[nodiscard]] const std::vector<uint8_t>& getCrlBinary() const noexcept {
        return x509CrlData_.getCrlBinary();
    }

    /**
     * @brief Check if this CRL is issued by the given issuer
     */
    [[nodiscard]] bool isIssuedBy(const IssuerName& issuer) const noexcept {
        return issuerName_ == issuer;
    }

    /**
     * @brief Check if this CRL is from the given country
     */
    [[nodiscard]] bool isFromCountry(const CountryCode& country) const noexcept {
        return countryCode_ == country;
    }

    /**
     * @brief Mark CRL as invalid (superseded by newer CRL)
     */
    void invalidate() {
        isValidCrl_ = false;
        updatedAt_ = std::chrono::system_clock::now();
    }

    /**
     * @brief Set CRL number
     */
    void setCrlNumber(const std::string& crlNumber) {
        crlNumber_ = crlNumber;
        updatedAt_ = std::chrono::system_clock::now();
    }

    [[nodiscard]] std::string toString() const {
        return "CRL[id=" + id_.getValue() +
               ", issuer=" + issuerName_.getValue() +
               ", country=" + countryCode_.getValue() +
               ", revoked=" + std::to_string(getRevokedCount()) +
               ", valid=" + (isValid() ? "true" : "false") + "]";
    }
};

} // namespace certificatevalidation::domain::model

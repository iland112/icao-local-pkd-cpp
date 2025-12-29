/**
 * @file CertificateData.hpp
 * @brief Value Object for parsed certificate data
 */

#pragma once

#include "CertificateType.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <optional>

namespace fileparsing::domain::model {

/**
 * @brief Certificate data extracted from LDIF or Master List
 */
class CertificateData {
private:
    CertificateType certificateType_;
    std::string countryCode_;
    std::string subjectDn_;
    std::string issuerDn_;
    std::string serialNumber_;
    std::chrono::system_clock::time_point notBefore_;
    std::chrono::system_clock::time_point notAfter_;
    std::vector<uint8_t> certificateBinary_;
    std::string fingerprintSha256_;
    std::map<std::string, std::vector<std::string>> allAttributes_;

    // Optional conformance info (for DSC_NC)
    std::optional<std::string> conformanceText_;
    std::optional<std::string> conformanceCode_;
    std::optional<std::string> conformancePolicy_;

public:
    CertificateData() = default;

    // Builder pattern for construction
    class Builder {
    private:
        CertificateData data_;

    public:
        Builder& certificateType(CertificateType type) {
            data_.certificateType_ = type;
            return *this;
        }

        Builder& countryCode(const std::string& code) {
            data_.countryCode_ = code;
            return *this;
        }

        Builder& subjectDn(const std::string& dn) {
            data_.subjectDn_ = dn;
            return *this;
        }

        Builder& issuerDn(const std::string& dn) {
            data_.issuerDn_ = dn;
            return *this;
        }

        Builder& serialNumber(const std::string& sn) {
            data_.serialNumber_ = sn;
            return *this;
        }

        Builder& notBefore(std::chrono::system_clock::time_point tp) {
            data_.notBefore_ = tp;
            return *this;
        }

        Builder& notAfter(std::chrono::system_clock::time_point tp) {
            data_.notAfter_ = tp;
            return *this;
        }

        Builder& certificateBinary(std::vector<uint8_t> binary) {
            data_.certificateBinary_ = std::move(binary);
            return *this;
        }

        Builder& fingerprintSha256(const std::string& fp) {
            data_.fingerprintSha256_ = fp;
            return *this;
        }

        Builder& allAttributes(std::map<std::string, std::vector<std::string>> attrs) {
            data_.allAttributes_ = std::move(attrs);
            return *this;
        }

        Builder& conformanceText(const std::string& text) {
            data_.conformanceText_ = text;
            return *this;
        }

        Builder& conformanceCode(const std::string& code) {
            data_.conformanceCode_ = code;
            return *this;
        }

        Builder& conformancePolicy(const std::string& policy) {
            data_.conformancePolicy_ = policy;
            return *this;
        }

        CertificateData build() {
            return std::move(data_);
        }
    };

    static Builder builder() {
        return Builder();
    }

    // Getters
    [[nodiscard]] CertificateType getCertificateType() const noexcept { return certificateType_; }
    [[nodiscard]] const std::string& getCountryCode() const noexcept { return countryCode_; }
    [[nodiscard]] const std::string& getSubjectDn() const noexcept { return subjectDn_; }
    [[nodiscard]] const std::string& getIssuerDn() const noexcept { return issuerDn_; }
    [[nodiscard]] const std::string& getSerialNumber() const noexcept { return serialNumber_; }
    [[nodiscard]] std::chrono::system_clock::time_point getNotBefore() const noexcept { return notBefore_; }
    [[nodiscard]] std::chrono::system_clock::time_point getNotAfter() const noexcept { return notAfter_; }
    [[nodiscard]] const std::vector<uint8_t>& getCertificateBinary() const noexcept { return certificateBinary_; }
    [[nodiscard]] const std::string& getFingerprintSha256() const noexcept { return fingerprintSha256_; }
    [[nodiscard]] const std::map<std::string, std::vector<std::string>>& getAllAttributes() const noexcept { return allAttributes_; }
    [[nodiscard]] const std::optional<std::string>& getConformanceText() const noexcept { return conformanceText_; }
    [[nodiscard]] const std::optional<std::string>& getConformanceCode() const noexcept { return conformanceCode_; }
    [[nodiscard]] const std::optional<std::string>& getConformancePolicy() const noexcept { return conformancePolicy_; }

    /**
     * @brief Check if certificate is self-signed (CSCA)
     */
    [[nodiscard]] bool isSelfSigned() const noexcept {
        return subjectDn_ == issuerDn_;
    }

    /**
     * @brief Check if certificate is currently valid
     */
    [[nodiscard]] bool isCurrentlyValid() const noexcept {
        auto now = std::chrono::system_clock::now();
        return now >= notBefore_ && now <= notAfter_;
    }

    /**
     * @brief Check if certificate is expired
     */
    [[nodiscard]] bool isExpired() const noexcept {
        return std::chrono::system_clock::now() > notAfter_;
    }
};

} // namespace fileparsing::domain::model

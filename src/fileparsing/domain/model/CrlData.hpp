/**
 * @file CrlData.hpp
 * @brief Value Object for parsed CRL data
 */

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace fileparsing::domain::model {

/**
 * @brief Revoked certificate entry from CRL
 */
struct RevokedCertificate {
    std::string serialNumber;
    std::chrono::system_clock::time_point revocationDate;
    std::optional<std::string> revocationReason;
};

/**
 * @brief CRL data extracted from LDIF
 */
class CrlData {
private:
    std::string countryCode_;
    std::string issuerDn_;
    std::string crlNumber_;
    std::chrono::system_clock::time_point thisUpdate_;
    std::optional<std::chrono::system_clock::time_point> nextUpdate_;
    std::vector<uint8_t> crlBinary_;
    std::string fingerprintSha256_;
    std::vector<RevokedCertificate> revokedCertificates_;
    bool signatureValid_ = false;

public:
    CrlData() = default;

    // Builder pattern
    class Builder {
    private:
        CrlData data_;

    public:
        Builder& countryCode(const std::string& code) {
            data_.countryCode_ = code;
            return *this;
        }

        Builder& issuerDn(const std::string& dn) {
            data_.issuerDn_ = dn;
            return *this;
        }

        Builder& crlNumber(const std::string& num) {
            data_.crlNumber_ = num;
            return *this;
        }

        Builder& thisUpdate(std::chrono::system_clock::time_point tp) {
            data_.thisUpdate_ = tp;
            return *this;
        }

        Builder& nextUpdate(std::chrono::system_clock::time_point tp) {
            data_.nextUpdate_ = tp;
            return *this;
        }

        Builder& crlBinary(std::vector<uint8_t> binary) {
            data_.crlBinary_ = std::move(binary);
            return *this;
        }

        Builder& fingerprintSha256(const std::string& fp) {
            data_.fingerprintSha256_ = fp;
            return *this;
        }

        Builder& revokedCertificates(std::vector<RevokedCertificate> revoked) {
            data_.revokedCertificates_ = std::move(revoked);
            return *this;
        }

        Builder& signatureValid(bool valid) {
            data_.signatureValid_ = valid;
            return *this;
        }

        CrlData build() {
            return std::move(data_);
        }
    };

    static Builder builder() {
        return Builder();
    }

    // Getters
    [[nodiscard]] const std::string& getCountryCode() const noexcept { return countryCode_; }
    [[nodiscard]] const std::string& getIssuerDn() const noexcept { return issuerDn_; }
    [[nodiscard]] const std::string& getCrlNumber() const noexcept { return crlNumber_; }
    [[nodiscard]] std::chrono::system_clock::time_point getThisUpdate() const noexcept { return thisUpdate_; }
    [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>& getNextUpdate() const noexcept { return nextUpdate_; }
    [[nodiscard]] const std::vector<uint8_t>& getCrlBinary() const noexcept { return crlBinary_; }
    [[nodiscard]] const std::string& getFingerprintSha256() const noexcept { return fingerprintSha256_; }
    [[nodiscard]] const std::vector<RevokedCertificate>& getRevokedCertificates() const noexcept { return revokedCertificates_; }
    [[nodiscard]] bool isSignatureValid() const noexcept { return signatureValid_; }

    /**
     * @brief Get count of revoked certificates
     */
    [[nodiscard]] size_t getRevokedCount() const noexcept {
        return revokedCertificates_.size();
    }

    /**
     * @brief Check if CRL is expired
     */
    [[nodiscard]] bool isExpired() const noexcept {
        if (!nextUpdate_) return false;
        return std::chrono::system_clock::now() > *nextUpdate_;
    }

    /**
     * @brief Check if a certificate serial is revoked
     */
    [[nodiscard]] bool isRevoked(const std::string& serialNumber) const noexcept {
        for (const auto& revoked : revokedCertificates_) {
            if (revoked.serialNumber == serialNumber) {
                return true;
            }
        }
        return false;
    }
};

} // namespace fileparsing::domain::model

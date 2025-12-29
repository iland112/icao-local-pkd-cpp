/**
 * @file X509CrlData.hpp
 * @brief Value Object for X.509 CRL binary data
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <vector>
#include <string>
#include <stdexcept>

namespace certificatevalidation::domain::model {

/**
 * @brief X.509 CRL binary data Value Object
 *
 * Contains the DER-encoded CRL binary data.
 */
class X509CrlData : public shared::domain::ValueObject {
private:
    std::vector<uint8_t> crlBinary_;
    std::string fingerprintSha256_;

    X509CrlData(std::vector<uint8_t> crlBinary, std::string fingerprint)
        : crlBinary_(std::move(crlBinary)),
          fingerprintSha256_(std::move(fingerprint)) {
        validate();
    }

    void validate() const {
        if (crlBinary_.empty()) {
            throw std::invalid_argument("CRL binary cannot be empty");
        }
    }

public:
    /**
     * @brief Create X509CrlData from binary
     */
    static X509CrlData of(std::vector<uint8_t> crlBinary, const std::string& fingerprint = "") {
        return X509CrlData(std::move(crlBinary), fingerprint);
    }

    [[nodiscard]] const std::vector<uint8_t>& getCrlBinary() const noexcept {
        return crlBinary_;
    }

    [[nodiscard]] const std::string& getFingerprintSha256() const noexcept {
        return fingerprintSha256_;
    }

    [[nodiscard]] size_t calculateSize() const noexcept {
        return crlBinary_.size();
    }

    bool operator==(const X509CrlData& other) const noexcept {
        return crlBinary_ == other.crlBinary_;
    }
};

} // namespace certificatevalidation::domain::model

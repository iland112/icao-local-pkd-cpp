/**
 * @file CountryCode.hpp
 * @brief Value Object for ISO 3166-1 alpha-2 country code
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace certificatevalidation::domain::model {

/**
 * @brief ISO 3166-1 alpha-2 country code Value Object
 *
 * Represents a two-letter country code as defined in ISO 3166-1 alpha-2.
 * Used to identify the issuing country of certificates and CRLs.
 */
class CountryCode : public shared::domain::ValueObject {
private:
    std::string value_;

    explicit CountryCode(std::string value) : value_(std::move(value)) {
        normalize();
        validate();
    }

    void normalize() {
        // Convert to uppercase
        std::transform(value_.begin(), value_.end(), value_.begin(), ::toupper);
    }

    void validate() const {
        if (value_.length() != 2) {
            throw std::invalid_argument("CountryCode must be exactly 2 characters");
        }
        for (char c : value_) {
            if (!std::isalpha(static_cast<unsigned char>(c))) {
                throw std::invalid_argument("CountryCode must contain only letters");
            }
        }
    }

public:
    /**
     * @brief Create CountryCode from string
     * @param value Two-letter country code (case-insensitive)
     */
    static CountryCode of(const std::string& value) {
        return CountryCode(value);
    }

    /**
     * @brief Extract country code from Distinguished Name
     * @param dn Distinguished Name (e.g., "CN=CSCA-QA,O=Qatar,C=QA")
     * @return CountryCode if found
     */
    static std::optional<CountryCode> fromDn(const std::string& dn) {
        // Look for C=XX pattern
        size_t pos = dn.find("C=");
        if (pos == std::string::npos) {
            return std::nullopt;
        }

        size_t start = pos + 2;
        size_t end = dn.find(',', start);
        if (end == std::string::npos) {
            end = dn.length();
        }

        std::string code = dn.substr(start, end - start);
        // Trim whitespace
        code.erase(0, code.find_first_not_of(" \t"));
        code.erase(code.find_last_not_of(" \t") + 1);

        if (code.length() == 2) {
            try {
                return CountryCode::of(code);
            } catch (const std::invalid_argument&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] const std::string& getValue() const noexcept {
        return value_;
    }

    bool operator==(const CountryCode& other) const noexcept {
        return value_ == other.value_;
    }

    bool operator!=(const CountryCode& other) const noexcept {
        return !(*this == other);
    }

    bool operator<(const CountryCode& other) const noexcept {
        return value_ < other.value_;
    }

    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace certificatevalidation::domain::model

namespace std {
    template<>
    struct hash<certificatevalidation::domain::model::CountryCode> {
        size_t operator()(const certificatevalidation::domain::model::CountryCode& cc) const {
            return hash<string>()(cc.getValue());
        }
    };
}

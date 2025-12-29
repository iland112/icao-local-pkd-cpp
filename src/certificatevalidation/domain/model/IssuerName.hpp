/**
 * @file IssuerName.hpp
 * @brief Value Object for CSCA issuer name
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <string>
#include <regex>
#include <stdexcept>

namespace certificatevalidation::domain::model {

/**
 * @brief CSCA issuer name Value Object
 *
 * Represents a CSCA issuer name (e.g., "CSCA-QA", "CSCA-NZ").
 * Used to identify the issuing CSCA for CRLs.
 */
class IssuerName : public shared::domain::ValueObject {
private:
    std::string value_;

    explicit IssuerName(std::string value) : value_(std::move(value)) {
        validate();
    }

    void validate() const {
        if (value_.empty()) {
            throw std::invalid_argument("IssuerName cannot be empty");
        }
    }

public:
    /**
     * @brief Create IssuerName from string
     */
    static IssuerName of(const std::string& value) {
        return IssuerName(value);
    }

    /**
     * @brief Extract IssuerName from Distinguished Name
     * @param dn Distinguished Name (e.g., "CN=CSCA-QA,O=Qatar,C=QA")
     * @return IssuerName if found
     */
    static std::optional<IssuerName> fromDn(const std::string& dn) {
        std::regex pattern("CN=([^,]+)");
        std::smatch match;
        if (std::regex_search(dn, match, pattern)) {
            std::string cn = match[1].str();
            // Trim whitespace
            cn.erase(0, cn.find_first_not_of(" \t"));
            cn.erase(cn.find_last_not_of(" \t") + 1);
            if (!cn.empty()) {
                return IssuerName::of(cn);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] const std::string& getValue() const noexcept {
        return value_;
    }

    /**
     * @brief Extract country code from issuer name (e.g., "CSCA-QA" -> "QA")
     */
    [[nodiscard]] std::string getCountryCode() const {
        std::regex pattern("CSCA-([A-Z]{2})");
        std::smatch match;
        if (std::regex_search(value_, match, pattern)) {
            return match[1].str();
        }
        return "";
    }

    /**
     * @brief Check if this issuer is for a specific country
     */
    [[nodiscard]] bool isCountry(const std::string& countryCode) const {
        return getCountryCode() == countryCode;
    }

    bool operator==(const IssuerName& other) const noexcept {
        return value_ == other.value_;
    }

    bool operator!=(const IssuerName& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace certificatevalidation::domain::model

namespace std {
    template<>
    struct hash<certificatevalidation::domain::model::IssuerName> {
        size_t operator()(const certificatevalidation::domain::model::IssuerName& name) const {
            return hash<string>()(name.getValue());
        }
    };
}

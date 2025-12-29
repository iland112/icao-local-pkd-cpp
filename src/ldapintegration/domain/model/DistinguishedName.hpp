/**
 * @file DistinguishedName.hpp
 * @brief LDAP Distinguished Name Value Object
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include "shared/exception/DomainException.hpp"
#include <string>
#include <vector>
#include <regex>
#include <sstream>
#include <algorithm>

namespace ldapintegration::domain::model {

/**
 * @brief LDAP Distinguished Name Value Object
 *
 * Represents an LDAP Distinguished Name (DN) in RFC 2253 format.
 *
 * Format example:
 * cn=CSCA-KOREA,ou=csca,o=ICAO-PKD,dc=ldap,dc=smartcoreinc,dc=com
 *
 * Business Rules:
 * - DN cannot be null or empty
 * - DN must contain at least one RDN component
 * - Each RDN must be in attribute=value format
 */
class DistinguishedName : public shared::domain::ValueObject {
private:
    std::string value_;

    explicit DistinguishedName(std::string value) : value_(std::move(value)) {
        validate();
    }

    void validate() const {
        if (value_.empty()) {
            throw shared::exception::DomainException(
                "INVALID_DN",
                "Distinguished Name must not be null or empty"
            );
        }

        // Must contain at least one '='
        if (value_.find('=') == std::string::npos) {
            throw shared::exception::DomainException(
                "INVALID_DN_FORMAT",
                "Distinguished Name must contain '=' in RDN components: " + value_
            );
        }

        // Validate each RDN component
        std::vector<std::string> rdns = splitRdns();
        for (const auto& rdn : rdns) {
            if (rdn.find('=') == std::string::npos) {
                throw shared::exception::DomainException(
                    "INVALID_RDN_FORMAT",
                    "RDN must be in attribute=value format: " + rdn
                );
            }
        }
    }

    /**
     * @brief Split DN into RDN components
     */
    std::vector<std::string> splitRdns() const {
        std::vector<std::string> rdns;
        std::stringstream ss(value_);
        std::string item;

        while (std::getline(ss, item, ',')) {
            // Trim whitespace
            size_t start = item.find_first_not_of(" \t");
            size_t end = item.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                rdns.push_back(item.substr(start, end - start + 1));
            }
        }

        return rdns;
    }

    /**
     * @brief Extract attribute value from DN
     */
    std::string extractAttribute(const std::string& attributeType) const {
        std::string pattern = attributeType + "=";
        std::string lowerValue = value_;
        std::string lowerPattern = pattern;

        // Convert to lowercase for case-insensitive comparison
        std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
        std::transform(lowerPattern.begin(), lowerPattern.end(), lowerPattern.begin(), ::tolower);

        size_t startIndex = lowerValue.find(lowerPattern);
        if (startIndex == std::string::npos) {
            return "";
        }

        size_t valueStart = startIndex + pattern.length();
        size_t valueEnd = value_.find(',', valueStart);

        if (valueEnd == std::string::npos) {
            valueEnd = value_.length();
        }

        std::string result = value_.substr(valueStart, valueEnd - valueStart);

        // Trim whitespace
        size_t start = result.find_first_not_of(" \t");
        size_t end = result.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            return result.substr(start, end - start + 1);
        }

        return result;
    }

public:
    /**
     * @brief Create DistinguishedName from string
     */
    static DistinguishedName of(const std::string& value) {
        return DistinguishedName(value);
    }

    /**
     * @brief Get the DN value
     */
    [[nodiscard]] const std::string& getValue() const noexcept {
        return value_;
    }

    /**
     * @brief Extract Common Name (cn)
     */
    [[nodiscard]] std::string getCommonName() const {
        return extractAttribute("cn");
    }

    /**
     * @brief Extract Organizational Unit (ou)
     */
    [[nodiscard]] std::string getOrganizationalUnit() const {
        return extractAttribute("ou");
    }

    /**
     * @brief Extract Organization (o)
     */
    [[nodiscard]] std::string getOrganization() const {
        return extractAttribute("o");
    }

    /**
     * @brief Extract Domain Component (dc)
     */
    [[nodiscard]] std::string getDomainComponent() const {
        return extractAttribute("dc");
    }

    /**
     * @brief Extract Country (c)
     */
    [[nodiscard]] std::string getCountry() const {
        return extractAttribute("c");
    }

    /**
     * @brief Check if this DN is under the given base DN
     */
    [[nodiscard]] bool isUnderBase(const DistinguishedName& baseDn) const noexcept {
        std::string currentLower = value_;
        std::string baseLower = baseDn.getValue();

        std::transform(currentLower.begin(), currentLower.end(), currentLower.begin(), ::tolower);
        std::transform(baseLower.begin(), baseLower.end(), baseLower.begin(), ::tolower);

        // Exact match
        if (currentLower == baseLower) {
            return true;
        }

        // DN ends with ",baseDn"
        std::string suffix = "," + baseLower;
        if (currentLower.length() > suffix.length()) {
            return currentLower.compare(
                currentLower.length() - suffix.length(),
                suffix.length(),
                suffix
            ) == 0;
        }

        return false;
    }

    /**
     * @brief Get parent DN
     */
    [[nodiscard]] std::optional<DistinguishedName> getParent() const {
        size_t commaPos = value_.find(',');
        if (commaPos == std::string::npos) {
            return std::nullopt;
        }

        std::string parentValue = value_.substr(commaPos + 1);
        if (parentValue.empty()) {
            return std::nullopt;
        }

        // Trim leading whitespace
        size_t start = parentValue.find_first_not_of(" \t");
        if (start != std::string::npos) {
            parentValue = parentValue.substr(start);
        }

        try {
            return DistinguishedName::of(parentValue);
        } catch (...) {
            return std::nullopt;
        }
    }

    /**
     * @brief Get RFC 2253 format
     */
    [[nodiscard]] std::string toRfc2253Format() const {
        return value_;
    }

    bool operator==(const DistinguishedName& other) const noexcept {
        // Case-insensitive comparison for DN
        std::string thisLower = value_;
        std::string otherLower = other.value_;
        std::transform(thisLower.begin(), thisLower.end(), thisLower.begin(), ::tolower);
        std::transform(otherLower.begin(), otherLower.end(), otherLower.begin(), ::tolower);
        return thisLower == otherLower;
    }

    bool operator!=(const DistinguishedName& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace ldapintegration::domain::model

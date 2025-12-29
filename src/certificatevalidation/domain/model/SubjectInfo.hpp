/**
 * @file SubjectInfo.hpp
 * @brief Value Object for certificate subject information
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <string>
#include <optional>
#include <regex>

namespace certificatevalidation::domain::model {

/**
 * @brief Certificate subject information Value Object
 *
 * Contains the subject (certificate holder) information:
 * - Distinguished Name (DN)
 * - Country Code
 * - Organization
 * - Organizational Unit
 * - Common Name
 */
class SubjectInfo : public shared::domain::ValueObject {
private:
    std::string distinguishedName_;
    std::string countryCode_;
    std::optional<std::string> organization_;
    std::optional<std::string> organizationalUnit_;
    std::optional<std::string> commonName_;

    SubjectInfo(
        std::string dn,
        std::string countryCode,
        std::optional<std::string> organization,
        std::optional<std::string> organizationalUnit,
        std::optional<std::string> commonName
    ) : distinguishedName_(std::move(dn)),
        countryCode_(std::move(countryCode)),
        organization_(std::move(organization)),
        organizationalUnit_(std::move(organizationalUnit)),
        commonName_(std::move(commonName)) {}

    static std::optional<std::string> extractFromDn(
        const std::string& dn,
        const std::string& attribute
    ) {
        std::regex pattern(attribute + "=([^,]+)");
        std::smatch match;
        if (std::regex_search(dn, match, pattern)) {
            std::string value = match[1].str();
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            return value;
        }
        return std::nullopt;
    }

public:
    /**
     * @brief Create SubjectInfo from Distinguished Name
     * @param dn Distinguished Name (e.g., "CN=CSCA-QA,O=Qatar,C=QA")
     */
    static SubjectInfo fromDn(const std::string& dn) {
        auto cn = extractFromDn(dn, "CN");
        auto o = extractFromDn(dn, "O");
        auto ou = extractFromDn(dn, "OU");
        auto c = extractFromDn(dn, "C");

        return SubjectInfo(
            dn,
            c.value_or(""),
            o,
            ou,
            cn
        );
    }

    /**
     * @brief Create SubjectInfo with all fields
     */
    static SubjectInfo of(
        const std::string& dn,
        const std::string& countryCode,
        const std::optional<std::string>& organization = std::nullopt,
        const std::optional<std::string>& organizationalUnit = std::nullopt,
        const std::optional<std::string>& commonName = std::nullopt
    ) {
        return SubjectInfo(dn, countryCode, organization, organizationalUnit, commonName);
    }

    // Getters
    [[nodiscard]] const std::string& getDistinguishedName() const noexcept {
        return distinguishedName_;
    }

    [[nodiscard]] const std::string& getCountryCode() const noexcept {
        return countryCode_;
    }

    [[nodiscard]] const std::optional<std::string>& getOrganization() const noexcept {
        return organization_;
    }

    [[nodiscard]] const std::optional<std::string>& getOrganizationalUnit() const noexcept {
        return organizationalUnit_;
    }

    [[nodiscard]] const std::optional<std::string>& getCommonName() const noexcept {
        return commonName_;
    }

    [[nodiscard]] std::string getCommonNameOrDefault() const {
        return commonName_.value_or("Unknown");
    }

    [[nodiscard]] bool isComplete() const noexcept {
        return !distinguishedName_.empty() &&
               !countryCode_.empty() &&
               commonName_.has_value();
    }

    bool operator==(const SubjectInfo& other) const noexcept {
        return distinguishedName_ == other.distinguishedName_;
    }

    [[nodiscard]] std::string toString() const {
        return distinguishedName_;
    }
};

} // namespace certificatevalidation::domain::model

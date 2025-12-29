/**
 * @file LdapMasterListEntry.hpp
 * @brief LDAP Master List Entry Entity
 */

#pragma once

#include "DistinguishedName.hpp"
#include "LdapEntryType.hpp"
#include "shared/exception/DomainException.hpp"
#include "shared/util/Base64Util.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace ldapintegration::domain::model {

/**
 * @brief LDAP Master List Entry
 *
 * Represents a Master List entry stored in LDAP.
 *
 * LDAP DN Structure:
 * cn={ISSUER-CN},o=ml,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
 *
 * LDAP Attributes:
 * - cn: Common Name (Issuer CN)
 * - masterList;binary: DER-encoded Master List
 * - issuerDN: Master List Issuer DN
 * - version: Master List version
 */
class LdapMasterListEntry {
private:
    DistinguishedName dn_;
    std::string masterListId_;
    std::string issuerDn_;
    std::string issuerName_;
    std::string countryCode_;
    std::vector<uint8_t> masterListBinary_;
    int version_;
    std::chrono::system_clock::time_point signingTime_;
    int certificateCount_;
    std::optional<std::chrono::system_clock::time_point> lastSyncAt_;

    LdapMasterListEntry(
        DistinguishedName dn,
        std::string masterListId,
        std::string issuerDn,
        std::string issuerName,
        std::string countryCode,
        std::vector<uint8_t> masterListBinary,
        int version,
        std::chrono::system_clock::time_point signingTime,
        int certificateCount
    ) : dn_(std::move(dn)),
        masterListId_(std::move(masterListId)),
        issuerDn_(std::move(issuerDn)),
        issuerName_(std::move(issuerName)),
        countryCode_(std::move(countryCode)),
        masterListBinary_(std::move(masterListBinary)),
        version_(version),
        signingTime_(signingTime),
        certificateCount_(certificateCount),
        lastSyncAt_(std::nullopt) {}

public:
    /**
     * @brief Create LdapMasterListEntry
     */
    static LdapMasterListEntry create(
        const std::string& baseDn,
        const std::string& masterListId,
        const std::string& issuerDn,
        const std::string& countryCode,
        const std::vector<uint8_t>& masterListBinary,
        int version,
        std::chrono::system_clock::time_point signingTime,
        int certificateCount
    ) {
        if (masterListBinary.empty()) {
            throw shared::exception::DomainException(
                "EMPTY_MASTER_LIST_DATA",
                "Master List data must not be empty"
            );
        }

        // Extract CN from issuer DN
        std::string issuerName = extractCommonName(issuerDn);

        // Sanitize CN for LDAP
        std::string sanitizedCn = sanitizeCnForLdap(issuerName);

        // Build DN
        std::string ouPath = getOuPath(LdapEntryType::MASTER_LIST, baseDn);
        std::string dnValue = "cn=" + sanitizedCn + ",c=" + countryCode + "," + ouPath;

        return LdapMasterListEntry(
            DistinguishedName::of(dnValue),
            masterListId,
            issuerDn,
            issuerName,
            countryCode,
            masterListBinary,
            version,
            signingTime,
            certificateCount
        );
    }

    // ========== Getters ==========
    [[nodiscard]] const DistinguishedName& getDn() const noexcept { return dn_; }
    [[nodiscard]] const std::string& getMasterListId() const noexcept { return masterListId_; }
    [[nodiscard]] const std::string& getIssuerDn() const noexcept { return issuerDn_; }
    [[nodiscard]] const std::string& getIssuerName() const noexcept { return issuerName_; }
    [[nodiscard]] const std::string& getCountryCode() const noexcept { return countryCode_; }
    [[nodiscard]] const std::vector<uint8_t>& getMasterListBinary() const noexcept { return masterListBinary_; }
    [[nodiscard]] int getVersion() const noexcept { return version_; }
    [[nodiscard]] std::chrono::system_clock::time_point getSigningTime() const noexcept { return signingTime_; }
    [[nodiscard]] int getCertificateCount() const noexcept { return certificateCount_; }
    [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>& getLastSyncAt() const noexcept { return lastSyncAt_; }

    /**
     * @brief Get Master List as Base64 string
     */
    [[nodiscard]] std::string getMasterListBase64() const {
        return shared::util::Base64Util::encode(masterListBinary_);
    }

    // ========== Business Logic ==========

    /**
     * @brief Check if this version is newer than existing
     */
    [[nodiscard]] bool isNewerThan(int existingVersion) const noexcept {
        return version_ > existingVersion;
    }

    /**
     * @brief Mark as synced to LDAP
     */
    void markAsSynced() {
        lastSyncAt_ = std::chrono::system_clock::now();
    }

    [[nodiscard]] std::string toString() const {
        return "LdapMasterListEntry[dn=" + dn_.getValue() +
               ", issuer=" + issuerName_ +
               ", version=" + std::to_string(version_) +
               ", certCount=" + std::to_string(certificateCount_) + "]";
    }

private:
    /**
     * @brief Extract Common Name from DN
     */
    static std::string extractCommonName(const std::string& dn) {
        std::regex cnPattern("CN=([^,]+)", std::regex::icase);
        std::smatch match;
        if (std::regex_search(dn, match, cnPattern)) {
            return match[1].str();
        }
        return dn;
    }

    /**
     * @brief Sanitize CN for LDAP
     */
    static std::string sanitizeCnForLdap(const std::string& cn) {
        if (cn.empty()) {
            return "unknown";
        }

        std::string result = cn;
        std::regex specialChars("[,=\\s+]");
        result = std::regex_replace(result, specialChars, "-");
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);

        return result;
    }
};

} // namespace ldapintegration::domain::model

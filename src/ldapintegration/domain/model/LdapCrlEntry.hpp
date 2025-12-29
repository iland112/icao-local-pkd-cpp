/**
 * @file LdapCrlEntry.hpp
 * @brief LDAP CRL Entry Entity
 */

#pragma once

#include "DistinguishedName.hpp"
#include "LdapEntryType.hpp"
#include "shared/exception/DomainException.hpp"
#include "shared/util/Base64Util.hpp"
#include <string>
#include <vector>
#include <set>
#include <chrono>
#include <optional>
#include <sstream>
#include <regex>

namespace ldapintegration::domain::model {

/**
 * @brief LDAP CRL Entry
 *
 * Represents an X.509 CRL entry stored in LDAP.
 *
 * LDAP DN Structure:
 * cn={ISSUER-CN},o=crl,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
 *
 * LDAP Attributes:
 * - cn: Common Name (Issuer CN)
 * - certificateRevocationList;binary: DER-encoded CRL
 * - issuerDN: CRL Issuer DN
 * - thisUpdate: CRL issue date
 * - nextUpdate: Next CRL issue date
 */
class LdapCrlEntry {
private:
    DistinguishedName dn_;
    std::string crlId_;
    std::string issuerDn_;
    std::string issuerName_;
    std::string countryCode_;
    std::vector<uint8_t> x509CrlBinary_;
    std::chrono::system_clock::time_point thisUpdate_;
    std::chrono::system_clock::time_point nextUpdate_;
    std::set<std::string> revokedSerialNumbers_;
    std::optional<std::chrono::system_clock::time_point> lastSyncAt_;

    LdapCrlEntry(
        DistinguishedName dn,
        std::string crlId,
        std::string issuerDn,
        std::string issuerName,
        std::string countryCode,
        std::vector<uint8_t> x509CrlBinary,
        std::chrono::system_clock::time_point thisUpdate,
        std::chrono::system_clock::time_point nextUpdate,
        std::set<std::string> revokedSerialNumbers
    ) : dn_(std::move(dn)),
        crlId_(std::move(crlId)),
        issuerDn_(std::move(issuerDn)),
        issuerName_(std::move(issuerName)),
        countryCode_(std::move(countryCode)),
        x509CrlBinary_(std::move(x509CrlBinary)),
        thisUpdate_(thisUpdate),
        nextUpdate_(nextUpdate),
        revokedSerialNumbers_(std::move(revokedSerialNumbers)),
        lastSyncAt_(std::nullopt) {}

public:
    /**
     * @brief Create LdapCrlEntry
     */
    static LdapCrlEntry create(
        const std::string& baseDn,
        const std::string& crlId,
        const std::string& issuerDn,
        const std::string& countryCode,
        const std::vector<uint8_t>& x509CrlBinary,
        std::chrono::system_clock::time_point thisUpdate,
        std::chrono::system_clock::time_point nextUpdate,
        const std::set<std::string>& revokedSerialNumbers
    ) {
        if (x509CrlBinary.empty()) {
            throw shared::exception::DomainException(
                "EMPTY_CRL_DATA",
                "CRL data must not be empty"
            );
        }

        // Extract CN from issuer DN
        std::string issuerName = extractCommonName(issuerDn);

        // Sanitize CN for LDAP
        std::string sanitizedCn = sanitizeCnForLdap(issuerName);

        // Build DN
        std::string ouPath = getOuPath(LdapEntryType::CRL, baseDn);
        std::string dnValue = "cn=" + sanitizedCn + ",c=" + countryCode + "," + ouPath;

        return LdapCrlEntry(
            DistinguishedName::of(dnValue),
            crlId,
            issuerDn,
            issuerName,
            countryCode,
            x509CrlBinary,
            thisUpdate,
            nextUpdate,
            revokedSerialNumbers
        );
    }

    // ========== Getters ==========
    [[nodiscard]] const DistinguishedName& getDn() const noexcept { return dn_; }
    [[nodiscard]] const std::string& getCrlId() const noexcept { return crlId_; }
    [[nodiscard]] const std::string& getIssuerDn() const noexcept { return issuerDn_; }
    [[nodiscard]] const std::string& getIssuerName() const noexcept { return issuerName_; }
    [[nodiscard]] const std::string& getCountryCode() const noexcept { return countryCode_; }
    [[nodiscard]] const std::vector<uint8_t>& getX509CrlBinary() const noexcept { return x509CrlBinary_; }
    [[nodiscard]] std::chrono::system_clock::time_point getThisUpdate() const noexcept { return thisUpdate_; }
    [[nodiscard]] std::chrono::system_clock::time_point getNextUpdate() const noexcept { return nextUpdate_; }
    [[nodiscard]] const std::set<std::string>& getRevokedSerialNumbers() const noexcept { return revokedSerialNumbers_; }
    [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>& getLastSyncAt() const noexcept { return lastSyncAt_; }

    /**
     * @brief Get CRL as Base64 string
     */
    [[nodiscard]] std::string getX509CrlBase64() const {
        return shared::util::Base64Util::encode(x509CrlBinary_);
    }

    /**
     * @brief Get revoked serial numbers as semicolon-separated string
     */
    [[nodiscard]] std::string getRevokedSerialNumbersString() const {
        std::ostringstream oss;
        bool first = true;
        for (const auto& serial : revokedSerialNumbers_) {
            if (!first) oss << ";";
            oss << serial;
            first = false;
        }
        return oss.str();
    }

    // ========== Business Logic ==========

    /**
     * @brief Check if CRL is expired
     */
    [[nodiscard]] bool isExpired() const noexcept {
        return std::chrono::system_clock::now() > nextUpdate_;
    }

    /**
     * @brief Check if CRL needs update
     */
    [[nodiscard]] bool needsUpdate() const noexcept {
        if (!lastSyncAt_) {
            return true;
        }
        return std::chrono::system_clock::now() > nextUpdate_;
    }

    /**
     * @brief Mark as synced to LDAP
     */
    void markAsSynced() {
        lastSyncAt_ = std::chrono::system_clock::now();
    }

    /**
     * @brief Check if a serial number is revoked
     */
    [[nodiscard]] bool isSerialNumberRevoked(const std::string& serialNumber) const {
        return revokedSerialNumbers_.find(serialNumber) != revokedSerialNumbers_.end();
    }

    /**
     * @brief Get revoked certificate count
     */
    [[nodiscard]] size_t getRevokedCount() const noexcept {
        return revokedSerialNumbers_.size();
    }

    [[nodiscard]] std::string toString() const {
        return "LdapCrlEntry[dn=" + dn_.getValue() +
               ", issuer=" + issuerName_ +
               ", revokedCount=" + std::to_string(revokedSerialNumbers_.size()) + "]";
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

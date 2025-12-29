/**
 * @file LdapCertificateEntry.hpp
 * @brief LDAP Certificate Entry Entity
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
#include <regex>

namespace ldapintegration::domain::model {

/**
 * @brief LDAP Certificate Entry
 *
 * Represents an X.509 certificate entry stored in LDAP.
 *
 * LDAP DN Structure:
 * cn={SUBJECT-CN},o=csca,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
 *
 * LDAP Attributes:
 * - cn: Common Name (Subject DN)
 * - userCertificate;binary: DER-encoded certificate
 * - certificateFingerprint: SHA-256 fingerprint
 * - serialNumber: Certificate serial number
 * - issuerDN: Issuer Distinguished Name
 * - notBefore: Validity start date
 * - notAfter: Validity end date
 * - description: Validation status
 */
class LdapCertificateEntry {
private:
    DistinguishedName dn_;
    std::string certificateId_;
    std::vector<uint8_t> x509CertificateBinary_;
    std::string fingerprint_;
    std::string serialNumber_;
    std::string issuerDn_;
    LdapEntryType entryType_;
    std::string countryCode_;
    std::chrono::system_clock::time_point notBefore_;
    std::chrono::system_clock::time_point notAfter_;
    std::string validationStatus_;
    std::optional<std::chrono::system_clock::time_point> lastSyncAt_;

    LdapCertificateEntry(
        DistinguishedName dn,
        std::string certificateId,
        std::vector<uint8_t> x509CertificateBinary,
        std::string fingerprint,
        std::string serialNumber,
        std::string issuerDn,
        LdapEntryType entryType,
        std::string countryCode,
        std::chrono::system_clock::time_point notBefore,
        std::chrono::system_clock::time_point notAfter
    ) : dn_(std::move(dn)),
        certificateId_(std::move(certificateId)),
        x509CertificateBinary_(std::move(x509CertificateBinary)),
        fingerprint_(std::move(fingerprint)),
        serialNumber_(std::move(serialNumber)),
        issuerDn_(std::move(issuerDn)),
        entryType_(entryType),
        countryCode_(std::move(countryCode)),
        notBefore_(notBefore),
        notAfter_(notAfter),
        validationStatus_("VALID"),
        lastSyncAt_(std::nullopt) {}

public:
    /**
     * @brief Create LdapCertificateEntry
     */
    static LdapCertificateEntry create(
        const std::string& baseDn,
        const std::string& certificateId,
        const std::string& subjectDn,
        const std::vector<uint8_t>& x509CertificateBinary,
        const std::string& fingerprint,
        const std::string& serialNumber,
        const std::string& issuerDn,
        LdapEntryType entryType,
        const std::string& countryCode,
        std::chrono::system_clock::time_point notBefore,
        std::chrono::system_clock::time_point notAfter
    ) {
        if (x509CertificateBinary.empty()) {
            throw shared::exception::DomainException(
                "EMPTY_CERTIFICATE_DATA",
                "Certificate data must not be empty"
            );
        }

        // Extract CN from subject DN
        std::string cn = extractCommonName(subjectDn);

        // Sanitize CN for LDAP
        std::string sanitizedCn = sanitizeCnForLdap(cn);

        // Build DN
        std::string ouPath = getOuPath(entryType, baseDn);
        std::string dnValue = "cn=" + sanitizedCn + ",c=" + countryCode + "," + ouPath;

        return LdapCertificateEntry(
            DistinguishedName::of(dnValue),
            certificateId,
            x509CertificateBinary,
            fingerprint,
            serialNumber,
            issuerDn,
            entryType,
            countryCode,
            notBefore,
            notAfter
        );
    }

    // ========== Getters ==========
    [[nodiscard]] const DistinguishedName& getDn() const noexcept { return dn_; }
    [[nodiscard]] const std::string& getCertificateId() const noexcept { return certificateId_; }
    [[nodiscard]] const std::vector<uint8_t>& getX509CertificateBinary() const noexcept { return x509CertificateBinary_; }
    [[nodiscard]] const std::string& getFingerprint() const noexcept { return fingerprint_; }
    [[nodiscard]] const std::string& getSerialNumber() const noexcept { return serialNumber_; }
    [[nodiscard]] const std::string& getIssuerDn() const noexcept { return issuerDn_; }
    [[nodiscard]] LdapEntryType getEntryType() const noexcept { return entryType_; }
    [[nodiscard]] const std::string& getCountryCode() const noexcept { return countryCode_; }
    [[nodiscard]] std::chrono::system_clock::time_point getNotBefore() const noexcept { return notBefore_; }
    [[nodiscard]] std::chrono::system_clock::time_point getNotAfter() const noexcept { return notAfter_; }
    [[nodiscard]] const std::string& getValidationStatus() const noexcept { return validationStatus_; }
    [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>& getLastSyncAt() const noexcept { return lastSyncAt_; }

    /**
     * @brief Get certificate as Base64 string
     */
    [[nodiscard]] std::string getX509CertificateBase64() const {
        return shared::util::Base64Util::encode(x509CertificateBinary_);
    }

    // ========== Business Logic ==========

    /**
     * @brief Check if certificate is expired
     */
    [[nodiscard]] bool isExpired() const noexcept {
        return std::chrono::system_clock::now() > notAfter_;
    }

    /**
     * @brief Check if certificate is not yet valid
     */
    [[nodiscard]] bool isNotYetValid() const noexcept {
        return std::chrono::system_clock::now() < notBefore_;
    }

    /**
     * @brief Check if certificate is currently valid
     */
    [[nodiscard]] bool isCurrentlyValid() const noexcept {
        return !isExpired() && !isNotYetValid();
    }

    /**
     * @brief Mark as synced to LDAP
     */
    void markAsSynced() {
        lastSyncAt_ = std::chrono::system_clock::now();
    }

    /**
     * @brief Set validation status
     */
    void setValidationStatus(const std::string& status) {
        validationStatus_ = status;
    }

    /**
     * @brief Check if sync is needed
     */
    [[nodiscard]] bool needsSync(int syncIntervalMinutes) const {
        if (!lastSyncAt_) {
            return true;
        }
        auto nextSyncTime = *lastSyncAt_ + std::chrono::minutes(syncIntervalMinutes);
        return std::chrono::system_clock::now() > nextSyncTime;
    }

    [[nodiscard]] std::string toString() const {
        return "LdapCertificateEntry[dn=" + dn_.getValue() +
               ", fingerprint=" + fingerprint_ +
               ", type=" + ldapintegration::domain::model::toString(entryType_) +
               ", status=" + validationStatus_ + "]";
    }

private:
    /**
     * @brief Extract Common Name from Subject DN
     */
    static std::string extractCommonName(const std::string& subjectDn) {
        std::regex cnPattern("CN=([^,]+)", std::regex::icase);
        std::smatch match;
        if (std::regex_search(subjectDn, match, cnPattern)) {
            return match[1].str();
        }
        return subjectDn;
    }

    /**
     * @brief Sanitize CN for LDAP (remove special characters)
     */
    static std::string sanitizeCnForLdap(const std::string& cn) {
        if (cn.empty()) {
            return "unknown";
        }

        std::string result = cn;
        // Replace special characters with hyphen
        std::regex specialChars("[,=\\s+]");
        result = std::regex_replace(result, specialChars, "-");

        // Convert to lowercase
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);

        return result;
    }
};

} // namespace ldapintegration::domain::model

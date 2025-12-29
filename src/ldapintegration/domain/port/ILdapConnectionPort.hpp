/**
 * @file ILdapConnectionPort.hpp
 * @brief LDAP Connection Port Interface
 */

#pragma once

#include "ldapintegration/domain/model/DistinguishedName.hpp"
#include "ldapintegration/domain/model/LdapCertificateEntry.hpp"
#include "ldapintegration/domain/model/LdapCrlEntry.hpp"
#include "ldapintegration/domain/model/LdapMasterListEntry.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>

namespace ldapintegration::domain::port {

using namespace ldapintegration::domain::model;

/**
 * @brief LDAP Connection Result
 */
struct LdapOperationResult {
    bool success;
    std::string message;
    int affectedCount;

    static LdapOperationResult ok(int count = 0) {
        return {true, "Operation successful", count};
    }

    static LdapOperationResult error(const std::string& msg) {
        return {false, msg, 0};
    }
};

/**
 * @brief LDAP Search Filter
 */
struct LdapSearchFilter {
    std::string baseDn;
    std::string filter;
    std::vector<std::string> attributes;
    int scope;  // LDAP_SCOPE_BASE, LDAP_SCOPE_ONELEVEL, LDAP_SCOPE_SUBTREE

    static LdapSearchFilter subtree(
        const std::string& baseDn,
        const std::string& filter,
        const std::vector<std::string>& attrs = {}
    ) {
        return {baseDn, filter, attrs, 2};  // LDAP_SCOPE_SUBTREE
    }

    static LdapSearchFilter oneLevel(
        const std::string& baseDn,
        const std::string& filter,
        const std::vector<std::string>& attrs = {}
    ) {
        return {baseDn, filter, attrs, 1};  // LDAP_SCOPE_ONELEVEL
    }

    static LdapSearchFilter base(
        const std::string& dn,
        const std::vector<std::string>& attrs = {}
    ) {
        return {dn, "(objectClass=*)", attrs, 0};  // LDAP_SCOPE_BASE
    }
};

/**
 * @brief LDAP Entry Attribute
 */
struct LdapAttribute {
    std::string name;
    std::vector<std::string> values;
    std::vector<std::vector<uint8_t>> binaryValues;
    bool isBinary;

    static LdapAttribute text(const std::string& name, const std::string& value) {
        return {name, {value}, {}, false};
    }

    static LdapAttribute textMulti(const std::string& name, const std::vector<std::string>& values) {
        return {name, values, {}, false};
    }

    static LdapAttribute binary(const std::string& name, const std::vector<uint8_t>& value) {
        return {name, {}, {value}, true};
    }
};

/**
 * @brief LDAP Entry for search results
 */
struct LdapEntry {
    std::string dn;
    std::vector<LdapAttribute> attributes;

    [[nodiscard]] std::optional<std::string> getAttributeValue(const std::string& name) const {
        for (const auto& attr : attributes) {
            if (attr.name == name && !attr.values.empty()) {
                return attr.values[0];
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<uint8_t>> getBinaryValue(const std::string& name) const {
        for (const auto& attr : attributes) {
            if (attr.name == name && !attr.binaryValues.empty()) {
                return attr.binaryValues[0];
            }
        }
        return std::nullopt;
    }
};

/**
 * @brief LDAP Connection Port Interface
 *
 * Hexagonal Architecture Port for LDAP operations.
 * Infrastructure adapters implement this interface.
 */
class ILdapConnectionPort {
public:
    virtual ~ILdapConnectionPort() = default;

    // ========== Connection Management ==========

    /**
     * @brief Check if connection is available
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Test LDAP connection
     */
    virtual bool testConnection() = 0;

    /**
     * @brief Get connection pool statistics
     */
    virtual std::string getPoolStats() const = 0;

    // ========== Base DN Operations ==========

    /**
     * @brief Get configured base DN
     */
    virtual std::string getBaseDn() const = 0;

    /**
     * @brief Ensure country entry exists
     */
    virtual LdapOperationResult ensureCountryExists(const std::string& countryCode) = 0;

    /**
     * @brief Ensure organizational unit exists
     */
    virtual LdapOperationResult ensureOuExists(LdapEntryType entryType, const std::string& countryCode) = 0;

    // ========== Certificate Operations ==========

    /**
     * @brief Save certificate entry to LDAP
     */
    virtual LdapOperationResult saveCertificate(const LdapCertificateEntry& entry) = 0;

    /**
     * @brief Save multiple certificates in batch
     */
    virtual LdapOperationResult saveCertificates(const std::vector<LdapCertificateEntry>& entries) = 0;

    /**
     * @brief Find certificate by fingerprint
     */
    virtual std::optional<LdapCertificateEntry> findCertificateByFingerprint(
        const std::string& fingerprint,
        LdapEntryType entryType
    ) = 0;

    /**
     * @brief Find certificates by country
     */
    virtual std::vector<LdapCertificateEntry> findCertificatesByCountry(
        const std::string& countryCode,
        LdapEntryType entryType
    ) = 0;

    /**
     * @brief Find certificate by issuer DN
     */
    virtual std::optional<LdapCertificateEntry> findCertificateByIssuerDn(
        const std::string& issuerDn,
        LdapEntryType entryType
    ) = 0;

    /**
     * @brief Delete certificate entry
     */
    virtual LdapOperationResult deleteCertificate(const DistinguishedName& dn) = 0;

    // ========== CRL Operations ==========

    /**
     * @brief Save CRL entry to LDAP
     */
    virtual LdapOperationResult saveCrl(const LdapCrlEntry& entry) = 0;

    /**
     * @brief Find CRL by issuer DN
     */
    virtual std::optional<LdapCrlEntry> findCrlByIssuerDn(const std::string& issuerDn) = 0;

    /**
     * @brief Find CRL by country
     */
    virtual std::vector<LdapCrlEntry> findCrlsByCountry(const std::string& countryCode) = 0;

    /**
     * @brief Update CRL if newer
     * @return true if updated, false if existing is newer
     */
    virtual bool updateCrlIfNewer(const LdapCrlEntry& entry) = 0;

    /**
     * @brief Delete CRL entry
     */
    virtual LdapOperationResult deleteCrl(const DistinguishedName& dn) = 0;

    // ========== Master List Operations ==========

    /**
     * @brief Save Master List entry to LDAP
     */
    virtual LdapOperationResult saveMasterList(const LdapMasterListEntry& entry) = 0;

    /**
     * @brief Find Master List by issuer
     */
    virtual std::optional<LdapMasterListEntry> findMasterListByIssuer(const std::string& issuerDn) = 0;

    /**
     * @brief Find Master Lists by country
     */
    virtual std::vector<LdapMasterListEntry> findMasterListsByCountry(const std::string& countryCode) = 0;

    /**
     * @brief Update Master List if newer version
     */
    virtual bool updateMasterListIfNewer(const LdapMasterListEntry& entry) = 0;

    // ========== Generic Search ==========

    /**
     * @brief Execute LDAP search
     */
    virtual std::vector<LdapEntry> search(const LdapSearchFilter& filter) = 0;

    /**
     * @brief Check if entry exists
     */
    virtual bool entryExists(const std::string& dn) = 0;

    /**
     * @brief Count entries matching filter
     */
    virtual int countEntries(const LdapSearchFilter& filter) = 0;

    // ========== Progress Callback ==========

    using ProgressCallback = std::function<void(int current, int total, const std::string& message)>;

    /**
     * @brief Set progress callback for batch operations
     */
    virtual void setProgressCallback(ProgressCallback callback) = 0;
};

} // namespace ldapintegration::domain::port

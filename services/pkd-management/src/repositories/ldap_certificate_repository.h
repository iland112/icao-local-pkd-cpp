/**
 * @file ldap_certificate_repository.h
 * @brief Repository Interface - LDAP Certificate Data Access
 *
 * Clean Architecture: Repository in Infrastructure Layer
 * Handles LDAP queries and transforms data into domain entities
 */

#pragma once

#include "../domain/models/certificate.h"
#include <ldap.h>
#include <memory>
#include <string>
#include <vector>

namespace repositories {

/**
 * @brief LDAP connection configuration
 */
struct LdapConfig {
    std::string uri;
    std::string bindDn;
    std::string bindPassword;
    std::string baseDn;
    int timeout;

    LdapConfig(
        std::string ldapUri = "ldap://haproxy:389",
        std::string bind_dn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com",
        std::string bind_pwd = "ldap_admin_password",
        std::string base_dn = "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com",
        int timeout_sec = 30
    ) : uri(std::move(ldapUri)),
        bindDn(std::move(bind_dn)),
        bindPassword(std::move(bind_pwd)),
        baseDn(std::move(base_dn)),
        timeout(timeout_sec) {}
};

/**
 * @brief Certificate Repository Interface
 *
 * Defines contract for certificate data access operations.
 * Implementations can be LDAP-based, database-based, or mock for testing.
 */
class ICertificateRepository {
public:
    virtual ~ICertificateRepository() = default;

    /**
     * @brief Search certificates with criteria and pagination
     * @param criteria Search filters and pagination parameters
     * @return Search result with certificates and metadata
     * @throws std::runtime_error on LDAP or parsing errors
     */
    virtual domain::models::CertificateSearchResult search(
        const domain::models::CertificateSearchCriteria& criteria
    ) = 0;

    /**
     * @brief Get certificate details by Distinguished Name
     * @param dn LDAP Distinguished Name
     * @return Certificate entity with all details
     * @throws std::runtime_error if DN not found or parsing fails
     */
    virtual domain::models::Certificate getByDn(const std::string& dn) = 0;

    /**
     * @brief Get certificate binary data (DER format)
     * @param dn LDAP Distinguished Name
     * @return Binary certificate data
     * @throws std::runtime_error if DN not found or attribute missing
     */
    virtual std::vector<uint8_t> getCertificateBinary(const std::string& dn) = 0;

    /**
     * @brief Get all certificate DNs for a country and type
     * @param country ISO 3166-1 alpha-2 code
     * @param certType Certificate type (optional, all types if nullopt)
     * @return List of Distinguished Names
     * @throws std::runtime_error on LDAP errors
     */
    virtual std::vector<std::string> getDnsByCountryAndType(
        const std::string& country,
        std::optional<domain::models::CertificateType> certType = std::nullopt
    ) = 0;
};

/**
 * @brief LDAP-based Certificate Repository Implementation
 *
 * Implements certificate data access using OpenLDAP C API.
 * Transforms LDAP entries into domain Certificate entities.
 */
class LdapCertificateRepository : public ICertificateRepository {
public:
    /**
     * @brief Constructor with LDAP configuration
     * @param config LDAP connection parameters
     */
    explicit LdapCertificateRepository(const LdapConfig& config);

    /**
     * @brief Destructor - ensures LDAP connection cleanup
     */
    ~LdapCertificateRepository() override;

    // Implement interface methods
    domain::models::CertificateSearchResult search(
        const domain::models::CertificateSearchCriteria& criteria
    ) override;

    domain::models::Certificate getByDn(const std::string& dn) override;

    std::vector<uint8_t> getCertificateBinary(const std::string& dn) override;

    std::vector<std::string> getDnsByCountryAndType(
        const std::string& country,
        std::optional<domain::models::CertificateType> certType
    ) override;

private:
    LdapConfig config_;
    LDAP* ldap_;

    /**
     * @brief Initialize and bind LDAP connection
     * @throws std::runtime_error on connection or bind failure
     */
    void connect();

    /**
     * @brief Close LDAP connection
     */
    void disconnect();

    /**
     * @brief Ensure LDAP connection is active, reconnect if needed
     * @throws std::runtime_error on reconnection failure
     */
    void ensureConnected();

    /**
     * @brief Build LDAP search filter from criteria
     * @param criteria Search parameters
     * @return LDAP filter string (e.g., "(&(c=US)(objectClass=cscaCertificateObject))")
     */
    std::string buildSearchFilter(const domain::models::CertificateSearchCriteria& criteria);

    /**
     * @brief Determine base DN for search based on country and certificate type
     * @param country Country code (nullopt for all countries)
     * @param certType Certificate type (nullopt for all types)
     * @return Base DN string
     */
    std::string getSearchBaseDn(
        std::optional<std::string> country,
        std::optional<domain::models::CertificateType> certType
    );

    /**
     * @brief Parse LDAP entry into Certificate domain entity
     * @param entry LDAP entry from search result
     * @param dn Distinguished Name of the entry
     * @return Certificate entity
     * @throws std::runtime_error on parsing errors
     */
    domain::models::Certificate parseEntry(LDAPMessage* entry, const std::string& dn);

    /**
     * @brief Extract certificate type from DN
     * @param dn Distinguished Name
     * @return Certificate type enum
     */
    domain::models::CertificateType extractCertTypeFromDn(const std::string& dn);

    /**
     * @brief Extract country code from DN
     * @param dn Distinguished Name
     * @return ISO 3166-1 alpha-2 country code
     */
    std::string extractCountryFromDn(const std::string& dn);

    /**
     * @brief Parse X.509 certificate binary data using OpenSSL
     * @param derData DER-encoded certificate binary
     * @param cert Output Certificate entity
     * @throws std::runtime_error on OpenSSL parsing errors
     */
    void parseX509Certificate(
        const std::vector<uint8_t>& derData,
        std::string& subjectDn,
        std::string& issuerDn,
        std::string& cn,
        std::string& sn,
        std::string& fingerprint,
        std::chrono::system_clock::time_point& validFrom,
        std::chrono::system_clock::time_point& validTo,
        // X.509 Metadata (v2.3.0)
        int& version,
        std::optional<std::string>& signatureAlgorithm,
        std::optional<std::string>& signatureHashAlgorithm,
        std::optional<std::string>& publicKeyAlgorithm,
        std::optional<int>& publicKeySize,
        std::optional<std::string>& publicKeyCurve,
        std::vector<std::string>& keyUsage,
        std::vector<std::string>& extendedKeyUsage,
        std::optional<bool>& isCA,
        std::optional<int>& pathLenConstraint,
        std::optional<std::string>& subjectKeyIdentifier,
        std::optional<std::string>& authorityKeyIdentifier,
        std::vector<std::string>& crlDistributionPoints,
        std::optional<std::string>& ocspResponderUrl,
        std::optional<bool>& isCertSelfSigned,
        // DN Components (shared library)
        std::optional<icao::x509::DnComponents>& subjectDnComponents,
        std::optional<icao::x509::DnComponents>& issuerDnComponents
    );

    /**
     * @brief Get LDAP attribute value as string
     * @param entry LDAP entry
     * @param attrName Attribute name
     * @return Attribute value (empty string if not found)
     */
    std::string getAttributeValue(LDAPMessage* entry, const char* attrName);

    /**
     * @brief Get LDAP binary attribute value
     * @param entry LDAP entry
     * @param attrName Attribute name (e.g., "userCertificate;binary")
     * @return Binary data vector (empty if not found)
     */
    std::vector<uint8_t> getBinaryAttributeValue(LDAPMessage* entry, const char* attrName);
};

} // namespace repositories

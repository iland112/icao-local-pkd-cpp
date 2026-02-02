/**
 * @file ldap_certificate_repository.h
 * @brief Repository for CSCA/DSC certificates in LDAP
 *
 * Handles LDAP queries for certificate retrieval.
 * Follows Repository Pattern with constructor-based dependency injection.
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <ldap.h>
#include <openssl/x509.h>
#include "icao/x509/dn_parser.h"      // Shared DN Parser
#include "icao/x509/dn_components.h"  // Shared DN Components

namespace repositories {

/**
 * @brief LDAP Certificate Repository
 *
 * Responsibilities:
 * - CSCA certificate retrieval from LDAP
 * - DSC certificate retrieval from LDAP
 * - Link certificate support
 * - X509 certificate parsing from LDAP berval
 */
class LdapCertificateRepository {
private:
    LDAP* ldapConn_;  // Not owned - do not free
    std::string baseDn_;

public:
    /**
     * @brief Constructor with LDAP connection injection
     * @param conn LDAP connection (must remain valid during repository lifetime)
     * @param baseDn Base DN for LDAP searches
     * @throws std::invalid_argument if conn is nullptr
     */
    LdapCertificateRepository(LDAP* conn, const std::string& baseDn);

    /**
     * @brief Destructor
     */
    ~LdapCertificateRepository() = default;

    // Prevent copying
    LdapCertificateRepository(const LdapCertificateRepository&) = delete;
    LdapCertificateRepository& operator=(const LdapCertificateRepository&) = delete;

    // ==========================================================================
    // CSCA Certificate Operations
    // ==========================================================================

    /**
     * @brief Find CSCA certificate by subject DN
     * @param subjectDn Subject DN to search (OpenSSL or RFC2253 format)
     * @param countryCode Country code (e.g., "KR")
     * @return X509* certificate or nullptr if not found (caller must X509_free)
     */
    X509* findCscaBySubjectDn(const std::string& subjectDn, const std::string& countryCode);

    /**
     * @brief Find all CSCA certificates for a country (including link certificates)
     * @param countryCode Country code
     * @return Vector of X509* certificates (caller must X509_free each)
     */
    std::vector<X509*> findAllCscasByCountry(const std::string& countryCode);

    /**
     * @brief Find CSCA certificate by issuer DN (for DSC validation)
     * @param issuerDn Issuer DN from DSC certificate
     * @param countryCode Country code
     * @return X509* certificate or nullptr (caller must X509_free)
     */
    X509* findCscaByIssuerDn(const std::string& issuerDn, const std::string& countryCode);

    // ==========================================================================
    // DSC Certificate Operations (if needed for future use)
    // ==========================================================================

    /**
     * @brief Find DSC certificate by subject DN
     * @param subjectDn Subject DN
     * @param countryCode Country code
     * @return X509* certificate or nullptr (caller must X509_free)
     */
    X509* findDscBySubjectDn(const std::string& subjectDn, const std::string& countryCode);

    // ==========================================================================
    // Helper Methods
    // ==========================================================================

    /**
     * @brief Build LDAP filter for certificate search
     * @param type Certificate type ("csca", "dsc", "mlsc")
     * @param countryCode Country code
     * @param subjectDn Optional subject DN filter
     * @return LDAP filter string
     */
    std::string buildLdapFilter(
        const std::string& type,
        const std::string& countryCode,
        const std::string& subjectDn = ""
    );

    /**
     * @brief Build LDAP base DN for search
     * @param type Certificate type ("csca", "dsc", "mlsc")
     * @param countryCode Country code
     * @return LDAP base DN string
     */
    std::string buildSearchBaseDn(const std::string& type, const std::string& countryCode);

    /**
     * @brief Escape LDAP filter value to prevent injection (RFC 4515)
     * @param value Raw value to escape
     * @return Escaped value safe for LDAP filter
     */
    std::string escapeLdapFilterValue(const std::string& value);

    /**
     * @brief Parse X509 certificate from LDAP berval
     * @param certData LDAP berval containing certificate data
     * @return X509* certificate or nullptr on error (caller must X509_free)
     */
    X509* parseCertificateFromLdap(struct berval** certData);

    /**
     * @brief Extract DN components for comparison
     * @param dn Subject or Issuer DN
     * @param attr Attribute name (e.g., "CN", "C", "O")
     * @return Attribute value or empty string
     */
    std::string extractDnAttribute(const std::string& dn, const std::string& attr);

    /**
     * @brief Normalize DN for comparison (format-independent)
     * @param dn DN string
     * @return Normalized DN (lowercase, sorted components)
     */
    std::string normalizeDn(const std::string& dn);

private:
    /**
     * @brief Execute LDAP search
     * @param baseDn Base DN for search
     * @param filter LDAP filter
     * @param attrs Attributes to retrieve
     * @return LDAPMessage* (caller must ldap_msgfree)
     */
    LDAPMessage* executeLdapSearch(
        const std::string& baseDn,
        const std::string& filter,
        const std::vector<std::string>& attrs
    );

    /**
     * @brief Extract certificates from LDAP search result
     * @param msg LDAP search result message
     * @return Vector of X509* certificates (caller must X509_free each)
     */
    std::vector<X509*> extractCertificatesFromResult(LDAPMessage* msg);
};

} // namespace repositories

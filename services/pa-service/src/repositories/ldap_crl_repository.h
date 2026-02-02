/**
 * @file ldap_crl_repository.h
 * @brief Repository for CRL (Certificate Revocation List) in LDAP
 *
 * Handles LDAP queries for CRL retrieval and revocation checking.
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

namespace repositories {

/**
 * @brief LDAP CRL Repository
 *
 * Responsibilities:
 * - CRL retrieval from LDAP by country
 * - Certificate revocation checking
 * - CRL expiration validation
 */
class LdapCrlRepository {
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
    LdapCrlRepository(LDAP* conn, const std::string& baseDn);

    /**
     * @brief Destructor
     */
    ~LdapCrlRepository() = default;

    // Prevent copying
    LdapCrlRepository(const LdapCrlRepository&) = delete;
    LdapCrlRepository& operator=(const LdapCrlRepository&) = delete;

    // ==========================================================================
    // CRL Operations
    // ==========================================================================

    /**
     * @brief Find CRL by country code
     * @param countryCode Country code (e.g., "KR")
     * @return X509_CRL* or nullptr if not found (caller must X509_CRL_free)
     */
    X509_CRL* findCrlByCountry(const std::string& countryCode);

    /**
     * @brief Find CRL by issuer DN
     * @param issuerDn Issuer DN
     * @param countryCode Country code
     * @return X509_CRL* or nullptr (caller must X509_CRL_free)
     */
    X509_CRL* findCrlByIssuer(const std::string& issuerDn, const std::string& countryCode);

    /**
     * @brief Check if certificate is revoked in CRL
     * @param cert Certificate to check
     * @param crl CRL to check against
     * @return true if certificate is revoked
     */
    bool isCertificateRevoked(X509* cert, X509_CRL* crl);

    /**
     * @brief Check if CRL is expired
     * @param crl CRL to check
     * @return true if CRL is expired
     */
    bool isCrlExpired(X509_CRL* crl);

    /**
     * @brief Get CRL expiration status
     * @param crl CRL to check
     * @return "VALID", "EXPIRED", or "UNKNOWN"
     */
    std::string getCrlExpirationStatus(X509_CRL* crl);

    // ==========================================================================
    // Helper Methods
    // ==========================================================================

    /**
     * @brief Build LDAP filter for CRL search
     * @param countryCode Country code
     * @return LDAP filter string
     */
    std::string buildCrlFilter(const std::string& countryCode);

    /**
     * @brief Build LDAP base DN for CRL search
     * @param countryCode Country code
     * @return LDAP base DN string
     */
    std::string buildCrlSearchBaseDn(const std::string& countryCode);

    /**
     * @brief Normalize DN for comparison (lowercase, remove spaces)
     * @param dn Distinguished Name to normalize
     * @return Normalized DN string
     */
    std::string normalizeDn(const std::string& dn);

    /**
     * @brief Parse X509_CRL from LDAP berval
     * @param crlData LDAP berval containing CRL data
     * @return X509_CRL* or nullptr on error (caller must X509_CRL_free)
     */
    X509_CRL* parseCrlFromLdap(struct berval** crlData);

private:
    /**
     * @brief Execute LDAP search for CRL
     * @param baseDn Base DN for search
     * @param filter LDAP filter
     * @return LDAPMessage* (caller must ldap_msgfree)
     */
    LDAPMessage* executeCrlSearch(
        const std::string& baseDn,
        const std::string& filter
    );

    /**
     * @brief Extract CRL from LDAP search result
     * @param msg LDAP search result message
     * @return X509_CRL* or nullptr (caller must X509_CRL_free)
     */
    X509_CRL* extractCrlFromResult(LDAPMessage* msg);
};

} // namespace repositories

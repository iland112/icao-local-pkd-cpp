/**
 * @file providers.h
 * @brief Provider interfaces for infrastructure abstraction
 *
 * These interfaces decouple the validation library from specific data sources.
 * Each service implements concrete adapters:
 *   - PKD Management: DbCscaProvider (CertificateRepository), DbCrlProvider (CrlRepository)
 *   - PA Service: LdapCscaProvider (LdapCertificateRepository), LdapCrlProvider (LdapCrlRepository)
 */

#pragma once

#include <string>
#include <vector>
#include <openssl/x509.h>

namespace icao::validation {

/**
 * @brief CSCA certificate lookup interface
 *
 * Abstracts CSCA retrieval from DB or LDAP.
 * Implementations must handle multi-CSCA scenarios (key rollover).
 *
 * Memory ownership: returned X509* pointers are owned by the caller and must be freed.
 */
class ICscaProvider {
public:
    virtual ~ICscaProvider() = default;

    /**
     * @brief Find all CSCAs matching an issuer DN (for key rollover support)
     *
     * ICAO Doc 9303 Part 12: Multiple CSCAs may share the same DN when
     * a country performs key rollover. The TrustChainBuilder will select
     * the correct one by signature verification.
     *
     * @param issuerDn Issuer DN to search for
     * @return Vector of X509* certificates (caller must free each)
     */
    virtual std::vector<X509*> findAllCscasByIssuerDn(const std::string& issuerDn) = 0;

    /**
     * @brief Find a single CSCA by issuer DN (with optional country code)
     * @param issuerDn Issuer DN to search for
     * @param countryCode Optional country code filter
     * @return X509* certificate (caller must free), or nullptr if not found
     */
    virtual X509* findCscaByIssuerDn(
        const std::string& issuerDn,
        const std::string& countryCode = "") = 0;
};

/**
 * @brief CRL lookup interface
 *
 * Abstracts CRL retrieval from DB or LDAP.
 *
 * Memory ownership: returned X509_CRL* pointers are owned by the caller and must be freed.
 */
class ICrlProvider {
public:
    virtual ~ICrlProvider() = default;

    /**
     * @brief Find CRL by country code
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @return X509_CRL* (caller must free), or nullptr if not found
     */
    virtual X509_CRL* findCrlByCountry(const std::string& countryCode) = 0;
};

} // namespace icao::validation

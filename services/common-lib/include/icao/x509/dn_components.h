/**
 * @file dn_components.h
 * @brief Structured DN component extraction
 *
 * Provides type-safe access to individual DN components using OpenSSL NIDs.
 * Eliminates error-prone regex parsing of DN strings.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <openssl/x509.h>

namespace icao {
namespace x509 {

/**
 * @brief Structured representation of DN components
 *
 * Extracts individual DN fields from X509_NAME structure.
 * All fields are optional as not all certificates include every component.
 */
struct DnComponents {
    std::optional<std::string> commonName;           ///< CN (Common Name)
    std::optional<std::string> organization;         ///< O (Organization)
    std::optional<std::string> organizationalUnit;   ///< OU (Organizational Unit)
    std::optional<std::string> locality;             ///< L (Locality/City)
    std::optional<std::string> stateOrProvince;      ///< ST (State or Province)
    std::optional<std::string> country;              ///< C (Country, ISO 3166-1 alpha-2)
    std::optional<std::string> email;                ///< emailAddress
    std::optional<std::string> serialNumber;         ///< serialNumber
    std::optional<std::string> title;                ///< title
    std::optional<std::string> givenName;            ///< GN (Given Name)
    std::optional<std::string> surname;              ///< SN (Surname)
    std::optional<std::string> pseudonym;            ///< pseudonym

    /**
     * @brief Check if all components are empty
     * @return true if no components are set
     */
    bool isEmpty() const;

    /**
     * @brief Get full DN in RFC2253 format
     *
     * Reconstructs DN string from components.
     * Only includes non-empty components.
     *
     * @return DN string like "CN=Name,O=Org,C=XX"
     */
    std::string toRfc2253() const;

    /**
     * @brief Get human-readable display name
     *
     * Returns the most appropriate name for display:
     * 1. Common Name (CN)
     * 2. Organization (O)
     * 3. Email address
     * 4. "Unknown"
     *
     * @return Best available name for UI display
     */
    std::string getDisplayName() const;
};

/**
 * @brief Extract all DN components from X509_NAME structure
 *
 * Uses OpenSSL X509_NAME_get_index_by_NID and X509_NAME_ENTRY_get_data
 * for reliable component extraction.
 *
 * @param name X509_NAME structure
 * @return DnComponents with all available fields populated
 *
 * @example
 * X509_NAME* subject = X509_get_subject_name(cert);
 * DnComponents components = extractDnComponents(subject);
 * if (components.commonName) {
 *     std::cout << "CN: " << *components.commonName << std::endl;
 * }
 */
DnComponents extractDnComponents(X509_NAME* name);

/**
 * @brief Extract subject DN components from certificate
 *
 * Convenience wrapper for X509_get_subject_name + extractDnComponents.
 *
 * @param cert X509 certificate
 * @return DnComponents for certificate subject
 */
DnComponents extractSubjectComponents(X509* cert);

/**
 * @brief Extract issuer DN components from certificate
 *
 * Convenience wrapper for X509_get_issuer_name + extractDnComponents.
 *
 * @param cert X509 certificate
 * @return DnComponents for certificate issuer
 */
DnComponents extractIssuerComponents(X509* cert);

/**
 * @brief Get specific DN component by NID
 *
 * Low-level function to extract a single component by OpenSSL NID.
 *
 * @param name X509_NAME structure
 * @param nid OpenSSL NID (e.g., NID_commonName, NID_organizationName)
 * @return Component value, or std::nullopt if not present
 *
 * @example
 * auto cn = getDnComponentByNid(subject, NID_commonName);
 * auto country = getDnComponentByNid(subject, NID_countryName);
 */
std::optional<std::string> getDnComponentByNid(X509_NAME* name, int nid);

/**
 * @brief Get all values for multi-valued DN component
 *
 * Some DN components can have multiple values (e.g., multiple OUs).
 * This function returns all values for a given NID.
 *
 * @param name X509_NAME structure
 * @param nid OpenSSL NID
 * @return Vector of all values for the component (empty if none)
 *
 * @example
 * auto ous = getDnComponentAllValues(subject, NID_organizationalUnitName);
 * // Returns: ["Division A", "Department B"]
 */
std::vector<std::string> getDnComponentAllValues(X509_NAME* name, int nid);

} // namespace x509
} // namespace icao

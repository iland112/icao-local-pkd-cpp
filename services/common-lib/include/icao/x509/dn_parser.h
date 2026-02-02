/**
 * @file dn_parser.h
 * @brief X.509 Distinguished Name (DN) parsing utilities
 *
 * ICAO-compliant DN parsing using OpenSSL ASN.1 structures.
 * Provides RFC2253 and OpenSSL oneline format support.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <optional>
#include <openssl/x509.h>

namespace icao {
namespace x509 {

/**
 * @brief DN output format options
 */
enum class DnFormat {
    RFC2253,        ///< RFC2253 format: CN=Name,O=Org,C=XX
    ONELINE,        ///< OpenSSL oneline: /C=XX/O=Org/CN=Name
    MULTILINE       ///< Multi-line format for debugging
};

/**
 * @brief Convert X509_NAME to string with specified format
 *
 * Uses OpenSSL X509_NAME_print_ex for proper ASN.1 structure handling.
 * This is the recommended approach from ICAO DN processing guide.
 *
 * @param name X509_NAME structure (from X509_get_subject_name or X509_get_issuer_name)
 * @param format Desired output format (default: RFC2253)
 * @return String representation of DN, or std::nullopt on error
 *
 * @example
 * X509_NAME* subject = X509_get_subject_name(cert);
 * auto dn = x509NameToString(subject, DnFormat::RFC2253);
 * // Returns: "CN=CSCA Latvia,O=National Security Authority,C=LV"
 */
std::optional<std::string> x509NameToString(
    X509_NAME* name,
    DnFormat format = DnFormat::RFC2253
);

/**
 * @brief Compare two X509_NAME structures for equality
 *
 * Uses OpenSSL X509_NAME_cmp for proper ASN.1 structure comparison.
 * This is more reliable than string comparison due to encoding differences.
 *
 * @param name1 First X509_NAME structure
 * @param name2 Second X509_NAME structure
 * @return true if names are identical, false otherwise
 *
 * @note This performs exact match including component order.
 *       For normalized comparison, use normalizeDnForComparison.
 */
bool compareX509Names(X509_NAME* name1, X509_NAME* name2);

/**
 * @brief Normalize DN string for format-independent comparison
 *
 * Extracts RDN components (C, O, OU, CN, serialNumber), converts to lowercase,
 * sorts alphabetically, and joins with '|' separator.
 *
 * @param dn DN string in any format (RFC2253 or oneline)
 * @return Normalized DN for comparison, or std::nullopt on error
 *
 * @example
 * auto norm1 = normalizeDnForComparison("CN=Test,O=Org,C=US");
 * auto norm2 = normalizeDnForComparison("/C=US/O=Org/CN=Test");
 * // Both return: "c=us|cn=test|o=org"
 *
 * @note This is useful for finding matching certificates when DN format
 *       differs between database and LDAP storage.
 */
std::optional<std::string> normalizeDnForComparison(const std::string& dn);

/**
 * @brief Parse DN string into X509_NAME structure
 *
 * Converts string DN back into OpenSSL X509_NAME for use with X509 APIs.
 * Supports both RFC2253 and oneline formats.
 *
 * @param dn DN string to parse
 * @return X509_NAME structure (caller must free with X509_NAME_free),
 *         or nullptr on error
 *
 * @warning Caller is responsible for freeing returned X509_NAME
 */
X509_NAME* parseDnString(const std::string& dn);

/**
 * @brief Extract subject DN from certificate
 *
 * Convenience wrapper for X509_get_subject_name + x509NameToString.
 *
 * @param cert X509 certificate
 * @param format Desired output format (default: RFC2253)
 * @return Subject DN string, or std::nullopt on error
 */
std::optional<std::string> getSubjectDn(
    X509* cert,
    DnFormat format = DnFormat::RFC2253
);

/**
 * @brief Extract issuer DN from certificate
 *
 * Convenience wrapper for X509_get_issuer_name + x509NameToString.
 *
 * @param cert X509 certificate
 * @param format Desired output format (default: RFC2253)
 * @return Issuer DN string, or std::nullopt on error
 */
std::optional<std::string> getIssuerDn(
    X509* cert,
    DnFormat format = DnFormat::RFC2253
);

/**
 * @brief Check if certificate is self-signed
 *
 * Compares subject and issuer using X509_NAME_cmp for accuracy.
 *
 * @param cert X509 certificate
 * @return true if subject DN equals issuer DN
 */
bool isSelfSigned(X509* cert);

} // namespace x509
} // namespace icao

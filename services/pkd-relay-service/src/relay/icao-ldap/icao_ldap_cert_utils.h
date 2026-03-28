/**
 * @file icao_ldap_cert_utils.h
 * @brief Testable certificate utilities extracted from icao_ldap_sync_service.cpp
 *
 * Functions that were previously in an anonymous namespace are promoted to this
 * named namespace so that unit tests can link against them directly without
 * pulling in the entire IcaoLdapSyncService compilation unit (which brings in
 * Drogon, LDAP, and database headers).
 */
#pragma once

#include <string>

// Forward declaration — avoids pulling in <openssl/x509.h> from headers
typedef struct x509_st X509;

namespace icao {
namespace relay {
namespace cert_utils {

/**
 * @brief Extract the country code (C= attribute) from an X.509 certificate's
 *        subject Distinguished Name.
 *
 * Uses OpenSSL's NID_countryName lookup so it handles all ASN.1 string types
 * (UTF8String, PrintableString, T61String) correctly.
 *
 * @param cert     OpenSSL X509 object.  Must not be nullptr.
 * @param fallback Value returned when the C= field is absent or empty.
 *                 Defaults to "XX".
 * @return Two-letter ISO 3166-1 alpha-2 country code, or @p fallback.
 */
std::string extractCountryFromCert(X509* cert, const std::string& fallback = "XX");

} // namespace cert_utils
} // namespace relay
} // namespace icao

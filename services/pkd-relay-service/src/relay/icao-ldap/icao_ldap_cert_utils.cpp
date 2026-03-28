/**
 * @file icao_ldap_cert_utils.cpp
 * @brief Implementation of testable certificate utilities
 *
 * This file provides the implementation that was previously locked inside an
 * anonymous namespace in icao_ldap_sync_service.cpp.  Moving it here lets
 * unit tests link the function directly without the full sync-service binary.
 */
#include "icao_ldap_cert_utils.h"

#include <openssl/x509.h>
#include <openssl/asn1.h>

namespace icao {
namespace relay {
namespace cert_utils {

std::string extractCountryFromCert(X509* cert, const std::string& fallback) {
    if (!cert) return fallback;

    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) return fallback;

    int idx = X509_NAME_get_index_by_NID(subject, NID_countryName, -1);
    if (idx < 0) return fallback;

    X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, idx);
    if (!entry) return fallback;

    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) return fallback;

    unsigned char* utf8 = nullptr;
    int len = ASN1_STRING_to_UTF8(&utf8, data);
    if (len <= 0 || !utf8) return fallback;

    std::string cc(reinterpret_cast<char*>(utf8), static_cast<size_t>(len));
    OPENSSL_free(utf8);
    return cc.empty() ? fallback : cc;
}

} // namespace cert_utils
} // namespace relay
} // namespace icao

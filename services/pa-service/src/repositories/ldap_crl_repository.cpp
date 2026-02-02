/**
 * @file ldap_crl_repository.cpp
 * @brief Implementation of LdapCrlRepository
 */

#include "ldap_crl_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace repositories {

LdapCrlRepository::LdapCrlRepository(LDAP* conn, const std::string& baseDn)
    : ldapConn_(conn), baseDn_(baseDn)
{
    if (!ldapConn_) {
        throw std::invalid_argument("LDAP connection cannot be null");
    }

    if (baseDn_.empty()) {
        throw std::invalid_argument("Base DN cannot be empty");
    }

    spdlog::debug("LdapCrlRepository initialized with baseDn: {}", baseDn_);
}

// ==========================================================================
// CRL Operations
// ==========================================================================

X509_CRL* LdapCrlRepository::findCrlByCountry(const std::string& countryCode) {
    spdlog::debug("Finding CRL for country: {}", countryCode);

    std::string baseDn = buildCrlSearchBaseDn(countryCode);
    std::string filter = buildCrlFilter(countryCode);

    LDAPMessage* res = executeCrlSearch(baseDn, filter);
    if (!res) {
        spdlog::debug("No CRL found for country: {}", countryCode);
        return nullptr;
    }

    X509_CRL* crl = extractCrlFromResult(res);
    ldap_msgfree(res);

    if (crl) {
        spdlog::info("Found CRL for country: {}", countryCode);
    }

    return crl;
}

X509_CRL* LdapCrlRepository::findCrlByIssuer(
    const std::string& issuerDn,
    const std::string& countryCode)
{
    spdlog::debug("Finding CRL by issuer: {}, country: {}", issuerDn, countryCode);

    // Search for CRLs in the country
    std::string baseDn = buildCrlSearchBaseDn(countryCode);
    std::string filter = buildCrlFilter(countryCode);

    std::vector<std::string> attrs = {"certificateRevocationList;binary"};
    LDAPMessage* res = executeCrlSearch(baseDn, filter);

    if (!res) {
        spdlog::warn("No CRLs found for country: {}", countryCode);
        return nullptr;
    }

    // Extract all CRLs and find matching issuer
    X509_CRL* bestMatch = nullptr;
    LDAPMessage* entry = ldap_first_entry(ldapConn_, res);

    while (entry) {
        struct berval** crlData = ldap_get_values_len(ldapConn_, entry, "certificateRevocationList;binary");
        if (crlData) {
            X509_CRL* crl = parseCrlFromLdap(crlData);
            if (crl) {
                // Check if issuer matches
                char* crlIssuerDn = X509_NAME_oneline(X509_CRL_get_issuer(crl), nullptr, 0);
                if (crlIssuerDn) {
                    std::string crlIssuer(crlIssuerDn);
                    OPENSSL_free(crlIssuerDn);

                    // Normalize and compare DNs
                    if (normalizeDn(crlIssuer) == normalizeDn(issuerDn)) {
                        spdlog::debug("Found matching CRL for issuer: {}", issuerDn);
                        bestMatch = crl;
                        ldap_value_free_len(crlData);
                        break;
                    }
                }
                X509_CRL_free(crl);
            }
            ldap_value_free_len(crlData);
        }
        entry = ldap_next_entry(ldapConn_, entry);
    }

    ldap_msgfree(res);

    if (!bestMatch) {
        spdlog::warn("No CRL found matching issuer: {}", issuerDn);
    }

    return bestMatch;
}

bool LdapCrlRepository::isCertificateRevoked(X509* cert, X509_CRL* crl) {
    if (!cert || !crl) {
        spdlog::warn("Cannot check revocation: cert or CRL is null");
        return false;
    }

    // Get certificate serial number
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    if (!serial) {
        spdlog::error("Failed to get certificate serial number");
        return false;
    }

    // Get revoked certificates list from CRL
    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
    if (!revoked) {
        spdlog::debug("CRL has no revoked certificates");
        return false;
    }

    // Check if certificate serial is in revoked list
    for (int i = 0; i < sk_X509_REVOKED_num(revoked); i++) {
        X509_REVOKED* rev = sk_X509_REVOKED_value(revoked, i);
        const ASN1_INTEGER* revokedSerial = X509_REVOKED_get0_serialNumber(rev);

        if (ASN1_INTEGER_cmp(serial, revokedSerial) == 0) {
            spdlog::warn("Certificate is REVOKED (serial matches CRL entry)");
            return true;
        }
    }

    spdlog::debug("Certificate is NOT revoked");
    return false;
}

bool LdapCrlRepository::isCrlExpired(X509_CRL* crl) {
    if (!crl) {
        return true;
    }

    const ASN1_TIME* nextUpdate = X509_CRL_get0_nextUpdate(crl);
    if (!nextUpdate) {
        spdlog::warn("CRL has no nextUpdate field");
        return true;
    }

    int days, seconds;
    ASN1_TIME_diff(&days, &seconds, nullptr, nextUpdate);

    // If diff is negative, CRL is expired
    bool expired = (days < 0 || (days == 0 && seconds < 0));

    if (expired) {
        spdlog::warn("CRL is EXPIRED");
    }

    return expired;
}

std::string LdapCrlRepository::getCrlExpirationStatus(X509_CRL* crl) {
    if (!crl) {
        return "UNKNOWN";
    }

    if (isCrlExpired(crl)) {
        return "EXPIRED";
    }

    return "VALID";
}

// ==========================================================================
// Helper Methods
// ==========================================================================

std::string LdapCrlRepository::buildCrlFilter(const std::string& countryCode) {
    // Build compound LDAP filter for CRL entries
    // Note: countryCode is handled via base DN in buildCrlSearchBaseDn()
    // This filter is primarily for object class selection
    return "(objectClass=pkdDownload)";
}

std::string LdapCrlRepository::buildCrlSearchBaseDn(const std::string& countryCode) {
    std::ostringstream oss;
    oss << "o=crl,c=" << countryCode << ",dc=data," << baseDn_;
    return oss.str();
}

X509_CRL* LdapCrlRepository::parseCrlFromLdap(struct berval** crlData) {
    if (!crlData || !crlData[0]) {
        return nullptr;
    }

    const unsigned char* data = reinterpret_cast<const unsigned char*>(crlData[0]->bv_val);
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, crlData[0]->bv_len);

    if (!crl) {
        spdlog::error("Failed to parse X509_CRL from LDAP data");
    }

    return crl;
}

std::string LdapCrlRepository::normalizeDn(const std::string& dn) {
    // Normalize DN for comparison: lowercase and remove spaces
    std::string normalized;
    normalized.reserve(dn.length());

    for (char c : dn) {
        if (c != ' ' && c != '\t') {
            normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return normalized;
}

// ==========================================================================
// Private Helper Methods
// ==========================================================================

LDAPMessage* LdapCrlRepository::executeCrlSearch(
    const std::string& baseDn,
    const std::string& filter)
{
    spdlog::debug("LDAP CRL search: base={}, filter={}", baseDn, filter);

    char* attrs[] = {
        const_cast<char*>("certificateRevocationList;binary"),
        nullptr
    };

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(
        ldapConn_,
        baseDn.c_str(),
        LDAP_SCOPE_SUBTREE,
        filter.c_str(),
        attrs,
        0,
        nullptr,
        nullptr,
        nullptr,
        10,  // Size limit
        &res
    );

    if (rc != LDAP_SUCCESS) {
        spdlog::debug("LDAP CRL search failed: {} ({})", ldap_err2string(rc), baseDn);
        if (res) ldap_msgfree(res);
        return nullptr;
    }

    int count = ldap_count_entries(ldapConn_, res);
    spdlog::debug("LDAP CRL search returned {} entries", count);

    if (count == 0) {
        ldap_msgfree(res);
        return nullptr;
    }

    return res;
}

X509_CRL* LdapCrlRepository::extractCrlFromResult(LDAPMessage* msg) {
    LDAPMessage* entry = ldap_first_entry(ldapConn_, msg);
    if (!entry) {
        return nullptr;
    }

    struct berval** values = ldap_get_values_len(ldapConn_, entry, "certificateRevocationList;binary");
    if (!values || !values[0]) {
        return nullptr;
    }

    X509_CRL* crl = parseCrlFromLdap(values);
    ldap_value_free_len(values);

    return crl;
}

} // namespace repositories

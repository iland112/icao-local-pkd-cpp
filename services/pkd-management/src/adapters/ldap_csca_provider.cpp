/**
 * @file ldap_csca_provider.cpp
 * @brief ICscaProvider adapter — LDAP pool-based CSCA lookup
 *
 * Searches o=csca and o=lc branches in LDAP for each country.
 * Each call acquires/releases a connection from the pool (RAII).
 */

#include "ldap_csca_provider.h"
#include <icao/validation/cert_ops.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace adapters {

LdapCscaProvider::LdapCscaProvider(common::LdapConnectionPool* ldapPool, const std::string& baseDn)
    : ldapPool_(ldapPool), baseDn_(baseDn)
{
    if (!ldapPool_) {
        throw std::invalid_argument("LdapCscaProvider: ldapPool cannot be nullptr");
    }
    if (baseDn_.empty()) {
        throw std::invalid_argument("LdapCscaProvider: baseDn cannot be empty");
    }
    spdlog::info("[LdapCscaProvider] Initialized (baseDn={})", baseDn_);
}

std::vector<X509*> LdapCscaProvider::findAllCscasByIssuerDn(const std::string& issuerDn) {
    std::string countryCode = extractCountryFromDn(issuerDn);
    if (countryCode.empty()) {
        spdlog::warn("[LdapCscaProvider] Cannot extract country from issuer DN: {}", issuerDn);
        return {};
    }

    // Acquire LDAP connection from pool (RAII)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        spdlog::error("[LdapCscaProvider] Failed to acquire LDAP connection");
        return {};
    }

    LDAP* ldap = conn.get();
    std::vector<X509*> allCerts;

    // Search o=csca branch
    auto cscaCerts = searchCscas(ldap, "csca", countryCode);
    allCerts.insert(allCerts.end(), cscaCerts.begin(), cscaCerts.end());

    // Search o=lc branch (link certificates)
    auto lcCerts = searchCscas(ldap, "lc", countryCode);
    allCerts.insert(allCerts.end(), lcCerts.begin(), lcCerts.end());

    spdlog::debug("[LdapCscaProvider] Found {} CSCAs for country {} (issuer: {})",
                  allCerts.size(), countryCode, issuerDn.substr(0, 60));
    return allCerts;
}

X509* LdapCscaProvider::findCscaByIssuerDn(
    const std::string& issuerDn, const std::string& countryCode)
{
    std::string cc = countryCode;
    if (cc.empty()) {
        cc = extractCountryFromDn(issuerDn);
    }
    if (cc.empty()) {
        return nullptr;
    }

    auto allCscas = findAllCscasByIssuerDn(issuerDn);
    if (allCscas.empty()) {
        return nullptr;
    }

    // Return first match, free the rest
    X509* result = allCscas[0];
    for (size_t i = 1; i < allCscas.size(); i++) {
        X509_free(allCscas[i]);
    }
    return result;
}

std::vector<X509*> LdapCscaProvider::searchCscas(
    LDAP* conn, const std::string& ou, const std::string& countryCode)
{
    std::vector<X509*> certs;

    // Build search base DN: o={ou},c={CC},dc=data,{baseDn}
    std::ostringstream baseDnStream;
    baseDnStream << "o=" << ou << ",c=" << countryCode << ",dc=data," << baseDn_;
    std::string searchBaseDn = baseDnStream.str();

    std::string filter = "(objectClass=pkdDownload)";
    char* attrs[] = {const_cast<char*>("userCertificate;binary"), nullptr};

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(
        conn,
        searchBaseDn.c_str(),
        LDAP_SCOPE_SUBTREE,
        filter.c_str(),
        attrs,
        0,
        nullptr, nullptr, nullptr,
        500,  // Size limit
        &res
    );

    if (rc != LDAP_SUCCESS) {
        spdlog::debug("[LdapCscaProvider] LDAP search failed for {}: {}", searchBaseDn, ldap_err2string(rc));
        if (res) ldap_msgfree(res);
        return certs;
    }

    // Extract certificates from result
    LDAPMessage* entry = ldap_first_entry(conn, res);
    while (entry) {
        struct berval** values = ldap_get_values_len(conn, entry, "userCertificate;binary");
        if (values && values[0]) {
            const unsigned char* data = reinterpret_cast<const unsigned char*>(values[0]->bv_val);
            X509* cert = d2i_X509(nullptr, &data, values[0]->bv_len);
            if (cert) {
                certs.push_back(cert);
            }
            ldap_value_free_len(values);
        }
        entry = ldap_next_entry(conn, entry);
    }

    ldap_msgfree(res);
    return certs;
}

std::string LdapCscaProvider::extractCountryFromDn(const std::string& dn) {
    // Use shared library for robust extraction
    std::string cc = icao::validation::extractDnAttribute(dn, "C");
    // Uppercase
    for (char& c : cc) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return cc;
}

} // namespace adapters

/**
 * @file ldap_crl_provider.cpp
 * @brief ICrlProvider adapter — LDAP pool-based CRL lookup
 *
 * Searches o=crl,c={CC},dc=data branch in LDAP.
 * Each call acquires/releases a connection from the pool (RAII).
 */

#include "ldap_crl_provider.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>

namespace adapters {

LdapCrlProvider::LdapCrlProvider(common::LdapConnectionPool* ldapPool, const std::string& baseDn)
    : ldapPool_(ldapPool), baseDn_(baseDn)
{
    if (!ldapPool_) {
        throw std::invalid_argument("LdapCrlProvider: ldapPool cannot be nullptr");
    }
    if (baseDn_.empty()) {
        throw std::invalid_argument("LdapCrlProvider: baseDn cannot be empty");
    }
    spdlog::info("[LdapCrlProvider] Initialized (baseDn={})", baseDn_);
}

X509_CRL* LdapCrlProvider::findCrlByCountry(const std::string& countryCode) {
    if (countryCode.empty()) {
        return nullptr;
    }

    // Acquire LDAP connection from pool (RAII)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        spdlog::error("[LdapCrlProvider] Failed to acquire LDAP connection");
        return nullptr;
    }

    // Build search base DN: o=crl,c={CC},dc=data,{baseDn}
    std::ostringstream baseDnStream;
    baseDnStream << "o=crl,c=" << countryCode << ",dc=data," << baseDn_;
    std::string searchBaseDn = baseDnStream.str();

    std::string filter = "(objectClass=pkdDownload)";
    char* attrs[] = {const_cast<char*>("certificateRevocationList;binary"), nullptr};

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(
        conn.get(),
        searchBaseDn.c_str(),
        LDAP_SCOPE_SUBTREE,
        filter.c_str(),
        attrs,
        0,
        nullptr, nullptr, nullptr,
        10,  // Size limit
        &res
    );

    if (rc != LDAP_SUCCESS) {
        spdlog::debug("[LdapCrlProvider] No CRL found for country {}: {}", countryCode, ldap_err2string(rc));
        if (res) ldap_msgfree(res);
        return nullptr;
    }

    // Extract first CRL from result
    X509_CRL* crl = nullptr;
    LDAPMessage* entry = ldap_first_entry(conn.get(), res);
    if (entry) {
        struct berval** values = ldap_get_values_len(conn.get(), entry, "certificateRevocationList;binary");
        if (values && values[0]) {
            const unsigned char* data = reinterpret_cast<const unsigned char*>(values[0]->bv_val);
            crl = d2i_X509_CRL(nullptr, &data, values[0]->bv_len);
            if (!crl) {
                spdlog::error("[LdapCrlProvider] Failed to parse CRL from LDAP for country {}", countryCode);
            }
            ldap_value_free_len(values);
        }
    }

    ldap_msgfree(res);

    if (crl) {
        spdlog::debug("[LdapCrlProvider] Found CRL for country {}", countryCode);
    }

    return crl;
}

} // namespace adapters

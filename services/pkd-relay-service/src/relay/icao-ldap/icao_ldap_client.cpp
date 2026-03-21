/**
 * @file icao_ldap_client.cpp
 * @brief LDAP V3 client implementation for ICAO PKD
 */
#include "icao_ldap_client.h"
#include <spdlog/spdlog.h>
#include <ldap.h>
#include <regex>

namespace icao {
namespace relay {

IcaoLdapClient::IcaoLdapClient(const std::string& host, int port,
                               const std::string& bindDn, const std::string& bindPassword,
                               const std::string& baseDn)
    : host_(host), port_(port), bindDn_(bindDn), bindPassword_(bindPassword), baseDn_(baseDn)
{}

IcaoLdapClient::~IcaoLdapClient() {
    disconnect();
}

bool IcaoLdapClient::connect() {
    if (ldap_) disconnect();

    std::string uri = "ldap://" + host_ + ":" + std::to_string(port_);
    int rc = ldap_initialize(&ldap_, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("[IcaoLdapClient] ldap_initialize failed: {}", ldap_err2string(rc));
        ldap_ = nullptr;
        return false;
    }

    // Set LDAP V3
    int version = LDAP_VERSION3;
    ldap_set_option(ldap_, LDAP_OPT_PROTOCOL_VERSION, &version);

    // Set network timeout
    struct timeval tv = {10, 0};  // 10 seconds
    ldap_set_option(ldap_, LDAP_OPT_NETWORK_TIMEOUT, &tv);

    // Bind
    struct berval cred;
    cred.bv_val = const_cast<char*>(bindPassword_.c_str());
    cred.bv_len = bindPassword_.length();

    rc = ldap_sasl_bind_s(ldap_, bindDn_.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("[IcaoLdapClient] Bind failed to {}: {}", uri, ldap_err2string(rc));
        ldap_unbind_ext_s(ldap_, nullptr, nullptr);
        ldap_ = nullptr;
        return false;
    }

    spdlog::info("[IcaoLdapClient] Connected to ICAO PKD LDAP: {}", uri);
    return true;
}

void IcaoLdapClient::disconnect() {
    if (ldap_) {
        ldap_unbind_ext_s(ldap_, nullptr, nullptr);
        ldap_ = nullptr;
    }
}

std::vector<IcaoLdapCertEntry> IcaoLdapClient::searchDscCertificates(int maxResults) {
    return searchEntries("dc=data," + baseDn_,
                        "(objectClass=pkdDownload)", "DSC", maxResults);
}

std::vector<IcaoLdapCertEntry> IcaoLdapClient::searchCscaCertificates(int maxResults) {
    // CSCA certs are in o=csca branches (or extracted from Master Lists)
    return searchEntries("dc=data," + baseDn_,
                        "(&(objectClass=pkdDownload)(!(objectClass=cRLDistributionPoint)))",
                        "CSCA", maxResults);
}

std::vector<IcaoLdapCertEntry> IcaoLdapClient::searchCrls(int maxResults) {
    return searchEntries("dc=data," + baseDn_,
                        "(objectClass=cRLDistributionPoint)", "CRL", maxResults);
}

std::vector<IcaoLdapCertEntry> IcaoLdapClient::searchNcDscCertificates(int maxResults) {
    return searchEntries("dc=nc-data," + baseDn_,
                        "(objectClass=pkdDownload)", "DSC_NC", maxResults);
}

std::vector<IcaoLdapCertEntry> IcaoLdapClient::searchMasterLists(int maxResults) {
    return searchEntries("dc=data," + baseDn_,
                        "(objectClass=pkdMasterList)", "ML", maxResults);
}

int IcaoLdapClient::getTotalEntryCount() {
    if (!ldap_) return -1;

    LDAPMessage* result = nullptr;
    const char* attrs[] = {"dn", nullptr};

    int rc = ldap_search_ext_s(ldap_, baseDn_.c_str(), LDAP_SCOPE_SUBTREE,
                               "(|(objectClass=pkdDownload)(objectClass=cRLDistributionPoint))",
                               const_cast<char**>(attrs), 0, nullptr, nullptr, nullptr, 0, &result);
    if (rc != LDAP_SUCCESS) {
        spdlog::warn("[IcaoLdapClient] Count query failed: {}", ldap_err2string(rc));
        return -1;
    }

    int count = ldap_count_entries(ldap_, result);
    ldap_msgfree(result);
    return count;
}

std::vector<IcaoLdapCertEntry> IcaoLdapClient::searchEntries(
    const std::string& searchBase,
    const std::string& filter,
    const std::string& certType,
    int maxResults)
{
    std::vector<IcaoLdapCertEntry> entries;
    if (!ldap_) {
        spdlog::error("[IcaoLdapClient] Not connected");
        return entries;
    }

    LDAPMessage* result = nullptr;
    int sizeLimit = maxResults > 0 ? maxResults : 0;

    int rc = ldap_search_ext_s(ldap_, searchBase.c_str(), LDAP_SCOPE_SUBTREE,
                               filter.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
                               sizeLimit, &result);

    // LDAP_SIZELIMIT_EXCEEDED is OK — we got partial results
    if (rc != LDAP_SUCCESS && rc != LDAP_SIZELIMIT_EXCEEDED) {
        spdlog::warn("[IcaoLdapClient] Search failed on {}: {} (filter: {})",
                    searchBase, ldap_err2string(rc), filter);
        if (result) ldap_msgfree(result);
        return entries;
    }

    int count = ldap_count_entries(ldap_, result);
    spdlog::info("[IcaoLdapClient] Found {} {} entries in ICAO PKD", count, certType);

    for (LDAPMessage* entry = ldap_first_entry(ldap_, result);
         entry != nullptr;
         entry = ldap_next_entry(ldap_, entry))
    {
        IcaoLdapCertEntry certEntry;

        // Get DN
        char* dn = ldap_get_dn(ldap_, entry);
        if (dn) {
            certEntry.dn = dn;
            ldap_memfree(dn);
        }

        // Determine actual cert type from DN (o=csca, o=dsc, o=crl, etc.)
        if (certEntry.dn.find("o=csca") != std::string::npos) {
            certEntry.certType = "CSCA";
        } else if (certEntry.dn.find("o=crl") != std::string::npos) {
            certEntry.certType = "CRL";
        } else if (certEntry.dn.find("o=mlsc") != std::string::npos || certEntry.dn.find("o=ml") != std::string::npos) {
            certEntry.certType = "MLSC";
        } else if (certEntry.dn.find("dc=nc-data") != std::string::npos) {
            certEntry.certType = "DSC_NC";
        } else {
            certEntry.certType = "DSC";
        }

        // Extract country code from DN
        certEntry.countryCode = extractCountryFromDn(certEntry.dn);

        // Extract CN
        certEntry.cn = extractStringAttribute(entry, "cn");

        // Extract binary certificate data
        if (certEntry.certType == "CRL") {
            certEntry.binaryData = extractBinaryAttribute(entry, "certificateRevocationList;binary");
        } else {
            certEntry.binaryData = extractBinaryAttribute(entry, "userCertificate;binary");
        }

        // Extract pkdVersion
        auto versionStr = extractStringAttribute(entry, "pkdVersion");
        if (!versionStr.empty()) {
            try { certEntry.pkdVersion = std::stoi(versionStr); } catch (...) {}
        }

        // NC-specific attributes
        certEntry.conformanceCode = extractStringAttribute(entry, "pkdConformanceCode");
        certEntry.conformanceText = extractStringAttribute(entry, "pkdConformanceText");

        // Skip entries without binary data (container entries)
        if (certEntry.binaryData.empty()) continue;

        entries.push_back(std::move(certEntry));
    }

    ldap_msgfree(result);
    return entries;
}

std::vector<uint8_t> IcaoLdapClient::extractBinaryAttribute(void* entry, const std::string& attrName) {
    std::vector<uint8_t> data;
    struct berval** vals = ldap_get_values_len(ldap_, static_cast<LDAPMessage*>(entry), attrName.c_str());
    if (vals && vals[0]) {
        data.assign(
            reinterpret_cast<uint8_t*>(vals[0]->bv_val),
            reinterpret_cast<uint8_t*>(vals[0]->bv_val) + vals[0]->bv_len
        );
    }
    if (vals) ldap_value_free_len(vals);
    return data;
}

std::string IcaoLdapClient::extractStringAttribute(void* entry, const std::string& attrName) {
    struct berval** vals = ldap_get_values_len(ldap_, static_cast<LDAPMessage*>(entry), attrName.c_str());
    std::string value;
    if (vals && vals[0] && vals[0]->bv_val) {
        value.assign(vals[0]->bv_val, vals[0]->bv_len);
    }
    if (vals) ldap_value_free_len(vals);
    return value;
}

std::string IcaoLdapClient::extractCountryFromDn(const std::string& dn) {
    // Match c=XX in DN
    static const std::regex countryRegex("c=([A-Za-z]{2,3})");
    std::smatch match;
    if (std::regex_search(dn, match, countryRegex)) {
        std::string cc = match[1].str();
        // Uppercase
        for (auto& c : cc) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return cc;
    }
    return "XX";
}

} // namespace relay
} // namespace icao

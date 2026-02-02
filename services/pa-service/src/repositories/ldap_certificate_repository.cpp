/**
 * @file ldap_certificate_repository.cpp
 * @brief Implementation of LdapCertificateRepository
 */

#include "ldap_certificate_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace repositories {

LdapCertificateRepository::LdapCertificateRepository(LDAP* conn, const std::string& baseDn)
    : ldapConn_(conn), baseDn_(baseDn)
{
    if (!ldapConn_) {
        throw std::invalid_argument("LDAP connection cannot be null");
    }

    if (baseDn_.empty()) {
        throw std::invalid_argument("Base DN cannot be empty");
    }

    spdlog::debug("LdapCertificateRepository initialized with baseDn: {}", baseDn_);
}

// ==========================================================================
// CSCA Certificate Operations
// ==========================================================================

X509* LdapCertificateRepository::findCscaBySubjectDn(
    const std::string& subjectDn,
    const std::string& countryCode)
{
    spdlog::debug("Finding CSCA by subject DN: {} (country: {})", subjectDn, countryCode);

    // Extract CN from subject DN
    std::string cn = extractDnAttribute(subjectDn, "CN");
    if (cn.empty()) {
        spdlog::warn("Could not extract CN from subject DN: {}", subjectDn);
        return nullptr;
    }

    // Try o=csca first
    std::string baseDn = buildSearchBaseDn("csca", countryCode);
    std::string filter = buildLdapFilter("csca", countryCode);

    std::vector<std::string> attrs = {"userCertificate;binary"};
    LDAPMessage* res = executeLdapSearch(baseDn, filter, attrs);

    if (!res) {
        // Try o=lc (link certificates)
        spdlog::debug("Not found in o=csca, trying o=lc");
        baseDn = buildSearchBaseDn("lc", countryCode);
        res = executeLdapSearch(baseDn, filter, attrs);
    }

    if (!res) {
        return nullptr;
    }

    std::vector<X509*> certs = extractCertificatesFromResult(res);
    ldap_msgfree(res);

    // Find best matching certificate
    X509* bestMatch = nullptr;
    std::string cnLower = cn;
    std::transform(cnLower.begin(), cnLower.end(), cnLower.begin(), ::tolower);

    for (X509* cert : certs) {
        X509_NAME* subject = X509_get_subject_name(cert);
        char subjectBuf[512];
        X509_NAME_oneline(subject, subjectBuf, sizeof(subjectBuf));

        std::string certCn = extractDnAttribute(subjectBuf, "CN");
        std::string certCnLower = certCn;
        std::transform(certCnLower.begin(), certCnLower.end(), certCnLower.begin(), ::tolower);

        if (cnLower == certCnLower) {
            // Exact match found
            if (bestMatch) X509_free(bestMatch);
            bestMatch = cert;
            // Free remaining certs
            for (size_t i = 0; i < certs.size(); i++) {
                if (certs[i] != cert && certs[i] != nullptr) {
                    X509_free(certs[i]);
                }
            }
            break;
        } else if (!bestMatch) {
            bestMatch = cert;
        } else {
            X509_free(cert);
        }
    }

    return bestMatch;
}

std::vector<X509*> LdapCertificateRepository::findAllCscasByCountry(const std::string& countryCode) {
    spdlog::debug("Finding all CSCAs for country: {}", countryCode);

    std::vector<X509*> allCerts;

    // Search in o=csca
    std::string baseDn = buildSearchBaseDn("csca", countryCode);
    std::string filter = buildLdapFilter("csca", countryCode);
    std::vector<std::string> attrs = {"userCertificate;binary"};

    LDAPMessage* res = executeLdapSearch(baseDn, filter, attrs);
    if (res) {
        std::vector<X509*> cscaCerts = extractCertificatesFromResult(res);
        ldap_msgfree(res);
        allCerts.insert(allCerts.end(), cscaCerts.begin(), cscaCerts.end());
    }

    // Search in o=lc (link certificates)
    baseDn = buildSearchBaseDn("lc", countryCode);
    res = executeLdapSearch(baseDn, filter, attrs);
    if (res) {
        std::vector<X509*> lcCerts = extractCertificatesFromResult(res);
        ldap_msgfree(res);
        allCerts.insert(allCerts.end(), lcCerts.begin(), lcCerts.end());
    }

    spdlog::info("Found {} CSCAs for country {}", allCerts.size(), countryCode);
    return allCerts;
}

X509* LdapCertificateRepository::findCscaByIssuerDn(
    const std::string& issuerDn,
    const std::string& countryCode)
{
    spdlog::debug("Finding CSCA by issuer DN: {} (country: {})", issuerDn, countryCode);

    std::string issuerCn = extractDnAttribute(issuerDn, "CN");
    if (issuerCn.empty()) {
        spdlog::warn("Could not extract CN from issuer DN: {}", issuerDn);
        return nullptr;
    }

    // Try o=csca first (most CSCAs are self-signed)
    std::string baseDn = buildSearchBaseDn("csca", countryCode);
    std::string filter = buildLdapFilter("csca", countryCode);
    std::vector<std::string> attrs = {"userCertificate;binary"};

    LDAPMessage* res = executeLdapSearch(baseDn, filter, attrs);
    if (res) {
        std::vector<X509*> certs = extractCertificatesFromResult(res);
        ldap_msgfree(res);

        // Find matching certificate by CN
        std::string issuerCnLower = issuerCn;
        std::transform(issuerCnLower.begin(), issuerCnLower.end(), issuerCnLower.begin(), ::tolower);

        for (X509* cert : certs) {
            X509_NAME* subject = X509_get_subject_name(cert);
            char subjectBuf[512];
            X509_NAME_oneline(subject, subjectBuf, sizeof(subjectBuf));

            std::string certCn = extractDnAttribute(subjectBuf, "CN");
            std::string certCnLower = certCn;
            std::transform(certCnLower.begin(), certCnLower.end(), certCnLower.begin(), ::tolower);

            if (issuerCnLower == certCnLower) {
                // Free remaining certs
                for (X509* c : certs) {
                    if (c != cert) X509_free(c);
                }
                return cert;
            }
        }

        // No exact match, free all
        for (X509* c : certs) {
            X509_free(c);
        }
    }

    // Try o=lc (link certificates)
    baseDn = buildSearchBaseDn("lc", countryCode);
    res = executeLdapSearch(baseDn, filter, attrs);
    if (res) {
        std::vector<X509*> certs = extractCertificatesFromResult(res);
        ldap_msgfree(res);

        std::string issuerCnLower = issuerCn;
        std::transform(issuerCnLower.begin(), issuerCnLower.end(), issuerCnLower.begin(), ::tolower);

        for (X509* cert : certs) {
            X509_NAME* subject = X509_get_subject_name(cert);
            char subjectBuf[512];
            X509_NAME_oneline(subject, subjectBuf, sizeof(subjectBuf));

            std::string certCn = extractDnAttribute(subjectBuf, "CN");
            std::string certCnLower = certCn;
            std::transform(certCnLower.begin(), certCnLower.end(), certCnLower.begin(), ::tolower);

            if (issuerCnLower == certCnLower) {
                for (X509* c : certs) {
                    if (c != cert) X509_free(c);
                }
                return cert;
            }
        }

        for (X509* c : certs) {
            X509_free(c);
        }
    }

    spdlog::warn("No CSCA found for issuer: {}", issuerDn);
    return nullptr;
}

// ==========================================================================
// DSC Certificate Operations
// ==========================================================================

X509* LdapCertificateRepository::findDscBySubjectDn(
    const std::string& subjectDn,
    const std::string& countryCode)
{
    spdlog::debug("Finding DSC by subject DN: {} (country: {})", subjectDn, countryCode);

    std::string baseDn = buildSearchBaseDn("dsc", countryCode);
    std::string filter = buildLdapFilter("dsc", countryCode);
    std::vector<std::string> attrs = {"userCertificate;binary"};

    LDAPMessage* res = executeLdapSearch(baseDn, filter, attrs);
    if (!res) {
        return nullptr;
    }

    std::vector<X509*> certs = extractCertificatesFromResult(res);
    ldap_msgfree(res);

    if (!certs.empty()) {
        // Return first match, free others
        X509* result = certs[0];
        for (size_t i = 1; i < certs.size(); i++) {
            X509_free(certs[i]);
        }
        return result;
    }

    return nullptr;
}

// ==========================================================================
// Helper Methods
// ==========================================================================

std::string LdapCertificateRepository::buildLdapFilter(
    const std::string& type,
    const std::string& countryCode,
    const std::string& subjectDn)
{
    // Note: type and countryCode are handled via base DN in buildSearchBaseDn()
    (void)type;
    (void)countryCode;

    // Build compound LDAP filter
    std::vector<std::string> conditions;

    // Always include objectClass
    conditions.push_back("(objectClass=pkdDownload)");

    // Add subjectDn filter if provided (searches in cn attribute)
    if (!subjectDn.empty()) {
        // Extract CN from subject DN for filtering
        std::string cn = extractDnAttribute(subjectDn, "CN");
        if (!cn.empty()) {
            // Use wildcard search for partial matching
            conditions.push_back("(cn=*" + escapeLdapFilterValue(cn) + "*)");
        }
    }

    // If multiple conditions, use AND operator
    if (conditions.size() == 1) {
        return conditions[0];
    } else {
        std::ostringstream oss;
        oss << "(&";
        for (const auto& cond : conditions) {
            oss << cond;
        }
        oss << ")";
        return oss.str();
    }
}

std::string LdapCertificateRepository::escapeLdapFilterValue(const std::string& value) {
    // Escape special LDAP filter characters according to RFC 4515
    // Special chars: * ( ) \ NUL
    std::string result;
    result.reserve(value.length() * 2);  // Reserve space for potential escaping

    for (char c : value) {
        switch (c) {
            case '*':
                result += "\\2a";
                break;
            case '(':
                result += "\\28";
                break;
            case ')':
                result += "\\29";
                break;
            case '\\':
                result += "\\5c";
                break;
            case '\0':
                result += "\\00";
                break;
            default:
                result += c;
        }
    }

    return result;
}

std::string LdapCertificateRepository::buildSearchBaseDn(
    const std::string& type,
    const std::string& countryCode)
{
    std::ostringstream oss;
    oss << "o=" << type << ",c=" << countryCode << ",dc=data," << baseDn_;
    return oss.str();
}

X509* LdapCertificateRepository::parseCertificateFromLdap(struct berval** certData) {
    if (!certData || !certData[0]) {
        return nullptr;
    }

    const unsigned char* data = reinterpret_cast<const unsigned char*>(certData[0]->bv_val);
    X509* cert = d2i_X509(nullptr, &data, certData[0]->bv_len);

    if (!cert) {
        spdlog::error("Failed to parse X509 certificate from LDAP data");
    }

    return cert;
}

std::string LdapCertificateRepository::extractDnAttribute(
    const std::string& dn,
    const std::string& attr)
{
    // Simple extraction: find "attr=" and get value until next comma or end
    std::string searchStr = attr + "=";
    size_t pos = dn.find(searchStr);

    if (pos == std::string::npos) {
        return "";
    }

    size_t start = pos + searchStr.length();
    size_t end = dn.find(',', start);

    if (end == std::string::npos) {
        return dn.substr(start);
    }

    return dn.substr(start, end - start);
}

std::string LdapCertificateRepository::normalizeDn(const std::string& dn) {
    // Simple normalization: lowercase
    std::string normalized = dn;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    return normalized;
}

// ==========================================================================
// Private Helper Methods
// ==========================================================================

LDAPMessage* LdapCertificateRepository::executeLdapSearch(
    const std::string& baseDn,
    const std::string& filter,
    const std::vector<std::string>& attrs)
{
    spdlog::debug("LDAP search: base={}, filter={}", baseDn, filter);

    // Convert attrs to char* array
    std::vector<char*> attrArray;
    for (const auto& attr : attrs) {
        attrArray.push_back(const_cast<char*>(attr.c_str()));
    }
    attrArray.push_back(nullptr);

    LDAPMessage* res = nullptr;
    int rc = ldap_search_ext_s(
        ldapConn_,
        baseDn.c_str(),
        LDAP_SCOPE_SUBTREE,
        filter.c_str(),
        attrArray.data(),
        0,
        nullptr,
        nullptr,
        nullptr,
        100,  // Size limit
        &res
    );

    if (rc != LDAP_SUCCESS) {
        spdlog::debug("LDAP search failed: {} ({})", ldap_err2string(rc), baseDn);
        if (res) ldap_msgfree(res);
        return nullptr;
    }

    int count = ldap_count_entries(ldapConn_, res);
    spdlog::debug("LDAP search returned {} entries", count);

    if (count == 0) {
        ldap_msgfree(res);
        return nullptr;
    }

    return res;
}

std::vector<X509*> LdapCertificateRepository::extractCertificatesFromResult(LDAPMessage* msg) {
    std::vector<X509*> certs;

    LDAPMessage* entry = ldap_first_entry(ldapConn_, msg);
    while (entry) {
        struct berval** values = ldap_get_values_len(ldapConn_, entry, "userCertificate;binary");

        if (values && values[0]) {
            X509* cert = parseCertificateFromLdap(values);
            if (cert) {
                certs.push_back(cert);
            }
            ldap_value_free_len(values);
        }

        entry = ldap_next_entry(ldapConn_, entry);
    }

    return certs;
}

} // namespace repositories

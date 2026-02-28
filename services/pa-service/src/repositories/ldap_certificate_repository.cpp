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
#include <iomanip>
#include <openssl/evp.h>

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

// --- CSCA Certificate Operations ---

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
        char* subjectStr = X509_NAME_oneline(subject, nullptr, 0);
        if (!subjectStr) continue;

        std::string certCn = extractDnAttribute(subjectStr, "CN");
        OPENSSL_free(subjectStr);
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
            char* subjectStr = X509_NAME_oneline(subject, nullptr, 0);
            if (!subjectStr) continue;

            std::string certCn = extractDnAttribute(subjectStr, "CN");
            OPENSSL_free(subjectStr);
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
            char* subjectStr = X509_NAME_oneline(subject, nullptr, 0);
            if (!subjectStr) continue;

            std::string certCn = extractDnAttribute(subjectStr, "CN");
            OPENSSL_free(subjectStr);
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

// --- DSC Certificate Operations ---

X509* LdapCertificateRepository::findDscBySubjectDn(
    const std::string& subjectDn,
    const std::string& countryCode,
    bool* isNonConformant)
{
    spdlog::debug("Finding DSC by subject DN: {} (country: {})", subjectDn, countryCode);

    std::string filter = buildLdapFilter("dsc", countryCode);
    std::vector<std::string> attrs = {"userCertificate;binary"};

    // Search dc=data first (conformant DSC)
    std::string baseDn = buildSearchBaseDn("dsc", countryCode);
    LDAPMessage* res = executeLdapSearch(baseDn, filter, attrs);
    if (res) {
        std::vector<X509*> certs = extractCertificatesFromResult(res);
        ldap_msgfree(res);
        if (!certs.empty()) {
            X509* result = certs[0];
            for (size_t i = 1; i < certs.size(); i++) {
                X509_free(certs[i]);
            }
            if (isNonConformant) *isNonConformant = false;
            return result;
        }
    }

    // Fallback: search dc=nc-data (non-conformant DSC_NC)
    spdlog::debug("DSC not found in dc=data, trying dc=nc-data (non-conformant)");
    baseDn = buildNcDataSearchBaseDn("dsc", countryCode);
    res = executeLdapSearch(baseDn, filter, attrs);
    if (res) {
        std::vector<X509*> certs = extractCertificatesFromResult(res);
        ldap_msgfree(res);
        if (!certs.empty()) {
            X509* result = certs[0];
            for (size_t i = 1; i < certs.size(); i++) {
                X509_free(certs[i]);
            }
            if (isNonConformant) *isNonConformant = true;
            spdlog::info("DSC found in dc=nc-data (non-conformant) for country {}", countryCode);
            return result;
        }
    }

    spdlog::debug("DSC not found in either dc=data or dc=nc-data");
    return nullptr;
}

// --- Helper Methods ---

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

std::string LdapCertificateRepository::buildNcDataSearchBaseDn(
    const std::string& type,
    const std::string& countryCode)
{
    std::ostringstream oss;
    oss << "o=" << type << ",c=" << countryCode << ",dc=nc-data," << baseDn_;
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
    // Use shared library for robust DN parsing
    try {
        X509_NAME* x509Name = icao::x509::parseDnString(dn);
        if (!x509Name) {
            return "";
        }

        auto components = icao::x509::extractDnComponents(x509Name);
        X509_NAME_free(x509Name);

        // Map attribute name to DnComponents field
        if (attr == "CN" && components.commonName.has_value()) {
            return *components.commonName;
        } else if (attr == "C" && components.country.has_value()) {
            return *components.country;
        } else if (attr == "O" && components.organization.has_value()) {
            return *components.organization;
        } else if (attr == "OU" && components.organizationalUnit.has_value()) {
            return *components.organizationalUnit;
        } else if (attr == "serialNumber" && components.serialNumber.has_value()) {
            return *components.serialNumber;
        }

        return "";
    } catch (const std::exception& e) {
        spdlog::warn("Failed to extract DN attribute '{}' from '{}': {}", attr, dn, e.what());
        return "";
    }
}

std::string LdapCertificateRepository::normalizeDn(const std::string& dn) {
    // Use shared library for format-independent normalization
    try {
        X509_NAME* x509Name = icao::x509::parseDnString(dn);
        if (!x509Name) {
            // Fallback to simple lowercase
            std::string normalized = dn;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
            return normalized;
        }

        auto components = icao::x509::extractDnComponents(x509Name);
        X509_NAME_free(x509Name);

        // Build normalized DN from components (sorted, lowercase)
        std::string normalized;
        if (components.country.has_value()) {
            normalized += "c=" + *components.country + "|";
        }
        if (components.organization.has_value()) {
            normalized += "o=" + *components.organization + "|";
        }
        if (components.organizationalUnit.has_value()) {
            normalized += "ou=" + *components.organizationalUnit + "|";
        }
        if (components.commonName.has_value()) {
            normalized += "cn=" + *components.commonName + "|";
        }
        if (components.serialNumber.has_value()) {
            normalized += "sn=" + *components.serialNumber + "|";
        }

        // Lowercase the entire normalized DN
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
        return normalized;
    } catch (const std::exception& e) {
        spdlog::warn("Failed to normalize DN '{}': {}", dn, e.what());
        // Fallback to simple lowercase
        std::string normalized = dn;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
        return normalized;
    }
}

// --- Private Helper Methods ---

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

// --- DSC Conformance Check (nc-data LDAP lookup) ---

DscConformanceInfo LdapCertificateRepository::checkDscConformance(
    X509* dscCert,
    const std::string& countryCode)
{
    DscConformanceInfo info;

    if (!dscCert || countryCode.empty()) {
        return info;
    }

    try {
        // Compute SHA-256 fingerprint of the DER-encoded certificate
        unsigned char* derBuf = nullptr;
        int derLen = i2d_X509(dscCert, &derBuf);
        if (derLen <= 0 || !derBuf) {
            spdlog::debug("checkDscConformance: Failed to DER-encode certificate");
            return info;
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen = 0;
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, derBuf, derLen);
        EVP_DigestFinal_ex(ctx, hash, &hashLen);
        EVP_MD_CTX_free(ctx);
        OPENSSL_free(derBuf);

        std::ostringstream fpStream;
        for (unsigned int i = 0; i < hashLen; i++) {
            fpStream << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(hash[i]);
        }
        std::string fingerprint = fpStream.str();

        // Build DN: cn={fingerprint},o=dsc,c={country},dc=nc-data,{baseDn}
        std::string searchDn = "cn=" + fingerprint + ",o=dsc,c=" + countryCode
                               + ",dc=nc-data," + baseDn_;

        spdlog::debug("checkDscConformance: Searching nc-data DN: {}", searchDn);

        // LDAP SCOPE_BASE search for the specific entry
        std::string filter = "(objectClass=*)";
        char* attrs[] = {
            const_cast<char*>("pkdConformanceCode"),
            const_cast<char*>("pkdConformanceText"),
            const_cast<char*>("pkdVersion"),
            nullptr
        };

        LDAPMessage* res = nullptr;
        struct timeval timeout = {5, 0};  // 5 second timeout
        int rc = ldap_search_ext_s(
            ldapConn_,
            searchDn.c_str(),
            LDAP_SCOPE_BASE,
            filter.c_str(),
            attrs,
            0,
            nullptr, nullptr,
            &timeout,
            1,
            &res
        );

        if (rc != LDAP_SUCCESS) {
            spdlog::debug("checkDscConformance: Not found in nc-data ({})", ldap_err2string(rc));
            if (res) ldap_msgfree(res);
            return info;
        }

        LDAPMessage* entry = ldap_first_entry(ldapConn_, res);
        if (!entry) {
            ldap_msgfree(res);
            return info;
        }

        // Found in nc-data - this DSC is non-conformant
        info.isNonConformant = true;

        // Extract conformance attributes using ldap_get_values_len
        struct berval** vals = ldap_get_values_len(ldapConn_, entry, "pkdConformanceCode");
        if (vals && vals[0]) {
            info.conformanceCode = std::string(vals[0]->bv_val, vals[0]->bv_len);
            ldap_value_free_len(vals);
        }

        vals = ldap_get_values_len(ldapConn_, entry, "pkdConformanceText");
        if (vals && vals[0]) {
            info.conformanceText = std::string(vals[0]->bv_val, vals[0]->bv_len);
            ldap_value_free_len(vals);
        }

        vals = ldap_get_values_len(ldapConn_, entry, "pkdVersion");
        if (vals && vals[0]) {
            info.pkdVersion = std::string(vals[0]->bv_val, vals[0]->bv_len);
            ldap_value_free_len(vals);
        }

        ldap_msgfree(res);

        spdlog::info("checkDscConformance: DSC is non-conformant - code={}, text={}",
                     info.conformanceCode, info.conformanceText.substr(0, 60));

    } catch (const std::exception& e) {
        spdlog::warn("checkDscConformance: Exception during nc-data lookup: {}", e.what());
    }

    return info;
}

} // namespace repositories

/**
 * @file dn_components.cpp
 * @brief DN Components extraction implementation
 *
 * Provides structured access to DN components using OpenSSL NIDs.
 */

#include "icao/x509/dn_components.h"
#include <openssl/objects.h>
#include <sstream>

namespace icao {
namespace x509 {

// DnComponents struct methods

bool DnComponents::isEmpty() const {
    return !commonName && !organization && !organizationalUnit &&
           !locality && !stateOrProvince && !country && !email &&
           !serialNumber && !title && !givenName && !surname && !pseudonym;
}

std::string DnComponents::toRfc2253() const {
    std::ostringstream oss;
    bool first = true;

    // RFC2253 order: CN, OU, O, STREET, L, ST, C
    auto addComponent = [&](const std::string& name, const std::optional<std::string>& value) {
        if (value && !value->empty()) {
            if (!first) {
                oss << ",";
            }
            oss << name << "=" << *value;
            first = false;
        }
    };

    addComponent("CN", commonName);
    addComponent("OU", organizationalUnit);
    addComponent("O", organization);
    addComponent("L", locality);
    addComponent("ST", stateOrProvince);
    addComponent("C", country);
    addComponent("emailAddress", email);
    addComponent("serialNumber", serialNumber);
    addComponent("title", title);
    addComponent("GN", givenName);
    addComponent("SN", surname);
    addComponent("pseudonym", pseudonym);

    return oss.str();
}

std::string DnComponents::getDisplayName() const {
    // Priority order: CN > O > email > "Unknown"
    if (commonName && !commonName->empty()) {
        return *commonName;
    }
    if (organization && !organization->empty()) {
        return *organization;
    }
    if (email && !email->empty()) {
        return *email;
    }
    return "Unknown";
}

// Helper function to extract single component
std::optional<std::string> getDnComponentByNid(X509_NAME* name, int nid) {
    if (!name) {
        return std::nullopt;
    }

    // Get index of entry with given NID (-1 means start from beginning)
    int pos = X509_NAME_get_index_by_NID(name, nid, -1);
    if (pos < 0) {
        return std::nullopt;
    }

    // Get the entry
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, pos);
    if (!entry) {
        return std::nullopt;
    }

    // Get the data
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) {
        return std::nullopt;
    }

    // Convert to UTF-8
    unsigned char* utf8_str = nullptr;
    int utf8_len = ASN1_STRING_to_UTF8(&utf8_str, data);
    if (utf8_len < 0 || !utf8_str) {
        return std::nullopt;
    }

    std::string result(reinterpret_cast<char*>(utf8_str), utf8_len);
    OPENSSL_free(utf8_str);

    return result;
}

std::vector<std::string> getDnComponentAllValues(X509_NAME* name, int nid) {
    std::vector<std::string> values;

    if (!name) {
        return values;
    }

    // Iterate through all entries with given NID
    int pos = -1;
    while ((pos = X509_NAME_get_index_by_NID(name, nid, pos)) >= 0) {
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, pos);
        if (!entry) {
            continue;
        }

        ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        if (!data) {
            continue;
        }

        unsigned char* utf8_str = nullptr;
        int utf8_len = ASN1_STRING_to_UTF8(&utf8_str, data);
        if (utf8_len < 0 || !utf8_str) {
            continue;
        }

        values.emplace_back(reinterpret_cast<char*>(utf8_str), utf8_len);
        OPENSSL_free(utf8_str);
    }

    return values;
}

DnComponents extractDnComponents(X509_NAME* name) {
    DnComponents components;

    if (!name) {
        return components;
    }

    // Extract each component using OpenSSL NIDs
    components.commonName = getDnComponentByNid(name, NID_commonName);
    components.organization = getDnComponentByNid(name, NID_organizationName);
    components.organizationalUnit = getDnComponentByNid(name, NID_organizationalUnitName);
    components.locality = getDnComponentByNid(name, NID_localityName);
    components.stateOrProvince = getDnComponentByNid(name, NID_stateOrProvinceName);
    components.country = getDnComponentByNid(name, NID_countryName);
    components.email = getDnComponentByNid(name, NID_pkcs9_emailAddress);
    components.serialNumber = getDnComponentByNid(name, NID_serialNumber);
    components.title = getDnComponentByNid(name, NID_title);
    components.givenName = getDnComponentByNid(name, NID_givenName);
    components.surname = getDnComponentByNid(name, NID_surname);
    components.pseudonym = getDnComponentByNid(name, NID_pseudonym);

    return components;
}

DnComponents extractSubjectComponents(X509* cert) {
    if (!cert) {
        return DnComponents();
    }

    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) {
        return DnComponents();
    }

    return extractDnComponents(subject);
}

DnComponents extractIssuerComponents(X509* cert) {
    if (!cert) {
        return DnComponents();
    }

    X509_NAME* issuer = X509_get_issuer_name(cert);
    if (!issuer) {
        return DnComponents();
    }

    return extractDnComponents(issuer);
}

} // namespace x509
} // namespace icao

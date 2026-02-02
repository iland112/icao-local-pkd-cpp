/**
 * @file dn_parser.cpp
 * @brief DN Parser implementation
 *
 * ICAO-compliant DN parsing using OpenSSL ASN.1 structures.
 */

#include "icao/x509/dn_parser.h"
#include <openssl/bio.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>
#include <map>

namespace icao {
namespace x509 {

std::optional<std::string> x509NameToString(X509_NAME* name, DnFormat format) {
    if (!name) {
        return std::nullopt;
    }

    // Create BIO for capturing output
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return std::nullopt;
    }

    // Set flags based on desired format
    unsigned long flags = 0;

    switch (format) {
        case DnFormat::RFC2253:
            // RFC2253 format: CN=Name,O=Org,C=XX
            flags = XN_FLAG_RFC2253;
            break;

        case DnFormat::ONELINE:
            // OpenSSL oneline format: /C=XX/O=Org/CN=Name
            flags = XN_FLAG_ONELINE;
            break;

        case DnFormat::MULTILINE:
            // Multi-line format for debugging
            flags = XN_FLAG_MULTILINE;
            break;
    }

    // Print X509_NAME to BIO
    int result = X509_NAME_print_ex(bio, name, 0, flags);
    if (result < 0) {
        BIO_free(bio);
        return std::nullopt;
    }

    // Read output from BIO
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    if (len <= 0 || !data) {
        BIO_free(bio);
        return std::nullopt;
    }

    std::string dn_string(data, len);
    BIO_free(bio);

    return dn_string;
}

bool compareX509Names(X509_NAME* name1, X509_NAME* name2) {
    if (!name1 || !name2) {
        return false;
    }

    // X509_NAME_cmp returns 0 if names are equal
    return X509_NAME_cmp(name1, name2) == 0;
}

std::optional<std::string> normalizeDnForComparison(const std::string& dn) {
    if (dn.empty()) {
        return std::nullopt;
    }

    // Parse DN string to X509_NAME
    X509_NAME* name = parseDnString(dn);
    if (!name) {
        return std::nullopt;
    }

    // Extract components into a map
    std::map<std::string, std::string> components;

    // Get number of entries in DN
    int entry_count = X509_NAME_entry_count(name);

    for (int i = 0; i < entry_count; i++) {
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, i);
        if (!entry) {
            continue;
        }

        // Get attribute type (OID)
        ASN1_OBJECT* obj = X509_NAME_ENTRY_get_object(entry);
        if (!obj) {
            continue;
        }

        // Get attribute name (CN, O, OU, C, etc.)
        char attr_name[80];
        int nid = OBJ_obj2nid(obj);
        const char* sn = OBJ_nid2sn(nid);
        if (!sn) {
            continue;
        }
        snprintf(attr_name, sizeof(attr_name), "%s", sn);

        // Get attribute value
        ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        if (!data) {
            continue;
        }

        unsigned char* utf8_str = nullptr;
        int utf8_len = ASN1_STRING_to_UTF8(&utf8_str, data);
        if (utf8_len < 0 || !utf8_str) {
            continue;
        }

        std::string value(reinterpret_cast<char*>(utf8_str), utf8_len);
        OPENSSL_free(utf8_str);

        // Convert attribute name to lowercase
        std::string attr_lower = attr_name;
        std::transform(attr_lower.begin(), attr_lower.end(), attr_lower.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Convert value to lowercase
        std::transform(value.begin(), value.end(), value.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Store as "attr=value"
        components[attr_lower] = attr_lower + "=" + value;
    }

    X509_NAME_free(name);

    if (components.empty()) {
        return std::nullopt;
    }

    // Build normalized string by sorting components alphabetically
    std::vector<std::string> sorted_components;
    for (const auto& pair : components) {
        sorted_components.push_back(pair.second);
    }

    // Join with '|' separator
    std::ostringstream oss;
    for (size_t i = 0; i < sorted_components.size(); i++) {
        if (i > 0) {
            oss << "|";
        }
        oss << sorted_components[i];
    }

    return oss.str();
}

X509_NAME* parseDnString(const std::string& dn) {
    if (dn.empty()) {
        return nullptr;
    }

    // Try RFC2253 format first
    // Use OpenSSL's X509_NAME_oneline to parse
    X509_NAME* name = X509_NAME_new();
    if (!name) {
        return nullptr;
    }

    // Determine format
    bool is_oneline_format = (dn[0] == '/');

    if (is_oneline_format) {
        // OpenSSL oneline format: /C=XX/O=Org/CN=Name
        // Split by '/' and parse each component
        std::istringstream iss(dn.substr(1)); // Skip first '/'
        std::string component;

        while (std::getline(iss, component, '/')) {
            size_t eq_pos = component.find('=');
            if (eq_pos == std::string::npos) {
                continue;
            }

            std::string attr = component.substr(0, eq_pos);
            std::string value = component.substr(eq_pos + 1);

            if (!X509_NAME_add_entry_by_txt(name, attr.c_str(), MBSTRING_UTF8,
                                           reinterpret_cast<const unsigned char*>(value.c_str()),
                                           value.length(), -1, 0)) {
                X509_NAME_free(name);
                return nullptr;
            }
        }
    } else {
        // RFC2253 format: CN=Name,O=Org,C=XX
        // Parse more carefully to handle escaped commas
        size_t pos = 0;
        std::string attr, value;
        bool in_attr = true;
        bool escaped = false;

        for (size_t i = 0; i < dn.length(); i++) {
            char c = dn[i];

            if (escaped) {
                if (in_attr) {
                    attr += c;
                } else {
                    value += c;
                }
                escaped = false;
                continue;
            }

            if (c == '\\') {
                escaped = true;
                continue;
            }

            if (c == '=' && in_attr) {
                in_attr = false;
                continue;
            }

            if (c == ',' && !in_attr) {
                // Add entry
                if (!attr.empty() && !value.empty()) {
                    // Trim whitespace
                    attr.erase(0, attr.find_first_not_of(" \t"));
                    attr.erase(attr.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);

                    if (!X509_NAME_add_entry_by_txt(name, attr.c_str(), MBSTRING_UTF8,
                                                   reinterpret_cast<const unsigned char*>(value.c_str()),
                                                   value.length(), -1, 0)) {
                        X509_NAME_free(name);
                        return nullptr;
                    }
                }
                attr.clear();
                value.clear();
                in_attr = true;
                continue;
            }

            if (in_attr) {
                attr += c;
            } else {
                value += c;
            }
        }

        // Add last entry
        if (!attr.empty() && !value.empty()) {
            attr.erase(0, attr.find_first_not_of(" \t"));
            attr.erase(attr.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            if (!X509_NAME_add_entry_by_txt(name, attr.c_str(), MBSTRING_UTF8,
                                           reinterpret_cast<const unsigned char*>(value.c_str()),
                                           value.length(), -1, 0)) {
                X509_NAME_free(name);
                return nullptr;
            }
        }
    }

    // Check if we successfully parsed anything
    if (X509_NAME_entry_count(name) == 0) {
        X509_NAME_free(name);
        return nullptr;
    }

    return name;
}

std::optional<std::string> getSubjectDn(X509* cert, DnFormat format) {
    if (!cert) {
        return std::nullopt;
    }

    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) {
        return std::nullopt;
    }

    return x509NameToString(subject, format);
}

std::optional<std::string> getIssuerDn(X509* cert, DnFormat format) {
    if (!cert) {
        return std::nullopt;
    }

    X509_NAME* issuer = X509_get_issuer_name(cert);
    if (!issuer) {
        return std::nullopt;
    }

    return x509NameToString(issuer, format);
}

bool isSelfSigned(X509* cert) {
    if (!cert) {
        return false;
    }

    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    if (!subject || !issuer) {
        return false;
    }

    return compareX509Names(subject, issuer);
}

} // namespace x509
} // namespace icao

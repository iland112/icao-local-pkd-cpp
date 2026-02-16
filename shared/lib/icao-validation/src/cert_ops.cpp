/**
 * @file cert_ops.cpp
 * @brief Pure X.509 certificate operations implementation
 *
 * Consolidated from:
 *   - services/pkd-management/src/services/validation_service.cpp
 *   - services/pa-service/src/services/certificate_validation_service.cpp
 *
 * All functions are idempotent — no I/O, no logging side effects.
 */

#include "icao/validation/cert_ops.h"

#include <cstring>
#include <ctime>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>
#include <openssl/evp.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

namespace icao::validation {

// --- Signature Verification ---

bool verifyCertificateSignature(X509* cert, X509* issuerCert) {
    if (!cert || !issuerCert) return false;

    EVP_PKEY* issuerPubKey = X509_get_pubkey(issuerCert);
    if (!issuerPubKey) return false;

    int result = X509_verify(cert, issuerPubKey);
    EVP_PKEY_free(issuerPubKey);

    // Clear OpenSSL error queue to prevent stale errors from leaking
    if (result != 1) {
        ERR_clear_error();
    }

    return (result == 1);
}

// --- Certificate Status Checks ---

bool isCertificateExpired(X509* cert) {
    if (!cert) return true;
    time_t now = time(nullptr);
    return (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0);
}

bool isCertificateNotYetValid(X509* cert) {
    if (!cert) return true;
    time_t now = time(nullptr);
    return (X509_cmp_time(X509_get0_notBefore(cert), &now) > 0);
}

bool isSelfSigned(X509* cert) {
    if (!cert) return false;

    // RFC 4517 Section 4.2.15: case-insensitive DN comparison
    std::string subject = getSubjectDn(cert);
    std::string issuer = getIssuerDn(cert);
    return (strcasecmp(subject.c_str(), issuer.c_str()) == 0);
}

bool isLinkCertificate(X509* cert) {
    if (!cert) return false;

    // Link certificates must NOT be self-signed
    if (isSelfSigned(cert)) return false;

    // Check BasicConstraints: CA:TRUE
    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));
    if (!bc || !bc->ca) {
        if (bc) BASIC_CONSTRAINTS_free(bc);
        return false;
    }
    BASIC_CONSTRAINTS_free(bc);

    // Check KeyUsage: keyCertSign (bit 5)
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    if (!usage) return false;

    bool hasKeyCertSign = (ASN1_BIT_STRING_get_bit(usage, 5) == 1);
    ASN1_BIT_STRING_free(usage);

    return hasKeyCertSign;
}

// --- DN Extraction ---

std::string getSubjectDn(X509* cert) {
    if (!cert) return "";

    char* dn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (!dn) return "";
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

std::string getIssuerDn(X509* cert) {
    if (!cert) return "";

    char* dn = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    if (!dn) return "";
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

// --- Fingerprint ---

std::string getCertificateFingerprint(X509* cert) {
    if (!cert) return "";

    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;

    if (X509_digest(cert, EVP_sha256(), md, &mdLen) != 1) {
        return "";
    }

    std::ostringstream oss;
    for (unsigned int i = 0; i < mdLen; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(md[i]);
    }
    return oss.str();
}

// --- DN Utilities ---

std::string normalizeDnForComparison(const std::string& dn) {
    if (dn.empty()) return dn;

    std::vector<std::string> parts;

    if (dn[0] == '/') {
        // OpenSSL slash-separated format: /C=Z/O=Y/CN=X
        std::istringstream stream(dn);
        std::string segment;
        while (std::getline(stream, segment, '/')) {
            if (!segment.empty()) {
                std::string lower;
                lower.reserve(segment.size());
                for (char c : segment) {
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) {
                    parts.push_back(lower.substr(s));
                }
            }
        }
    } else {
        // RFC 2253 comma-separated format: CN=X,O=Y,C=Z
        std::string current;
        bool inQuotes = false;
        for (size_t i = 0; i < dn.size(); i++) {
            char c = dn[i];
            if (c == '"') {
                inQuotes = !inQuotes;
                current += c;
            } else if (c == ',' && !inQuotes) {
                std::string lower;
                lower.reserve(current.size());
                for (char ch : current) {
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) {
                    parts.push_back(lower.substr(s));
                }
                current.clear();
            } else if (c == '\\' && i + 1 < dn.size()) {
                current += c;
                current += dn[++i];
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            std::string lower;
            lower.reserve(current.size());
            for (char ch : current) {
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            size_t s = lower.find_first_not_of(" \t");
            if (s != std::string::npos) {
                parts.push_back(lower.substr(s));
            }
        }
    }

    // Sort components for order-independent comparison
    std::sort(parts.begin(), parts.end());

    // Join with pipe separator
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += "|";
        result += parts[i];
    }
    return result;
}

std::string extractDnAttribute(const std::string& dn, const std::string& attr) {
    std::string searchKey = attr + "=";

    // Lowercase DN for case-insensitive search
    std::string dnLower;
    dnLower.reserve(dn.size());
    for (char c : dn) {
        dnLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string keyLower;
    keyLower.reserve(searchKey.size());
    for (char c : searchKey) {
        keyLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    size_t pos = 0;
    while ((pos = dnLower.find(keyLower, pos)) != std::string::npos) {
        // Verify it's at a boundary (start of string, after / or , or space)
        if (pos == 0 || dnLower[pos - 1] == '/' || dnLower[pos - 1] == ',' || dnLower[pos - 1] == ' ') {
            size_t valStart = pos + keyLower.size();
            size_t valEnd = dn.find_first_of("/,", valStart);
            if (valEnd == std::string::npos) {
                valEnd = dn.size();
            }
            std::string val = dn.substr(valStart, valEnd - valStart);
            // Trim and lowercase
            size_t s = val.find_first_not_of(" \t");
            size_t e = val.find_last_not_of(" \t");
            if (s != std::string::npos) {
                val = val.substr(s, e - s + 1);
                for (char& c : val) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return val;
            }
        }
        pos++;
    }
    return "";
}

// --- Time Utilities ---

std::string asn1TimeToIso8601(const ASN1_TIME* t) {
    if (!t) return "";
    struct tm tm_val;
    if (ASN1_TIME_to_tm(t, &tm_val) == 1) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
        return std::string(buf);
    }
    return "";
}

} // namespace icao::validation

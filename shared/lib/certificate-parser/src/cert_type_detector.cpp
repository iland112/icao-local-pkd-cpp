#include "cert_type_detector.h"
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/objects.h>
#include <openssl/asn1.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace icao {
namespace certificate_parser {

CertificateInfo CertTypeDetector::detectType(X509* cert) {
    CertificateInfo info;
    info.type = CertificateType::UNKNOWN;

    if (!cert) {
        info.error_message = "Certificate pointer is null";
        return info;
    }

    try {
        // Extract basic information
        info.subject_dn = nameToString(X509_get_subject_name(cert));
        info.issuer_dn = nameToString(X509_get_issuer_name(cert));
        info.country = extractCountry(cert);
        info.fingerprint = calculateFingerprint(cert);
        info.is_self_signed = isSelfSigned(cert);
        info.is_ca = isCA(cert);
        info.has_key_cert_sign = hasKeyCertSign(cert);

        // Detection Algorithm (ICAO Doc 9303 Part 12)

        // 1. Check Extended Key Usage for MLSC
        if (isMasterListSigner(cert)) {
            info.type = CertificateType::MLSC;
            return info;
        }

        // 2. Check Extended Key Usage for DL Signer
        if (isDeviationListSigner(cert)) {
            info.type = CertificateType::DL_SIGNER;
            return info;
        }

        // 3. Check if it's a CA certificate
        if (info.is_ca && info.has_key_cert_sign) {
            if (info.is_self_signed) {
                // Self-signed CA → CSCA (Root CA)
                info.type = CertificateType::CSCA;
            } else {
                // Non-self-signed CA → Link Certificate (Intermediate CA)
                info.type = CertificateType::LINK_CERT;
            }
            return info;
        }

        // 4. Default: Document Signer Certificate
        // Non-CA certificate used for signing travel documents
        info.type = CertificateType::DSC;

        return info;

    } catch (const std::exception& e) {
        info.error_message = std::string("Exception during type detection: ") + e.what();
        info.type = CertificateType::UNKNOWN;
        return info;
    }
}

std::string CertTypeDetector::typeToString(CertificateType type) {
    switch (type) {
        case CertificateType::CSCA:         return "CSCA";
        case CertificateType::DSC:          return "DSC";
        case CertificateType::DSC_NC:       return "DSC_NC";
        case CertificateType::MLSC:         return "MLSC";
        case CertificateType::LINK_CERT:    return "LINK_CERT";
        case CertificateType::DL_SIGNER:    return "DL_SIGNER";
        case CertificateType::UNKNOWN:
        default:                            return "UNKNOWN";
    }
}

CertificateType CertTypeDetector::stringToType(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    if (upper == "CSCA")        return CertificateType::CSCA;
    if (upper == "DSC")         return CertificateType::DSC;
    if (upper == "DSC_NC")      return CertificateType::DSC_NC;
    if (upper == "MLSC")        return CertificateType::MLSC;
    if (upper == "LINK_CERT")   return CertificateType::LINK_CERT;
    if (upper == "DL_SIGNER")   return CertificateType::DL_SIGNER;

    return CertificateType::UNKNOWN;
}

bool CertTypeDetector::isMasterListSigner(X509* cert) {
    // OID: 2.23.136.1.1.9 (id-icao-mrtd-security-masterListSigner)
    return hasExtendedKeyUsage(cert, "2.23.136.1.1.9");
}

bool CertTypeDetector::isDeviationListSigner(X509* cert) {
    // OID: 2.23.136.1.1.10 (id-icao-mrtd-security-deviationListSigner)
    return hasExtendedKeyUsage(cert, "2.23.136.1.1.10");
}

bool CertTypeDetector::isDocumentSigner(X509* cert) {
    // OID: 2.23.136.1.1.6 (id-icao-mrtd-security-aaProtocol)
    return hasExtendedKeyUsage(cert, "2.23.136.1.1.6");
}

bool CertTypeDetector::isCA(X509* cert) {
    // Get Basic Constraints extension
    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr)
    );

    if (!bc) {
        // No Basic Constraints extension → Not a CA
        return false;
    }

    bool is_ca = (bc->ca != 0);
    BASIC_CONSTRAINTS_free(bc);

    return is_ca;
}

bool CertTypeDetector::isSelfSigned(X509* cert) {
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    // Compare DNs
    int cmp = X509_NAME_cmp(subject, issuer);
    return (cmp == 0);
}

std::string CertTypeDetector::extractCountry(X509* cert) {
    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) {
        return "";
    }

    // Get country (C=) component
    int pos = X509_NAME_get_index_by_NID(subject, NID_countryName, -1);
    if (pos == -1) {
        return "";
    }

    X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, pos);
    if (!entry) {
        return "";
    }

    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) {
        return "";
    }

    const unsigned char* str = ASN1_STRING_get0_data(data);
    int len = ASN1_STRING_length(data);

    return std::string(reinterpret_cast<const char*>(str), len);
}

bool CertTypeDetector::hasKeyCertSign(X509* cert) {
    // Get Key Usage extension
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr)
    );

    if (!usage) {
        // No Key Usage extension
        return false;
    }

    // Check keyCertSign bit (bit 5)
    // Bit numbering: digitalSignature(0), nonRepudiation(1), keyEncipherment(2),
    //                dataEncipherment(3), keyAgreement(4), keyCertSign(5), ...
    int has_kcs = ASN1_BIT_STRING_get_bit(usage, 5);

    ASN1_BIT_STRING_free(usage);

    return (has_kcs == 1);
}

std::string CertTypeDetector::calculateFingerprint(X509* cert) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;

    // Calculate SHA-256 hash
    if (!X509_digest(cert, EVP_sha256(), md, &md_len)) {
        return "";
    }

    // Convert to hex string
    std::ostringstream oss;
    for (unsigned int i = 0; i < md_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(md[i]);
    }

    return oss.str();
}

std::string CertTypeDetector::nameToString(X509_NAME* name) {
    if (!name) {
        return "";
    }

    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return "";
    }

    // Print in RFC2253 format
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);

    // Read result
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);

    std::string result;
    if (data && len > 0) {
        result.assign(data, len);
    }

    BIO_free(bio);
    return result;
}

bool CertTypeDetector::hasExtendedKeyUsage(X509* cert, const std::string& oid_str) {
    // Get Extended Key Usage extension
    EXTENDED_KEY_USAGE* eku = static_cast<EXTENDED_KEY_USAGE*>(
        X509_get_ext_d2i(cert, NID_ext_key_usage, nullptr, nullptr)
    );

    if (!eku) {
        // No Extended Key Usage extension
        return false;
    }

    // Convert OID string to ASN1_OBJECT
    ASN1_OBJECT* target_oid = OBJ_txt2obj(oid_str.c_str(), 1);
    if (!target_oid) {
        EXTENDED_KEY_USAGE_free(eku);
        return false;
    }

    // Check if target OID is in the EKU list
    bool found = false;
    int num_oids = sk_ASN1_OBJECT_num(eku);
    for (int i = 0; i < num_oids; ++i) {
        ASN1_OBJECT* oid = sk_ASN1_OBJECT_value(eku, i);
        if (OBJ_cmp(oid, target_oid) == 0) {
            found = true;
            break;
        }
    }

    ASN1_OBJECT_free(target_oid);
    EXTENDED_KEY_USAGE_free(eku);

    return found;
}

} // namespace certificate_parser
} // namespace icao

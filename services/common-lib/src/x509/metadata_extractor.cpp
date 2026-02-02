/**
 * @file metadata_extractor.cpp
 * @brief X.509 certificate metadata extraction implementation
 *
 * Comprehensive metadata extraction using OpenSSL 3.x APIs.
 * Handles all extension types defined in X.509v3.
 */

#include "icao/x509/metadata_extractor.h"
#include "icao/x509/dn_parser.h"
#include "icao/utils/time_utils.h"
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/x509v3.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <ctime>

namespace icao {
namespace x509 {

namespace {
    /**
     * @brief Convert ASN1_OCTET_STRING to hex string
     */
    std::string octetStringToHex(const ASN1_OCTET_STRING* oct_str) {
        if (!oct_str) {
            return "";
        }

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');

        for (int i = 0; i < oct_str->length; ++i) {
            oss << std::setw(2) << static_cast<int>(oct_str->data[i]);
        }

        return oss.str();
    }
}

int getVersion(X509* cert) {
    if (!cert) {
        return 0;
    }

    return X509_get_version(cert);
}

std::string getSerialNumber(X509* cert) {
    if (!cert) {
        return "";
    }

    const ASN1_INTEGER* serial = X509_get0_serialNumber(cert);
    return utils::asn1IntegerToHex(serial);
}

std::optional<std::string> getSignatureAlgorithm(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    int nid = X509_get_signature_nid(cert);
    if (nid == NID_undef) {
        return std::nullopt;
    }

    const char* name = OBJ_nid2ln(nid);
    if (!name) {
        return std::nullopt;
    }

    return std::string(name);
}

std::optional<std::string> getSignatureHashAlgorithm(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    int sig_nid = X509_get_signature_nid(cert);
    if (sig_nid == NID_undef) {
        return std::nullopt;
    }

    // Map signature algorithm to hash algorithm
    int hash_nid = NID_undef;

    switch (sig_nid) {
        case NID_sha1WithRSAEncryption:
        case NID_sha1WithRSA:
        case NID_ecdsa_with_SHA1:
            hash_nid = NID_sha1;
            break;
        case NID_sha256WithRSAEncryption:
        case NID_ecdsa_with_SHA256:
            hash_nid = NID_sha256;
            break;
        case NID_sha384WithRSAEncryption:
        case NID_ecdsa_with_SHA384:
            hash_nid = NID_sha384;
            break;
        case NID_sha512WithRSAEncryption:
        case NID_ecdsa_with_SHA512:
            hash_nid = NID_sha512;
            break;
        default:
            return std::nullopt;
    }

    const char* hash_name = OBJ_nid2sn(hash_nid);
    if (!hash_name) {
        return std::nullopt;
    }

    // Convert to uppercase (SHA-256 format)
    std::string result(hash_name);
    for (char& c : result) {
        c = std::toupper(c);
    }

    return result;
}

std::optional<std::string> getPublicKeyAlgorithm(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    EVP_PKEY* pkey = X509_get_pubkey(cert);
    if (!pkey) {
        return std::nullopt;
    }

    int key_type = EVP_PKEY_base_id(pkey);
    EVP_PKEY_free(pkey);

    switch (key_type) {
        case EVP_PKEY_RSA:
            return "RSA";
        case EVP_PKEY_EC:
            return "ECDSA";
        case EVP_PKEY_DSA:
            return "DSA";
        default:
            return std::nullopt;
    }
}

std::optional<int> getPublicKeySize(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    EVP_PKEY* pkey = X509_get_pubkey(cert);
    if (!pkey) {
        return std::nullopt;
    }

    int bits = EVP_PKEY_bits(pkey);
    EVP_PKEY_free(pkey);

    if (bits <= 0) {
        return std::nullopt;
    }

    return bits;
}

std::optional<std::string> getPublicKeyCurve(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    EVP_PKEY* pkey = X509_get_pubkey(cert);
    if (!pkey) {
        return std::nullopt;
    }

    if (EVP_PKEY_base_id(pkey) != EVP_PKEY_EC) {
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    EC_KEY* ec_key = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ec_key) {
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    if (!group) {
        EC_KEY_free(ec_key);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    int nid = EC_GROUP_get_curve_name(group);
    const char* curve_name = OBJ_nid2sn(nid);

    EC_KEY_free(ec_key);
    EVP_PKEY_free(pkey);

    if (!curve_name) {
        return std::nullopt;
    }

    return std::string(curve_name);
}

std::vector<std::string> getKeyUsage(X509* cert) {
    std::vector<std::string> usages;

    if (!cert) {
        return usages;
    }

    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));

    if (!usage) {
        return usages;
    }

    // Check each key usage bit
    if (ASN1_BIT_STRING_get_bit(usage, 0)) usages.push_back("digitalSignature");
    if (ASN1_BIT_STRING_get_bit(usage, 1)) usages.push_back("nonRepudiation");
    if (ASN1_BIT_STRING_get_bit(usage, 2)) usages.push_back("keyEncipherment");
    if (ASN1_BIT_STRING_get_bit(usage, 3)) usages.push_back("dataEncipherment");
    if (ASN1_BIT_STRING_get_bit(usage, 4)) usages.push_back("keyAgreement");
    if (ASN1_BIT_STRING_get_bit(usage, 5)) usages.push_back("keyCertSign");
    if (ASN1_BIT_STRING_get_bit(usage, 6)) usages.push_back("cRLSign");
    if (ASN1_BIT_STRING_get_bit(usage, 7)) usages.push_back("encipherOnly");
    if (ASN1_BIT_STRING_get_bit(usage, 8)) usages.push_back("decipherOnly");

    ASN1_BIT_STRING_free(usage);

    return usages;
}

std::vector<std::string> getExtendedKeyUsage(X509* cert) {
    std::vector<std::string> usages;

    if (!cert) {
        return usages;
    }

    EXTENDED_KEY_USAGE* ext_usage = static_cast<EXTENDED_KEY_USAGE*>(
        X509_get_ext_d2i(cert, NID_ext_key_usage, nullptr, nullptr));

    if (!ext_usage) {
        return usages;
    }

    int num = sk_ASN1_OBJECT_num(ext_usage);
    for (int i = 0; i < num; ++i) {
        ASN1_OBJECT* obj = sk_ASN1_OBJECT_value(ext_usage, i);
        int nid = OBJ_obj2nid(obj);
        const char* name = OBJ_nid2sn(nid);
        if (name) {
            usages.push_back(name);
        }
    }

    sk_ASN1_OBJECT_pop_free(ext_usage, ASN1_OBJECT_free);

    return usages;
}

std::optional<bool> isCA(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));

    if (!bc) {
        return std::nullopt;
    }

    bool is_ca = (bc->ca != 0);
    BASIC_CONSTRAINTS_free(bc);

    return is_ca;
}

std::optional<int> getPathLenConstraint(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));

    if (!bc) {
        return std::nullopt;
    }

    std::optional<int> path_len = std::nullopt;

    if (bc->pathlen) {
        long len = ASN1_INTEGER_get(bc->pathlen);
        path_len = static_cast<int>(len);
    }

    BASIC_CONSTRAINTS_free(bc);

    return path_len;
}

std::optional<std::string> getSubjectKeyIdentifier(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    ASN1_OCTET_STRING* ski = static_cast<ASN1_OCTET_STRING*>(
        X509_get_ext_d2i(cert, NID_subject_key_identifier, nullptr, nullptr));

    if (!ski) {
        return std::nullopt;
    }

    std::string hex = octetStringToHex(ski);
    ASN1_OCTET_STRING_free(ski);

    if (hex.empty()) {
        return std::nullopt;
    }

    return hex;
}

std::optional<std::string> getAuthorityKeyIdentifier(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    AUTHORITY_KEYID* akid = static_cast<AUTHORITY_KEYID*>(
        X509_get_ext_d2i(cert, NID_authority_key_identifier, nullptr, nullptr));

    if (!akid || !akid->keyid) {
        if (akid) AUTHORITY_KEYID_free(akid);
        return std::nullopt;
    }

    std::string hex = octetStringToHex(akid->keyid);
    AUTHORITY_KEYID_free(akid);

    if (hex.empty()) {
        return std::nullopt;
    }

    return hex;
}

std::vector<std::string> getCrlDistributionPoints(X509* cert) {
    std::vector<std::string> urls;

    if (!cert) {
        return urls;
    }

    CRL_DIST_POINTS* crld = static_cast<CRL_DIST_POINTS*>(
        X509_get_ext_d2i(cert, NID_crl_distribution_points, nullptr, nullptr));

    if (!crld) {
        return urls;
    }

    int num = sk_DIST_POINT_num(crld);
    for (int i = 0; i < num; ++i) {
        DIST_POINT* dp = sk_DIST_POINT_value(crld, i);
        if (!dp || !dp->distpoint) {
            continue;
        }

        if (dp->distpoint->type == 0) { // fullname
            GENERAL_NAMES* names = dp->distpoint->name.fullname;
            int name_count = sk_GENERAL_NAME_num(names);

            for (int j = 0; j < name_count; ++j) {
                GENERAL_NAME* name = sk_GENERAL_NAME_value(names, j);
                if (name->type == GEN_URI) {
                    ASN1_STRING* uri = name->d.uniformResourceIdentifier;
                    const char* url = reinterpret_cast<const char*>(ASN1_STRING_get0_data(uri));
                    if (url) {
                        urls.push_back(url);
                    }
                }
            }
        }
    }

    CRL_DIST_POINTS_free(crld);

    return urls;
}

std::optional<std::string> getOcspResponderUrl(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    AUTHORITY_INFO_ACCESS* info = static_cast<AUTHORITY_INFO_ACCESS*>(
        X509_get_ext_d2i(cert, NID_info_access, nullptr, nullptr));

    if (!info) {
        return std::nullopt;
    }

    int num = sk_ACCESS_DESCRIPTION_num(info);
    std::optional<std::string> ocsp_url = std::nullopt;

    for (int i = 0; i < num; ++i) {
        ACCESS_DESCRIPTION* ad = sk_ACCESS_DESCRIPTION_value(info, i);
        if (OBJ_obj2nid(ad->method) == NID_ad_OCSP) {
            if (ad->location->type == GEN_URI) {
                ASN1_STRING* uri = ad->location->d.uniformResourceIdentifier;
                const char* url = reinterpret_cast<const char*>(ASN1_STRING_get0_data(uri));
                if (url) {
                    ocsp_url = std::string(url);
                    break;
                }
            }
        }
    }

    AUTHORITY_INFO_ACCESS_free(info);

    return ocsp_url;
}

std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
getValidityPeriod(X509* cert) {
    auto zero = std::chrono::system_clock::time_point{};

    if (!cert) {
        return {zero, zero};
    }

    const ASN1_TIME* not_before = X509_get0_notBefore(cert);
    const ASN1_TIME* not_after = X509_get0_notAfter(cert);

    if (!not_before || !not_after) {
        return {zero, zero};
    }

    return {
        utils::asn1TimeToTimePoint(not_before),
        utils::asn1TimeToTimePoint(not_after)
    };
}

bool isCurrentlyValid(X509* cert) {
    if (!cert) {
        return false;
    }

    const ASN1_TIME* not_before = X509_get0_notBefore(cert);
    const ASN1_TIME* not_after = X509_get0_notAfter(cert);

    if (!not_before || !not_after) {
        return false;
    }

    // Check if current time is after notBefore
    if (X509_cmp_time(not_before, nullptr) > 0) {
        return false;
    }

    // Check if current time is before notAfter
    if (X509_cmp_time(not_after, nullptr) < 0) {
        return false;
    }

    return true;
}

bool isExpired(X509* cert) {
    if (!cert) {
        return true;
    }

    const ASN1_TIME* not_after = X509_get0_notAfter(cert);
    if (!not_after) {
        return true;
    }

    // Returns -1 if time is after notAfter (expired)
    return (X509_cmp_time(not_after, nullptr) < 0);
}

int getDaysUntilExpiration(X509* cert) {
    if (!cert) {
        return 0;
    }

    auto validity = getValidityPeriod(cert);
    auto now = std::chrono::system_clock::now();

    auto duration = validity.second - now;
    auto days = std::chrono::duration_cast<std::chrono::hours>(duration).count() / 24;

    return static_cast<int>(days);
}

CertificateMetadata extractMetadata(X509* cert) {
    CertificateMetadata meta;

    if (!cert) {
        return meta;
    }

    // Basic information
    meta.version = getVersion(cert);
    meta.serialNumber = getSerialNumber(cert);

    // Algorithm information
    meta.signatureAlgorithm = getSignatureAlgorithm(cert);
    meta.signatureHashAlgorithm = getSignatureHashAlgorithm(cert);
    meta.publicKeyAlgorithm = getPublicKeyAlgorithm(cert);
    meta.publicKeySize = getPublicKeySize(cert);
    meta.publicKeyCurve = getPublicKeyCurve(cert);

    // Key usage
    meta.keyUsage = getKeyUsage(cert);
    meta.extendedKeyUsage = getExtendedKeyUsage(cert);

    // CA information
    meta.isCA = isCA(cert);
    meta.pathLenConstraint = getPathLenConstraint(cert);

    // Identifiers
    meta.subjectKeyIdentifier = getSubjectKeyIdentifier(cert);
    meta.authorityKeyIdentifier = getAuthorityKeyIdentifier(cert);

    // Distribution points
    meta.crlDistributionPoints = getCrlDistributionPoints(cert);
    meta.ocspResponderUrl = getOcspResponderUrl(cert);

    // Validity
    auto validity = getValidityPeriod(cert);
    meta.validFrom = validity.first;
    meta.validTo = validity.second;

    // Self-signed check
    meta.isSelfSigned = isSelfSigned(cert);

    return meta;
}

} // namespace x509
} // namespace icao

#include "x509_metadata_extractor.h"
#include <spdlog/spdlog.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace x509 {

// =============================================================================
// Main Extraction Function
// =============================================================================

CertificateMetadata extractMetadata(X509* cert)
{
    CertificateMetadata metadata;

    if (!cert) {
        spdlog::warn("[X509Metadata] NULL certificate pointer");
        return metadata;
    }

    try {
        // Basic Fields
        metadata.version = getVersion(cert);
        metadata.signatureAlgorithm = getSignatureAlgorithm(cert);
        metadata.signatureHashAlgorithm = extractHashAlgorithm(metadata.signatureAlgorithm);

        // Public Key Info
        metadata.publicKeyAlgorithm = getPublicKeyAlgorithm(cert);
        metadata.publicKeySize = getPublicKeySize(cert);
        metadata.publicKeyCurve = getPublicKeyCurve(cert);

        // Key Usage
        metadata.keyUsage = getKeyUsage(cert);
        metadata.extendedKeyUsage = getExtendedKeyUsage(cert);

        // Basic Constraints
        metadata.isCA = isCA(cert);
        metadata.pathLenConstraint = getPathLenConstraint(cert);

        // Identifiers
        metadata.subjectKeyIdentifier = getSubjectKeyIdentifier(cert);
        metadata.authorityKeyIdentifier = getAuthorityKeyIdentifier(cert);

        // CRL & OCSP
        metadata.crlDistributionPoints = getCrlDistributionPoints(cert);
        metadata.ocspResponderUrl = getOcspResponderUrl(cert);

        // Computed
        metadata.isSelfSigned = isSelfSigned(cert);

    } catch (const std::exception& e) {
        spdlog::error("[X509Metadata] Extraction failed: {}", e.what());
    }

    return metadata;
}

// =============================================================================
// Basic Fields
// =============================================================================

int getVersion(X509* cert)
{
    long version = X509_get_version(cert);  // Returns 0, 1, or 2
    return static_cast<int>(version);
}

std::string getSignatureAlgorithm(X509* cert)
{
    const X509_ALGOR* alg = X509_get0_tbs_sigalg(cert);
    if (!alg) return "unknown";

    char buffer[128] = {0};
    OBJ_obj2txt(buffer, sizeof(buffer), alg->algorithm, 0);
    return std::string(buffer);
}

std::string extractHashAlgorithm(const std::string& signatureAlgorithm)
{
    std::string lower = signatureAlgorithm;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Map signature algorithms to hash algorithms
    if (lower.find("sha512") != std::string::npos) return "SHA-512";
    if (lower.find("sha384") != std::string::npos) return "SHA-384";
    if (lower.find("sha256") != std::string::npos) return "SHA-256";
    if (lower.find("sha224") != std::string::npos) return "SHA-224";
    if (lower.find("sha1") != std::string::npos) return "SHA-1";
    if (lower.find("md5") != std::string::npos) return "MD5";
    if (lower.find("md2") != std::string::npos) return "MD2";

    return "unknown";
}

// =============================================================================
// Public Key Info
// =============================================================================

std::string getPublicKeyAlgorithm(X509* cert)
{
    EVP_PKEY* pkey = X509_get0_pubkey(cert);
    if (!pkey) return "unknown";

    int type = EVP_PKEY_base_id(pkey);

    switch (type) {
        case EVP_PKEY_RSA:
        case EVP_PKEY_RSA2:
            return "RSA";
        case EVP_PKEY_DSA:
        case EVP_PKEY_DSA1:
        case EVP_PKEY_DSA2:
        case EVP_PKEY_DSA3:
        case EVP_PKEY_DSA4:
            return "DSA";
        case EVP_PKEY_EC:
            return "ECDSA";
        case EVP_PKEY_ED25519:
            return "Ed25519";
        case EVP_PKEY_ED448:
            return "Ed448";
        case EVP_PKEY_DH:
            return "DH";
        default:
            return "unknown";
    }
}

int getPublicKeySize(X509* cert)
{
    EVP_PKEY* pkey = X509_get0_pubkey(cert);
    if (!pkey) return 0;

    return EVP_PKEY_bits(pkey);
}

std::optional<std::string> getPublicKeyCurve(X509* cert)
{
    EVP_PKEY* pkey = X509_get0_pubkey(cert);
    if (!pkey || EVP_PKEY_base_id(pkey) != EVP_PKEY_EC) {
        return std::nullopt;
    }

    const EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(pkey);
    if (!ec_key) return std::nullopt;

    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    if (!group) return std::nullopt;

    int nid = EC_GROUP_get_curve_name(group);
    if (nid == 0) return std::nullopt;

    const char* curve_name = OBJ_nid2sn(nid);
    if (!curve_name) return std::nullopt;

    return std::string(curve_name);
}

// =============================================================================
// Key Usage Extensions
// =============================================================================

std::vector<std::string> getKeyUsage(X509* cert)
{
    std::vector<std::string> usages;

    ASN1_BIT_STRING* usage = (ASN1_BIT_STRING*)X509_get_ext_d2i(
        cert, NID_key_usage, nullptr, nullptr);

    if (!usage) return usages;

    // Bit positions defined in RFC 5280
    const char* usage_names[] = {
        "digitalSignature",   // 0
        "nonRepudiation",     // 1 (or contentCommitment)
        "keyEncipherment",    // 2
        "dataEncipherment",   // 3
        "keyAgreement",       // 4
        "keyCertSign",        // 5
        "cRLSign",            // 6
        "encipherOnly",       // 7
        "decipherOnly"        // 8
    };

    for (int i = 0; i < 9; i++) {
        if (ASN1_BIT_STRING_get_bit(usage, i) == 1) {
            usages.push_back(usage_names[i]);
        }
    }

    ASN1_BIT_STRING_free(usage);
    return usages;
}

std::vector<std::string> getExtendedKeyUsage(X509* cert)
{
    std::vector<std::string> usages;

    EXTENDED_KEY_USAGE* ext_usage = (EXTENDED_KEY_USAGE*)X509_get_ext_d2i(
        cert, NID_ext_key_usage, nullptr, nullptr);

    if (!ext_usage) return usages;

    for (int i = 0; i < sk_ASN1_OBJECT_num(ext_usage); i++) {
        ASN1_OBJECT* obj = sk_ASN1_OBJECT_value(ext_usage, i);
        char buffer[128] = {0};
        OBJ_obj2txt(buffer, sizeof(buffer), obj, 0);
        usages.push_back(std::string(buffer));
    }

    sk_ASN1_OBJECT_pop_free(ext_usage, ASN1_OBJECT_free);
    return usages;
}

// =============================================================================
// Basic Constraints
// =============================================================================

bool isCA(X509* cert)
{
    BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(
        cert, NID_basic_constraints, nullptr, nullptr);

    if (!bc) return false;

    bool is_ca = (bc->ca != 0);
    BASIC_CONSTRAINTS_free(bc);
    return is_ca;
}

std::optional<int> getPathLenConstraint(X509* cert)
{
    BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(
        cert, NID_basic_constraints, nullptr, nullptr);

    if (!bc) return std::nullopt;

    std::optional<int> pathLen = std::nullopt;
    if (bc->pathlen) {
        pathLen = ASN1_INTEGER_get(bc->pathlen);
    }

    BASIC_CONSTRAINTS_free(bc);
    return pathLen;
}

// =============================================================================
// Identifiers
// =============================================================================

std::optional<std::string> getSubjectKeyIdentifier(X509* cert)
{
    ASN1_OCTET_STRING* ski = (ASN1_OCTET_STRING*)X509_get_ext_d2i(
        cert, NID_subject_key_identifier, nullptr, nullptr);

    if (!ski) return std::nullopt;

    std::string hex = bytesToHex(ski->data, ski->length);
    ASN1_OCTET_STRING_free(ski);
    return hex;
}

std::optional<std::string> getAuthorityKeyIdentifier(X509* cert)
{
    AUTHORITY_KEYID* aki = (AUTHORITY_KEYID*)X509_get_ext_d2i(
        cert, NID_authority_key_identifier, nullptr, nullptr);

    if (!aki || !aki->keyid) {
        if (aki) AUTHORITY_KEYID_free(aki);
        return std::nullopt;
    }

    std::string hex = bytesToHex(aki->keyid->data, aki->keyid->length);
    AUTHORITY_KEYID_free(aki);
    return hex;
}

// =============================================================================
// CRL & OCSP
// =============================================================================

std::vector<std::string> getCrlDistributionPoints(X509* cert)
{
    std::vector<std::string> urls;

    STACK_OF(DIST_POINT)* crldp = (STACK_OF(DIST_POINT)*)X509_get_ext_d2i(
        cert, NID_crl_distribution_points, nullptr, nullptr);

    if (!crldp) return urls;

    for (int i = 0; i < sk_DIST_POINT_num(crldp); i++) {
        DIST_POINT* dp = sk_DIST_POINT_value(crldp, i);
        if (!dp->distpoint || dp->distpoint->type != 0) continue;

        GENERAL_NAMES* names = dp->distpoint->name.fullname;
        if (!names) continue;

        for (int j = 0; j < sk_GENERAL_NAME_num(names); j++) {
            GENERAL_NAME* name = sk_GENERAL_NAME_value(names, j);
            if (name->type == GEN_URI) {
                ASN1_IA5STRING* uri = name->d.uniformResourceIdentifier;
                urls.push_back(std::string((char*)uri->data, uri->length));
            }
        }
    }

    sk_DIST_POINT_pop_free(crldp, DIST_POINT_free);
    return urls;
}

std::optional<std::string> getOcspResponderUrl(X509* cert)
{
    AUTHORITY_INFO_ACCESS* aia = (AUTHORITY_INFO_ACCESS*)X509_get_ext_d2i(
        cert, NID_info_access, nullptr, nullptr);

    if (!aia) return std::nullopt;

    std::optional<std::string> ocsp_url = std::nullopt;

    for (int i = 0; i < sk_ACCESS_DESCRIPTION_num(aia); i++) {
        ACCESS_DESCRIPTION* ad = sk_ACCESS_DESCRIPTION_value(aia, i);

        // Check if this is OCSP (OID 1.3.6.1.5.5.7.48.1)
        if (OBJ_obj2nid(ad->method) == NID_ad_OCSP) {
            if (ad->location->type == GEN_URI) {
                ASN1_IA5STRING* uri = ad->location->d.uniformResourceIdentifier;
                ocsp_url = std::string((char*)uri->data, uri->length);
                break;
            }
        }
    }

    sk_ACCESS_DESCRIPTION_pop_free(aia, ACCESS_DESCRIPTION_free);
    return ocsp_url;
}

// =============================================================================
// Computed/Derived
// =============================================================================

bool isSelfSigned(X509* cert)
{
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    return (X509_NAME_cmp(subject, issuer) == 0);
}

// =============================================================================
// Utility Functions
// =============================================================================

std::string bytesToHex(const unsigned char* data, size_t len)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');

    for (size_t i = 0; i < len; i++) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }

    return ss.str();
}

} // namespace x509

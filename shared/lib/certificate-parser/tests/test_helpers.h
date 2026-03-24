/**
 * @file test_helpers.h
 * @brief Helper functions for creating test X.509 certificates using OpenSSL C API
 *
 * All test certificates are generated in-memory -- no file-based test data required.
 */

#pragma once

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <cstring>

namespace test_helpers {

/**
 * @brief RAII wrapper for EVP_PKEY
 */
struct EvpPkeyDeleter {
    void operator()(EVP_PKEY* p) const { if (p) EVP_PKEY_free(p); }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

/**
 * @brief RAII wrapper for X509
 */
struct X509Deleter {
    void operator()(X509* p) const { if (p) X509_free(p); }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

/**
 * @brief Generate an RSA key pair
 *
 * @param bits Key size in bits (default 2048)
 * @return EVP_PKEY* (caller owns) or nullptr on failure
 */
inline EVP_PKEY* generateRsaKey(int bits = 2048) {
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) return nullptr;

    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0 ||
        EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

/**
 * @brief Add an X.509 v3 extension to a certificate
 */
inline bool addExtension(X509* cert, int nid, const char* value, X509* issuer = nullptr) {
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer ? issuer : cert, cert, nullptr, nullptr, 0);

    X509_EXTENSION* ext = X509V3_EXT_nconf(nullptr, &ctx, OBJ_nid2sn(nid), value);
    if (!ext) return false;

    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);
    return true;
}

/**
 * @brief Create a minimal self-signed certificate (CSCA-like: CA=TRUE, keyCertSign)
 *
 * @param country ISO 3166-1 alpha-2 country code
 * @param cn Common Name
 * @param validDays Number of days the certificate is valid
 * @param pkey If non-null, use this key; otherwise generate a new one
 * @return X509* (caller owns) or nullptr on failure
 */
inline X509* createSelfSignedCert(
    const std::string& country = "US",
    const std::string& cn = "Test CSCA",
    int validDays = 365,
    EVP_PKEY* pkey = nullptr)
{
    EvpPkeyPtr ownedKey;
    if (!pkey) {
        ownedKey.reset(generateRsaKey(2048));
        pkey = ownedKey.get();
    }
    if (!pkey) return nullptr;

    X509* cert = X509_new();
    if (!cert) return nullptr;

    // Version 3
    X509_set_version(cert, 2);

    // Serial number
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

    // Validity
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), static_cast<long>(validDays) * 86400);

    // Subject = Issuer (self-signed)
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>(country.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("Test Organization"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_issuer_name(cert, name);

    // Public key
    X509_set_pubkey(cert, pkey);

    // Extensions: CA certificate with keyCertSign + cRLSign
    addExtension(cert, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    addExtension(cert, NID_key_usage, "critical,keyCertSign,cRLSign");
    addExtension(cert, NID_subject_key_identifier, "hash");

    // Sign
    if (X509_sign(cert, pkey, EVP_sha256()) == 0) {
        X509_free(cert);
        return nullptr;
    }

    return cert;
}

/**
 * @brief Create a non-CA certificate (DSC-like: no CA flag, digitalSignature)
 *
 * @param issuerCert Issuer certificate (or nullptr for self-signed DSC)
 * @param issuerKey Issuer private key (or nullptr to generate self-signed)
 * @param country Country code
 * @param cn Common Name
 * @param validDays Validity period
 * @return X509* (caller owns) or nullptr on failure
 */
inline X509* createDscCert(
    X509* issuerCert = nullptr,
    EVP_PKEY* issuerKey = nullptr,
    const std::string& country = "US",
    const std::string& cn = "Test DSC",
    int validDays = 180)
{
    EvpPkeyPtr ownedKey(generateRsaKey(2048));
    EVP_PKEY* subjectKey = ownedKey.get();
    if (!subjectKey) return nullptr;

    X509* cert = X509_new();
    if (!cert) return nullptr;

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 100);

    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), static_cast<long>(validDays) * 86400);

    // Subject
    X509_NAME* subjectName = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(subjectName, "C", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>(country.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subjectName, "CN", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);

    // Issuer
    if (issuerCert) {
        X509_set_issuer_name(cert, X509_get_subject_name(issuerCert));
    } else {
        X509_set_issuer_name(cert, subjectName);
    }

    X509_set_pubkey(cert, subjectKey);

    // Extensions: Non-CA, digitalSignature only
    addExtension(cert, NID_basic_constraints, "critical,CA:FALSE");
    addExtension(cert, NID_key_usage, "critical,digitalSignature");

    // Sign with issuer key if provided, otherwise self-sign
    EVP_PKEY* signingKey = issuerKey ? issuerKey : subjectKey;
    if (X509_sign(cert, signingKey, EVP_sha256()) == 0) {
        X509_free(cert);
        return nullptr;
    }

    return cert;
}

/**
 * @brief Create an expired certificate
 *
 * @param country Country code
 * @param cn Common Name
 * @return X509* (caller owns) or nullptr on failure
 */
inline X509* createExpiredCert(
    const std::string& country = "US",
    const std::string& cn = "Expired Cert")
{
    EvpPkeyPtr ownedKey(generateRsaKey(2048));
    EVP_PKEY* pkey = ownedKey.get();
    if (!pkey) return nullptr;

    X509* cert = X509_new();
    if (!cert) return nullptr;

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 200);

    // Expired: notBefore = 2 years ago, notAfter = 1 year ago
    X509_gmtime_adj(X509_getm_notBefore(cert), -2 * 365 * 86400L);
    X509_gmtime_adj(X509_getm_notAfter(cert), -1 * 365 * 86400L);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>(country.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_set_pubkey(cert, pkey);

    addExtension(cert, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    addExtension(cert, NID_key_usage, "critical,keyCertSign,cRLSign");

    if (X509_sign(cert, pkey, EVP_sha256()) == 0) {
        X509_free(cert);
        return nullptr;
    }

    return cert;
}

/**
 * @brief Create a not-yet-valid certificate (notBefore in the future)
 *
 * @return X509* (caller owns) or nullptr on failure
 */
inline X509* createNotYetValidCert() {
    EvpPkeyPtr ownedKey(generateRsaKey(2048));
    EVP_PKEY* pkey = ownedKey.get();
    if (!pkey) return nullptr;

    X509* cert = X509_new();
    if (!cert) return nullptr;

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 300);

    // notBefore = 1 year from now, notAfter = 2 years from now
    X509_gmtime_adj(X509_getm_notBefore(cert), 365 * 86400L);
    X509_gmtime_adj(X509_getm_notAfter(cert), 2 * 365 * 86400L);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("US"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("Future Cert"), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_set_pubkey(cert, pkey);

    if (X509_sign(cert, pkey, EVP_sha256()) == 0) {
        X509_free(cert);
        return nullptr;
    }

    return cert;
}

/**
 * @brief Create a certificate with MLSC Extended Key Usage
 *
 * OID 2.23.136.1.1.9 = id-icao-mrtd-security-masterListSigner
 *
 * @return X509* (caller owns) or nullptr on failure
 */
inline X509* createMlscCert(
    X509* issuerCert = nullptr,
    EVP_PKEY* issuerKey = nullptr)
{
    EvpPkeyPtr ownedKey(generateRsaKey(2048));
    EVP_PKEY* subjectKey = ownedKey.get();
    if (!subjectKey) return nullptr;

    X509* cert = X509_new();
    if (!cert) return nullptr;

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 400);

    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 365 * 86400L);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("DE"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("Test MLSC"), -1, -1, 0);

    if (issuerCert) {
        X509_set_issuer_name(cert, X509_get_subject_name(issuerCert));
    } else {
        X509_set_issuer_name(cert, name);
    }

    X509_set_pubkey(cert, subjectKey);

    // MLSC-specific EKU: 2.23.136.1.1.9
    // Note: This OID is not registered in default OpenSSL, so we add it as a raw extension.
    // Build the EKU extension manually.
    {
        ASN1_OBJECT* mlscOid = OBJ_txt2obj("2.23.136.1.1.9", 1);
        if (mlscOid) {
            EXTENDED_KEY_USAGE* eku = sk_ASN1_OBJECT_new_null();
            sk_ASN1_OBJECT_push(eku, mlscOid);

            X509_EXTENSION* ext = X509V3_EXT_i2d(NID_ext_key_usage, 0, eku);
            if (ext) {
                X509_add_ext(cert, ext, -1);
                X509_EXTENSION_free(ext);
            }
            sk_ASN1_OBJECT_pop_free(eku, ASN1_OBJECT_free);
        }
    }

    addExtension(cert, NID_basic_constraints, "critical,CA:FALSE");
    addExtension(cert, NID_key_usage, "critical,digitalSignature");

    EVP_PKEY* signingKey = issuerKey ? issuerKey : subjectKey;
    if (X509_sign(cert, signingKey, EVP_sha256()) == 0) {
        X509_free(cert);
        return nullptr;
    }

    return cert;
}

/**
 * @brief Create a Link Certificate (non-self-signed CA with keyCertSign)
 *
 * @param issuerCert Issuer certificate
 * @param issuerKey Issuer private key
 * @return X509* (caller owns) or nullptr on failure
 */
inline X509* createLinkCert(
    X509* issuerCert,
    EVP_PKEY* issuerKey)
{
    if (!issuerCert || !issuerKey) return nullptr;

    EvpPkeyPtr ownedKey(generateRsaKey(2048));
    EVP_PKEY* subjectKey = ownedKey.get();
    if (!subjectKey) return nullptr;

    X509* cert = X509_new();
    if (!cert) return nullptr;

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 500);

    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 365 * 86400L);

    // Subject differs from issuer
    X509_NAME* subjectName = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(subjectName, "C", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("US"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subjectName, "O", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("Test Organization"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subjectName, "CN", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("Test Link CSCA"), -1, -1, 0);

    // Issuer from the issuer cert
    X509_set_issuer_name(cert, X509_get_subject_name(issuerCert));

    X509_set_pubkey(cert, subjectKey);

    // CA certificate with keyCertSign (but NOT self-signed => Link Cert)
    addExtension(cert, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    addExtension(cert, NID_key_usage, "critical,keyCertSign,cRLSign");

    if (X509_sign(cert, issuerKey, EVP_sha256()) == 0) {
        X509_free(cert);
        return nullptr;
    }

    return cert;
}

/**
 * @brief Convert an X509 certificate to PEM string
 */
inline std::string certToPem(X509* cert) {
    if (!cert) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";
    PEM_write_bio_X509(bio, cert);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string pem(data, len);
    BIO_free(bio);
    return pem;
}

/**
 * @brief Convert an X509 certificate to DER bytes
 */
inline std::vector<uint8_t> certToDer(X509* cert) {
    if (!cert) return {};
    int len = i2d_X509(cert, nullptr);
    if (len <= 0) return {};
    std::vector<uint8_t> der(len);
    unsigned char* p = der.data();
    i2d_X509(cert, &p);
    return der;
}

} // namespace test_helpers

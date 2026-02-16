/**
 * @file test_helpers.h
 * @brief Shared test helpers for icao::validation unit tests
 *
 * Provides OpenSSL certificate/CRL/key generation utilities
 * for self-contained tests without DB or LDAP dependencies.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <ctime>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/asn1.h>

namespace test_helpers {

/// RAII wrapper for EVP_PKEY
struct PKeyDeleter { void operator()(EVP_PKEY* p) { EVP_PKEY_free(p); } };
using UniqueKey = std::unique_ptr<EVP_PKEY, PKeyDeleter>;

/// RAII wrapper for X509
struct X509Deleter { void operator()(X509* p) { X509_free(p); } };
using UniqueCert = std::unique_ptr<X509, X509Deleter>;

/// RAII wrapper for X509_CRL
struct CrlDeleter { void operator()(X509_CRL* p) { X509_CRL_free(p); } };
using UniqueCrl = std::unique_ptr<X509_CRL, CrlDeleter>;

// --- Key Generation ---

inline UniqueKey generateRsaKey(int bits = 2048) {
    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, bits, e, nullptr);
    EVP_PKEY_assign_RSA(pkey, rsa);
    BN_free(e);
    return UniqueKey(pkey);
}

inline UniqueKey generateEcKey() {
    EVP_PKEY* pkey = EVP_PKEY_new();
    EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(ec);
    EVP_PKEY_assign_EC_KEY(pkey, ec);
    return UniqueKey(pkey);
}

// --- Certificate Creation ---

/**
 * @brief Create a self-signed root CA (CSCA-style) certificate
 */
inline UniqueCert createRootCa(
    EVP_PKEY* key,
    const std::string& cn,
    const std::string& country = "KR",
    int validDays = 3650,
    const EVP_MD* md = EVP_sha256())
{
    X509* cert = X509_new();
    X509_set_version(cert, 2);  // v3

    // Serial
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

    // Subject = Issuer (self-signed)
    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(country.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Test CA"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);
    X509_NAME_free(name);

    // Validity
    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) - 86400);
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + validDays * 86400L);

    X509_set_pubkey(cert, key);

    // BasicConstraints: CA:TRUE (critical)
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, const_cast<char*>("critical,CA:TRUE"));
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);

    // KeyUsage: keyCertSign, cRLSign (critical)
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, const_cast<char*>("critical,keyCertSign,cRLSign"));
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);

    X509_sign(cert, key, md);
    return UniqueCert(cert);
}

/**
 * @brief Create a DSC (Document Signer Certificate) signed by issuer
 */
inline UniqueCert createDsc(
    EVP_PKEY* dscKey,
    EVP_PKEY* issuerKey,
    X509* issuerCert,
    const std::string& cn,
    const std::string& country = "KR",
    int validDays = 365,
    const EVP_MD* md = EVP_sha256())
{
    X509* cert = X509_new();
    X509_set_version(cert, 2);

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 100);

    // Subject
    X509_NAME* subject = X509_NAME_new();
    X509_NAME_add_entry_by_txt(subject, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(country.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_subject_name(cert, subject);
    X509_NAME_free(subject);

    // Issuer = issuerCert's subject
    X509_set_issuer_name(cert, X509_get_subject_name(issuerCert));

    // Validity
    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) - 86400);
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + validDays * 86400L);

    X509_set_pubkey(cert, dscKey);

    // KeyUsage: digitalSignature (critical)
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuerCert, cert, nullptr, nullptr, 0);
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, const_cast<char*>("critical,digitalSignature"));
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);

    X509_sign(cert, issuerKey, md);
    return UniqueCert(cert);
}

/**
 * @brief Create an expired certificate (notAfter in the past)
 */
inline UniqueCert createExpiredCert(EVP_PKEY* key, const std::string& cn = "Expired Cert") {
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 200);

    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);
    X509_NAME_free(name);

    // Both dates in the past
    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) - 730 * 86400L);
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) - 1 * 86400L);

    X509_set_pubkey(cert, key);
    X509_sign(cert, key, EVP_sha256());
    return UniqueCert(cert);
}

/**
 * @brief Create a not-yet-valid certificate (notBefore in the future)
 */
inline UniqueCert createFutureCert(EVP_PKEY* key, const std::string& cn = "Future Cert") {
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 300);

    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);
    X509_NAME_free(name);

    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) + 365 * 86400L);
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 730 * 86400L);

    X509_set_pubkey(cert, key);
    X509_sign(cert, key, EVP_sha256());
    return UniqueCert(cert);
}

/**
 * @brief Create a Link Certificate (CA:TRUE, keyCertSign, NOT self-signed)
 */
inline UniqueCert createLinkCert(
    EVP_PKEY* linkKey,
    EVP_PKEY* signerKey,
    X509* signerCert,
    const std::string& cn = "Link CSCA")
{
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 50);

    X509_NAME* subject = X509_NAME_new();
    X509_NAME_add_entry_by_txt(subject, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subject, "O", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Test CA"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_subject_name(cert, subject);
    X509_NAME_free(subject);

    // Issuer = signer's subject (NOT self-signed)
    X509_set_issuer_name(cert, X509_get_subject_name(signerCert));

    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) - 86400);
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 3650 * 86400L);

    X509_set_pubkey(cert, linkKey);

    // BasicConstraints: CA:TRUE (critical)
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, signerCert, cert, nullptr, nullptr, 0);
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, const_cast<char*>("critical,CA:TRUE"));
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);

    // KeyUsage: keyCertSign (critical)
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, const_cast<char*>("critical,keyCertSign"));
    X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);

    X509_sign(cert, signerKey, EVP_sha256());
    return UniqueCert(cert);
}

// --- CRL Creation ---

/**
 * @brief Create a CRL with specified revoked serials
 */
inline UniqueCrl createCrl(
    EVP_PKEY* issuerKey,
    X509* issuerCert,
    const std::vector<long>& revokedSerials = {},
    int validDays = 30,
    bool expired = false)
{
    X509_CRL* crl = X509_CRL_new();
    X509_CRL_set_version(crl, 1);
    X509_CRL_set_issuer_name(crl, X509_get_subject_name(issuerCert));

    ASN1_TIME* thisUpdate = ASN1_TIME_new();
    ASN1_TIME_set(thisUpdate, expired ? time(nullptr) - 60 * 86400L : time(nullptr));
    X509_CRL_set1_lastUpdate(crl, thisUpdate);
    ASN1_TIME_free(thisUpdate);

    ASN1_TIME* nextUpdate = ASN1_TIME_new();
    ASN1_TIME_set(nextUpdate, expired ? time(nullptr) - 1 * 86400L : time(nullptr) + validDays * 86400L);
    X509_CRL_set1_nextUpdate(crl, nextUpdate);
    ASN1_TIME_free(nextUpdate);

    for (long serial : revokedSerials) {
        X509_REVOKED* rev = X509_REVOKED_new();

        ASN1_INTEGER* serialAsn1 = ASN1_INTEGER_new();
        ASN1_INTEGER_set(serialAsn1, serial);
        X509_REVOKED_set_serialNumber(rev, serialAsn1);
        ASN1_INTEGER_free(serialAsn1);

        ASN1_TIME* revDate = ASN1_TIME_new();
        ASN1_TIME_set(revDate, time(nullptr) - 7 * 86400L);
        X509_REVOKED_set_revocationDate(rev, revDate);
        ASN1_TIME_free(revDate);

        X509_CRL_add0_revoked(crl, rev);
    }

    X509_CRL_sort(crl);
    X509_CRL_sign(crl, issuerKey, EVP_sha256());
    return UniqueCrl(crl);
}

} // namespace test_helpers

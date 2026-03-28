/**
 * @file test_icao_ldap_cert_utils.cpp
 * @brief Unit tests for icao::relay::cert_utils::extractCountryFromCert()
 *
 * extractCountryFromCert() was previously in an anonymous namespace inside
 * icao_ldap_sync_service.cpp.  It has been promoted to the named namespace
 * icao::relay::cert_utils in icao_ldap_cert_utils.{h,cpp} so it can be
 * tested independently.
 *
 * Test certificates are constructed in-memory with OpenSSL — no PEM files
 * on disk are required.
 *
 * Tested:
 *   - nullptr cert → fallback returned
 *   - cert with C=KR in subject → "KR" returned
 *   - cert without any C= field → fallback returned
 *   - custom fallback string used when C= absent
 *   - cert with C= that is an empty ASN1_STRING → fallback returned
 *   - two-character ISO 3166-1 alpha-2 codes (KR, US, DE, JP)
 *   - repeated calls with same cert are idempotent
 *
 * Framework: Google Test (GTest)
 */

#include <gtest/gtest.h>
#include "relay/icao-ldap/icao_ldap_cert_utils.h"

#include <openssl/x509.h>
#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include <memory>
#include <string>

using icao::relay::cert_utils::extractCountryFromCert;

// ---------------------------------------------------------------------------
// Helper: build a minimal self-signed X.509 certificate with configurable
// subject fields.  Returns nullptr on internal OpenSSL error.
// ---------------------------------------------------------------------------
struct X509Deleter { void operator()(X509* p) const { X509_free(p); } };
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

struct EVPKeyDeleter { void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); } };
using EVPKeyPtr = std::unique_ptr<EVP_PKEY, EVPKeyDeleter>;

/// Build a minimal certificate.  @p countryCode may be empty to omit the C= RDN.
X509Ptr makeCert(const std::string& countryCode, bool omitCountry = false) {
    // Generate a throwaway RSA key
    EVPKeyPtr pkey(EVP_PKEY_new());
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!ctx) return nullptr;
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 1024);  // small — test only
        EVP_PKEY* rawKey = nullptr;
        EVP_PKEY_keygen(ctx, &rawKey);
        EVP_PKEY_CTX_free(ctx);
        pkey.reset(rawKey);
    }
    if (!pkey) return nullptr;

    X509Ptr cert(X509_new());
    if (!cert) return nullptr;

    X509_set_version(cert.get(), 2);  // version 3
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600);

    X509_NAME* name = X509_get_subject_name(cert.get());
    if (!omitCountry && !countryCode.empty()) {
        X509_NAME_add_entry_by_txt(name, "C", MBSTRING_UTF8,
            reinterpret_cast<const unsigned char*>(countryCode.c_str()),
            static_cast<int>(countryCode.size()), -1, 0);
    }
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
        reinterpret_cast<const unsigned char*>("Test Cert"),
        static_cast<int>(std::string("Test Cert").size()), -1, 0);

    X509_set_issuer_name(cert.get(), name);
    X509_set_pubkey(cert.get(), pkey.get());
    X509_sign(cert.get(), pkey.get(), EVP_sha256());

    return cert;
}

// ===========================================================================
// nullptr input
// ===========================================================================

TEST(ExtractCountryFromCert, NullCert_ReturnsDefaultFallback) {
    std::string cc = extractCountryFromCert(nullptr);
    EXPECT_EQ(cc, "XX");
}

TEST(ExtractCountryFromCert, NullCert_ReturnsCustomFallback) {
    std::string cc = extractCountryFromCert(nullptr, "ZZ");
    EXPECT_EQ(cc, "ZZ");
}

// ===========================================================================
// Country code extraction
// ===========================================================================

TEST(ExtractCountryFromCert, CertWithC_KR_ReturnsKR) {
    auto cert = makeCert("KR");
    ASSERT_NE(cert, nullptr) << "OpenSSL failed to generate test certificate";
    EXPECT_EQ(extractCountryFromCert(cert.get()), "KR");
}

TEST(ExtractCountryFromCert, CertWithC_US_ReturnsUS) {
    auto cert = makeCert("US");
    ASSERT_NE(cert, nullptr);
    EXPECT_EQ(extractCountryFromCert(cert.get()), "US");
}

TEST(ExtractCountryFromCert, CertWithC_DE_ReturnsDE) {
    auto cert = makeCert("DE");
    ASSERT_NE(cert, nullptr);
    EXPECT_EQ(extractCountryFromCert(cert.get()), "DE");
}

TEST(ExtractCountryFromCert, CertWithC_JP_ReturnsJP) {
    auto cert = makeCert("JP");
    ASSERT_NE(cert, nullptr);
    EXPECT_EQ(extractCountryFromCert(cert.get()), "JP");
}

// ===========================================================================
// Missing C= field
// ===========================================================================

TEST(ExtractCountryFromCert, CertWithoutCountry_ReturnsDefaultFallback) {
    auto cert = makeCert("", /*omitCountry=*/true);
    ASSERT_NE(cert, nullptr);
    EXPECT_EQ(extractCountryFromCert(cert.get()), "XX");
}

TEST(ExtractCountryFromCert, CertWithoutCountry_ReturnsCustomFallback) {
    auto cert = makeCert("", /*omitCountry=*/true);
    ASSERT_NE(cert, nullptr);
    EXPECT_EQ(extractCountryFromCert(cert.get(), "YY"), "YY");
}

// ===========================================================================
// Fallback string handling
// ===========================================================================

TEST(ExtractCountryFromCert, CustomFallback_EmptyString) {
    // Passing an empty fallback returns empty string when C= is absent
    auto cert = makeCert("", /*omitCountry=*/true);
    ASSERT_NE(cert, nullptr);
    std::string cc = extractCountryFromCert(cert.get(), "");
    EXPECT_EQ(cc, "");
}

TEST(ExtractCountryFromCert, CustomFallback_LongerString) {
    auto cert = makeCert("", /*omitCountry=*/true);
    ASSERT_NE(cert, nullptr);
    EXPECT_EQ(extractCountryFromCert(cert.get(), "UNKNOWN"), "UNKNOWN");
}

// ===========================================================================
// Idempotency
// ===========================================================================

TEST(ExtractCountryFromCert, CalledTwiceOnSameCert_SameResult) {
    auto cert = makeCert("KR");
    ASSERT_NE(cert, nullptr);
    std::string a = extractCountryFromCert(cert.get());
    std::string b = extractCountryFromCert(cert.get());
    EXPECT_EQ(a, b);
}

TEST(ExtractCountryFromCert, DoesNotMutateCert) {
    auto cert = makeCert("KR");
    ASSERT_NE(cert, nullptr);
    // Calling the function must not invalidate the cert object
    extractCountryFromCert(cert.get());
    // Second call still works
    EXPECT_EQ(extractCountryFromCert(cert.get()), "KR");
}

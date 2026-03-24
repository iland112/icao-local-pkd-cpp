/**
 * @file test_certificate_utils.cpp
 * @brief Unit tests for certificate utility functions in certificate_utils.cpp
 *
 * Tests pure/stateless utility functions that do not require a database or LDAP
 * connection. Functions depending on g_services (saveCertificateWithDuplicateCheck,
 * trackCertificateDuplicate, etc.) are integration-level and excluded here.
 */

#include <gtest/gtest.h>
#include "../src/common/certificate_utils.h"

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/asn1.h>

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

using namespace certificate_utils;

// =============================================================================
// Null stub for g_services
// certificate_utils.cpp declares `extern infrastructure::ServiceContainer* g_services`
// and uses it only inside the DB-dependent functions (saveCertificateWithDuplicateCheck,
// trackCertificateDuplicate, etc.) which are NOT called in these unit tests.
// Providing a nullptr definition here satisfies the linker without pulling in the
// full service infrastructure.
// =============================================================================
namespace infrastructure { class ServiceContainer; }
infrastructure::ServiceContainer* g_services = nullptr;

// =============================================================================
// Test Certificate Factory
// =============================================================================

namespace {

/**
 * @brief Create a minimal self-signed X.509 certificate.
 *  Caller owns the returned pointer and must call X509_free().
 */
X509* createMinimalCert(
    const std::string& subjectCountry = "KR",
    const std::string& issuerCountry = "KR",
    bool isCA = false,
    int rsaBits = 2048,
    time_t notBefore = 0,
    time_t notAfter = 0
) {
    X509* cert = X509_new();
    if (!cert) return nullptr;

    X509_set_version(cert, 2);

    // Serial
    BIGNUM* bn = BN_new();
    BN_rand(bn, 64, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY);
    ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(bn, nullptr);
    X509_set_serialNumber(cert, serial);
    ASN1_INTEGER_free(serial);
    BN_free(bn);

    // Key
    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, rsaBits, e, nullptr);
    BN_free(e);
    EVP_PKEY_assign_RSA(pkey, rsa);
    X509_set_pubkey(cert, pkey);

    // Subject
    X509_NAME* subjectName = X509_NAME_new();
    X509_NAME_add_entry_by_txt(subjectName, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(subjectCountry.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subjectName, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Test Cert"), -1, -1, 0);
    X509_set_subject_name(cert, subjectName);
    X509_NAME_free(subjectName);

    // Issuer
    X509_NAME* issuerName = X509_NAME_new();
    X509_NAME_add_entry_by_txt(issuerName, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(issuerCountry.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(issuerName, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Test CA"), -1, -1, 0);
    X509_set_issuer_name(cert, issuerName);
    X509_NAME_free(issuerName);

    // Validity
    time_t nb = (notBefore != 0) ? notBefore : (time(nullptr) - 3600);
    time_t na = (notAfter != 0)  ? notAfter  : (time(nullptr) + 365 * 86400);
    ASN1_TIME_set(X509_getm_notBefore(cert), nb);
    ASN1_TIME_set(X509_getm_notAfter(cert), na);

    // Basic Constraints
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    const char* bcStr = isCA ? "critical,CA:TRUE,pathlen:0" : "critical,CA:FALSE";
    X509_EXTENSION* bc_ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, bcStr);
    if (bc_ext) {
        X509_add_ext(cert, bc_ext, -1);
        X509_EXTENSION_free(bc_ext);
    }

    // SKI
    X509_EXTENSION* ski_ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_key_identifier, "hash");
    if (ski_ext) {
        X509_add_ext(cert, ski_ext, -1);
        X509_EXTENSION_free(ski_ext);
    }

    X509_sign(cert, pkey, EVP_sha256());
    EVP_PKEY_free(pkey);

    return cert;
}

/** @brief Create an already-expired certificate (notAfter in the past). */
X509* createExpiredCert() {
    return createMinimalCert("KR", "KR", false, 2048,
        time(nullptr) - 2 * 86400,  // notBefore: 2 days ago
        time(nullptr) - 86400);     // notAfter:  1 day ago
}

/** @brief Create a not-yet-valid certificate (notBefore in the future). */
X509* createNotYetValidCert() {
    return createMinimalCert("KR", "KR", false, 2048,
        time(nullptr) + 7 * 86400,   // notBefore: 7 days from now
        time(nullptr) + 14 * 86400); // notAfter:  14 days from now
}

/** @brief Convert X509* to PEM byte vector. */
std::vector<uint8_t> certToPem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::vector<uint8_t> pem(data, data + len);
    BIO_free(bio);
    return pem;
}

/** @brief Convert X509* to DER byte vector. */
std::vector<uint8_t> certToDer(X509* cert) {
    int len = i2d_X509(cert, nullptr);
    if (len <= 0) return {};
    std::vector<uint8_t> der(len);
    unsigned char* p = der.data();
    i2d_X509(cert, &p);
    return der;
}

} // anonymous namespace

// =============================================================================
// Test Fixture
// =============================================================================

class CertificateUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        cert_ = createMinimalCert();
        ASSERT_NE(cert_, nullptr);
    }

    void TearDown() override {
        if (cert_) X509_free(cert_);
    }

    X509* cert_ = nullptr;
};

// =============================================================================
// x509NameToString
// =============================================================================

TEST_F(CertificateUtilsTest, X509NameToString_ValidName_ReturnsRFC2253) {
    X509_NAME* name = X509_get_subject_name(cert_);
    ASSERT_NE(name, nullptr);

    std::string dn = x509NameToString(name);
    EXPECT_FALSE(dn.empty());
    // RFC 2253: attributes separated by commas
    EXPECT_NE(dn.find("CN="), std::string::npos);
    EXPECT_NE(dn.find("C="), std::string::npos);
}

TEST_F(CertificateUtilsTest, X509NameToString_Null_ReturnsEmpty) {
    std::string dn = x509NameToString(nullptr);
    EXPECT_TRUE(dn.empty());
}

TEST_F(CertificateUtilsTest, X509NameToString_ContainsCountryCode) {
    X509_NAME* name = X509_get_subject_name(cert_);
    std::string dn = x509NameToString(name);
    // The test cert has C=KR
    EXPECT_NE(dn.find("KR"), std::string::npos);
}

// =============================================================================
// asn1IntegerToHex
// =============================================================================

TEST_F(CertificateUtilsTest, Asn1IntegerToHex_ValidSerial_ReturnsHex) {
    const ASN1_INTEGER* serial = X509_get0_serialNumber(cert_);
    ASSERT_NE(serial, nullptr);

    std::string hex = asn1IntegerToHex(serial);
    EXPECT_FALSE(hex.empty());
    // All characters should be valid hex digits
    for (char c : hex) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                    (c >= 'a' && c <= 'f'))
            << "Non-hex character: " << c;
    }
}

TEST_F(CertificateUtilsTest, Asn1IntegerToHex_Null_ReturnsEmpty) {
    std::string hex = asn1IntegerToHex(nullptr);
    EXPECT_TRUE(hex.empty());
}

TEST_F(CertificateUtilsTest, Asn1IntegerToHex_KnownValue_CorrectHex) {
    // Create an ASN1_INTEGER with value 255 (0xFF)
    BIGNUM* bn = BN_new();
    BN_set_word(bn, 255);
    ASN1_INTEGER* asn1 = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);
    ASSERT_NE(asn1, nullptr);

    std::string hex = asn1IntegerToHex(asn1);
    // BN_bn2hex(255) produces "FF"
    EXPECT_EQ(hex, "FF");

    ASN1_INTEGER_free(asn1);
}

TEST_F(CertificateUtilsTest, Asn1IntegerToHex_ZeroValue) {
    BIGNUM* bn = BN_new();
    BN_zero(bn);
    ASN1_INTEGER* asn1 = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);
    ASSERT_NE(asn1, nullptr);

    std::string hex = asn1IntegerToHex(asn1);
    EXPECT_EQ(hex, "0");

    ASN1_INTEGER_free(asn1);
}

// =============================================================================
// asn1TimeToIso8601
// =============================================================================

TEST_F(CertificateUtilsTest, Asn1TimeToIso8601_ValidTime_ReturnsIso8601) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(cert_);
    ASSERT_NE(notBefore, nullptr);

    std::string iso = asn1TimeToIso8601(notBefore);
    EXPECT_FALSE(iso.empty());
    // ISO 8601 format: YYYY-MM-DDTHH:MM:SS (19 chars minimum)
    EXPECT_GE(iso.length(), 19u);
    EXPECT_EQ(iso[4], '-');
    EXPECT_EQ(iso[7], '-');
    EXPECT_EQ(iso[10], 'T');
    EXPECT_EQ(iso[13], ':');
    EXPECT_EQ(iso[16], ':');
}

TEST_F(CertificateUtilsTest, Asn1TimeToIso8601_Null_ReturnsEmpty) {
    std::string iso = asn1TimeToIso8601(nullptr);
    EXPECT_TRUE(iso.empty());
}

TEST_F(CertificateUtilsTest, Asn1TimeToIso8601_UtcTime_CorrectFormat) {
    // ASN1_TIME_set uses UTCTIME for dates before 2050
    time_t t = 1000000000; // 2001-09-09
    ASN1_TIME* asn1t = ASN1_TIME_new();
    ASN1_TIME_set(asn1t, t);

    std::string iso = asn1TimeToIso8601(asn1t);
    EXPECT_FALSE(iso.empty());
    // Year should start with 200x
    EXPECT_EQ(iso.substr(0, 3), "200");
    ASN1_TIME_free(asn1t);
}

TEST_F(CertificateUtilsTest, Asn1TimeToIso8601_GeneralizedTime_CorrectFormat) {
    // GENERALIZEDTIME is used for dates >= 2050
    ASN1_GENERALIZEDTIME* gt = ASN1_GENERALIZEDTIME_new();
    ASN1_GENERALIZEDTIME_set_string(gt, "20500101120000Z");

    std::string iso = asn1TimeToIso8601(reinterpret_cast<const ASN1_TIME*>(gt));
    EXPECT_FALSE(iso.empty());
    EXPECT_EQ(iso.substr(0, 4), "2050");
    ASN1_GENERALIZEDTIME_free(gt);
}

// =============================================================================
// extractCountryCode
// =============================================================================

TEST(ExtractCountryCodeTest, SlashFormat_KR_Extracted) {
    EXPECT_EQ(extractCountryCode("/C=KR/O=Test/CN=Test"), "KR");
}

TEST(ExtractCountryCodeTest, RFC2253Format_KR_Extracted) {
    EXPECT_EQ(extractCountryCode("CN=Test,O=Test,C=KR"), "KR");
}

TEST(ExtractCountryCodeTest, TwoLetterCode_Extracted) {
    EXPECT_EQ(extractCountryCode("C=DE,CN=Test"), "DE");
}

TEST(ExtractCountryCodeTest, ThreeLetterCode_Extracted) {
    EXPECT_EQ(extractCountryCode("C=USA,CN=Test"), "USA");
}

TEST(ExtractCountryCodeTest, UpperCase_AlwaysUpperCase) {
    std::string result = extractCountryCode("C=kr,CN=Test");
    for (char c : result) {
        EXPECT_TRUE(c >= 'A' && c <= 'Z') << "Expected uppercase, got: " << c;
    }
}

TEST(ExtractCountryCodeTest, MissingCountryCode_ReturnsXX) {
    // No C= field → default "XX"
    EXPECT_EQ(extractCountryCode("CN=Test,O=Org"), "XX");
}

TEST(ExtractCountryCodeTest, EmptyString_ReturnsXX) {
    EXPECT_EQ(extractCountryCode(""), "XX");
}

TEST(ExtractCountryCodeTest, CountryAtEnd_Extracted) {
    EXPECT_EQ(extractCountryCode("CN=Test,C=JP"), "JP");
}

TEST(ExtractCountryCodeTest, CountryAtStart_SlashFormat) {
    EXPECT_EQ(extractCountryCode("/C=FR/CN=Test"), "FR");
}

// =============================================================================
// computeSha256Fingerprint
// =============================================================================

TEST_F(CertificateUtilsTest, ComputeSha256Fingerprint_ValidCert_Returns64HexChars) {
    std::string fp = computeSha256Fingerprint(cert_);
    EXPECT_EQ(fp.length(), 64u) << "SHA-256 fingerprint must be 64 hex chars";
    for (char c : fp) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Non-hex character: " << c;
    }
}

TEST_F(CertificateUtilsTest, ComputeSha256Fingerprint_Null_ReturnsEmpty) {
    std::string fp = computeSha256Fingerprint(nullptr);
    EXPECT_TRUE(fp.empty());
}

TEST_F(CertificateUtilsTest, ComputeSha256Fingerprint_Deterministic) {
    std::string fp1 = computeSha256Fingerprint(cert_);
    std::string fp2 = computeSha256Fingerprint(cert_);
    EXPECT_EQ(fp1, fp2) << "Same cert must produce same fingerprint";
}

TEST_F(CertificateUtilsTest, ComputeSha256Fingerprint_DifferentCerts_DifferentValues) {
    X509* cert2 = createMinimalCert("US");
    ASSERT_NE(cert2, nullptr);

    std::string fp1 = computeSha256Fingerprint(cert_);
    std::string fp2 = computeSha256Fingerprint(cert2);
    // Different certificates (different keys) must have different fingerprints
    EXPECT_NE(fp1, fp2);

    X509_free(cert2);
}

// =============================================================================
// computeSha1Fingerprint
// =============================================================================

TEST_F(CertificateUtilsTest, ComputeSha1Fingerprint_ValidCert_Returns40HexChars) {
    std::string fp = computeSha1Fingerprint(cert_);
    EXPECT_EQ(fp.length(), 40u) << "SHA-1 fingerprint must be 40 hex chars";
    for (char c : fp) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Non-hex character: " << c;
    }
}

TEST_F(CertificateUtilsTest, ComputeSha1Fingerprint_Null_ReturnsEmpty) {
    std::string fp = computeSha1Fingerprint(nullptr);
    EXPECT_TRUE(fp.empty());
}

TEST_F(CertificateUtilsTest, ComputeSha1Fingerprint_Deterministic) {
    std::string fp1 = computeSha1Fingerprint(cert_);
    std::string fp2 = computeSha1Fingerprint(cert_);
    EXPECT_EQ(fp1, fp2);
}

TEST_F(CertificateUtilsTest, Sha256Sha1_DifferentLength) {
    std::string sha256 = computeSha256Fingerprint(cert_);
    std::string sha1   = computeSha1Fingerprint(cert_);
    EXPECT_NE(sha256.length(), sha1.length());
    EXPECT_EQ(sha256.length(), 64u);
    EXPECT_EQ(sha1.length(), 40u);
}

// =============================================================================
// isExpired
// =============================================================================

TEST_F(CertificateUtilsTest, IsExpired_ValidCert_ReturnsFalse) {
    EXPECT_FALSE(isExpired(cert_));
}

TEST_F(CertificateUtilsTest, IsExpired_Null_ReturnsTrue) {
    // NULL cert is treated as expired
    EXPECT_TRUE(isExpired(nullptr));
}

TEST_F(CertificateUtilsTest, IsExpired_ExpiredCert_ReturnsTrue) {
    X509* expired = createExpiredCert();
    ASSERT_NE(expired, nullptr);

    EXPECT_TRUE(isExpired(expired));

    X509_free(expired);
}

TEST_F(CertificateUtilsTest, IsExpired_NotYetValidCert_ReturnsFalse) {
    // notBefore in the future but notAfter also in the future
    X509* future = createNotYetValidCert();
    ASSERT_NE(future, nullptr);

    // isExpired only checks notAfter, so not-yet-valid is NOT expired
    EXPECT_FALSE(isExpired(future));

    X509_free(future);
}

// =============================================================================
// isLinkCertificate
// =============================================================================

TEST_F(CertificateUtilsTest, IsLinkCertificate_SelfSignedCA_ReturnsFalse) {
    // A self-signed CA is a root CA, not a link certificate.
    // createMinimalCert uses different CN values for subject ("Test Cert") and
    // issuer ("Test CA"), so it never creates a truly self-signed cert.
    // Build a self-signed CA cert manually with identical subject and issuer.
    X509* caCert = X509_new();
    ASSERT_NE(caCert, nullptr);

    X509_set_version(caCert, 2);

    BIGNUM* bn = BN_new();
    BN_rand(bn, 64, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY);
    ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(bn, nullptr);
    X509_set_serialNumber(caCert, serial);
    ASN1_INTEGER_free(serial);
    BN_free(bn);

    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, e, nullptr);
    BN_free(e);
    EVP_PKEY_assign_RSA(pkey, rsa);
    X509_set_pubkey(caCert, pkey);

    // Identical subject and issuer → truly self-signed
    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Root CA"), -1, -1, 0);
    X509_set_subject_name(caCert, name);
    X509_set_issuer_name(caCert, name);  // same name → self-signed
    X509_NAME_free(name);

    ASN1_TIME_set(X509_getm_notBefore(caCert), time(nullptr) - 3600);
    ASN1_TIME_set(X509_getm_notAfter(caCert),  time(nullptr) + 365 * 86400);

    // BasicConstraints: CA:TRUE
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, caCert, caCert, nullptr, nullptr, 0);
    X509_EXTENSION* bc = X509V3_EXT_conf_nid(nullptr, &ctx,
        NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    if (bc) { X509_add_ext(caCert, bc, -1); X509_EXTENSION_free(bc); }

    X509_sign(caCert, pkey, EVP_sha256());
    EVP_PKEY_free(pkey);

    // Self-signed (subject == issuer) CA → NOT a link cert
    EXPECT_FALSE(isLinkCertificate(caCert));

    X509_free(caCert);
}

TEST_F(CertificateUtilsTest, IsLinkCertificate_NonCA_ReturnsFalse) {
    // An end-entity cert (CA=false) is never a link certificate
    EXPECT_FALSE(isLinkCertificate(cert_));
}

TEST_F(CertificateUtilsTest, IsLinkCertificate_Null_ReturnsFalse) {
    EXPECT_FALSE(isLinkCertificate(nullptr));
}

TEST_F(CertificateUtilsTest, IsLinkCertificate_CaDifferentSubjectIssuer_ReturnsTrue) {
    // CA=true AND subject != issuer → link certificate
    X509* cert = X509_new();
    ASSERT_NE(cert, nullptr);

    X509_set_version(cert, 2);

    // Different subject and issuer
    X509_NAME* subject = X509_NAME_new();
    X509_NAME_add_entry_by_txt(subject, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Link Cert Subject"), -1, -1, 0);
    X509_set_subject_name(cert, subject);
    X509_NAME_free(subject);

    X509_NAME* issuer = X509_NAME_new();
    X509_NAME_add_entry_by_txt(issuer, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(issuer, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Root CA (Different Issuer)"), -1, -1, 0);
    X509_set_issuer_name(cert, issuer);
    X509_NAME_free(issuer);

    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) - 3600);
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 365 * 86400);

    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, e, nullptr);
    BN_free(e);
    EVP_PKEY_assign_RSA(pkey, rsa);
    X509_set_pubkey(cert, pkey);

    // Add CA=TRUE basic constraints
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    X509_EXTENSION* bc = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    if (bc) { X509_add_ext(cert, bc, -1); X509_EXTENSION_free(bc); }

    X509_sign(cert, pkey, EVP_sha256());
    EVP_PKEY_free(pkey);

    EXPECT_TRUE(isLinkCertificate(cert));

    X509_free(cert);
}

// =============================================================================
// extractAsn1Text
// =============================================================================

TEST_F(CertificateUtilsTest, ExtractAsn1Text_ValidCert_ContainsCertificateInfo) {
    std::string text = extractAsn1Text(cert_);
    EXPECT_FALSE(text.empty());
    // OpenSSL text output always contains "Certificate:" or "Subject:"
    EXPECT_TRUE(text.find("Certificate") != std::string::npos ||
                text.find("Subject") != std::string::npos);
}

TEST_F(CertificateUtilsTest, ExtractAsn1Text_Null_ReturnsEmpty) {
    std::string text = extractAsn1Text(nullptr);
    EXPECT_TRUE(text.empty());
}

// =============================================================================
// extractAsn1TextFromPem
// =============================================================================

TEST_F(CertificateUtilsTest, ExtractAsn1TextFromPem_ValidPem_Success) {
    std::vector<uint8_t> pem = certToPem(cert_);
    ASSERT_FALSE(pem.empty());

    std::string text = extractAsn1TextFromPem(pem);
    EXPECT_EQ(text.find("Error:"), std::string::npos)
        << "Expected no error, got: " << text.substr(0, 200);
    EXPECT_FALSE(text.empty());
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextFromPem_EmptyData_ReturnsError) {
    std::vector<uint8_t> empty;
    std::string text = extractAsn1TextFromPem(empty);
    EXPECT_EQ(text.substr(0, 6), "Error:") << "Expected Error prefix, got: " << text;
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextFromPem_InvalidData_ReturnsError) {
    std::vector<uint8_t> garbage = {0x00, 0x01, 0x02, 0x03, 0xFF};
    std::string text = extractAsn1TextFromPem(garbage);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextFromPem_PartialPem_ReturnsError) {
    const std::string partial = "-----BEGIN CERTIFICATE-----\nABC123\n";
    std::vector<uint8_t> data(partial.begin(), partial.end());
    std::string text = extractAsn1TextFromPem(data);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

// =============================================================================
// extractAsn1TextFromDer
// =============================================================================

TEST_F(CertificateUtilsTest, ExtractAsn1TextFromDer_ValidDer_Success) {
    std::vector<uint8_t> der = certToDer(cert_);
    ASSERT_FALSE(der.empty());

    std::string text = extractAsn1TextFromDer(der);
    EXPECT_EQ(text.find("Error:"), std::string::npos)
        << "Expected no error, got: " << text.substr(0, 200);
    EXPECT_FALSE(text.empty());
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextFromDer_EmptyData_ReturnsError) {
    std::vector<uint8_t> empty;
    std::string text = extractAsn1TextFromDer(empty);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextFromDer_InvalidData_ReturnsError) {
    std::vector<uint8_t> garbage = {0x01, 0x02, 0x03, 0x04};
    std::string text = extractAsn1TextFromDer(garbage);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

// =============================================================================
// extractAsn1TextAuto
// =============================================================================

TEST_F(CertificateUtilsTest, ExtractAsn1TextAuto_PemInput_DetectsPem) {
    std::vector<uint8_t> pem = certToPem(cert_);
    std::string text = extractAsn1TextAuto(pem);
    EXPECT_EQ(text.find("Error:"), std::string::npos);
    EXPECT_NE(text.find("PEM"), std::string::npos) << "Expected 'PEM' in output";
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextAuto_DerInput_DetectsDer) {
    std::vector<uint8_t> der = certToDer(cert_);
    std::string text = extractAsn1TextAuto(der);
    EXPECT_EQ(text.find("Error:"), std::string::npos);
    // DER/CER/BIN format
    EXPECT_NE(text.find("DER"), std::string::npos) << "Expected 'DER' in output";
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextAuto_EmptyData_ReturnsError) {
    std::vector<uint8_t> empty;
    std::string text = extractAsn1TextAuto(empty);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

TEST_F(CertificateUtilsTest, ExtractAsn1TextAuto_GarbageData_ReturnsError) {
    std::vector<uint8_t> garbage(100, 0xAB);
    std::string text = extractAsn1TextAuto(garbage);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

// =============================================================================
// extractCmsAsn1Text
// =============================================================================

TEST_F(CertificateUtilsTest, ExtractCmsAsn1Text_EmptyData_ReturnsError) {
    std::vector<uint8_t> empty;
    std::string text = extractCmsAsn1Text(empty);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

TEST_F(CertificateUtilsTest, ExtractCmsAsn1Text_InvalidData_ReturnsError) {
    std::vector<uint8_t> garbage = {0x01, 0x02, 0x03};
    std::string text = extractCmsAsn1Text(garbage);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

TEST_F(CertificateUtilsTest, ExtractCmsAsn1Text_DerCertNotCms_ReturnsError) {
    // A plain DER cert is not a CMS SignedData
    std::vector<uint8_t> der = certToDer(cert_);
    std::string text = extractCmsAsn1Text(der);
    EXPECT_EQ(text.substr(0, 6), "Error:");
}

// =============================================================================
// getSourceType
// =============================================================================

TEST(GetSourceTypeTest, LDIF001_ReturnsCorrectType) {
    EXPECT_EQ(getSourceType("LDIF_001"), "LDIF_001");
}

TEST(GetSourceTypeTest, LDIF002_ReturnsCorrectType) {
    EXPECT_EQ(getSourceType("LDIF_002"), "LDIF_002");
}

TEST(GetSourceTypeTest, LDIF003_ReturnsCorrectType) {
    EXPECT_EQ(getSourceType("LDIF_003"), "LDIF_003");
}

TEST(GetSourceTypeTest, MASTERLIST_ReturnsMLFile) {
    EXPECT_EQ(getSourceType("MASTERLIST"), "ML_FILE");
}

TEST(GetSourceTypeTest, Unknown_ReturnsUnknown) {
    EXPECT_EQ(getSourceType("UNKNOWN_FORMAT"), "UNKNOWN");
    EXPECT_EQ(getSourceType(""), "UNKNOWN");
    EXPECT_EQ(getSourceType("ldif_001"), "UNKNOWN");  // case-sensitive
}

TEST(GetSourceTypeTest, AllDefinedTypes_Covered) {
    EXPECT_EQ(getSourceType("LDIF_001"), "LDIF_001");
    EXPECT_EQ(getSourceType("LDIF_002"), "LDIF_002");
    EXPECT_EQ(getSourceType("LDIF_003"), "LDIF_003");
    EXPECT_EQ(getSourceType("MASTERLIST"), "ML_FILE");
}

// =============================================================================
// Idempotency: fingerprints must be stable across repeated calls
// =============================================================================

TEST_F(CertificateUtilsTest, Idempotency_Sha256FingerprintStable) {
    std::string fp1 = computeSha256Fingerprint(cert_);
    std::string fp2 = computeSha256Fingerprint(cert_);
    std::string fp3 = computeSha256Fingerprint(cert_);
    EXPECT_EQ(fp1, fp2);
    EXPECT_EQ(fp2, fp3);
}

TEST_F(CertificateUtilsTest, Idempotency_IsExpiredStable) {
    bool e1 = isExpired(cert_);
    bool e2 = isExpired(cert_);
    EXPECT_EQ(e1, e2);
}

TEST_F(CertificateUtilsTest, Idempotency_ExtractAsn1TextStable) {
    std::string text1 = extractAsn1Text(cert_);
    std::string text2 = extractAsn1Text(cert_);
    EXPECT_EQ(text1, text2);
}

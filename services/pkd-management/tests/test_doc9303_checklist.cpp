/**
 * @file test_doc9303_checklist.cpp
 * @brief Unit tests for ICAO Doc 9303 compliance checklist
 *
 * Tests all ~28 compliance checks across certificate types (CSCA, DSC, MLSC, DSC_NC).
 * Uses OpenSSL API to generate minimal self-signed test certificates with
 * specific properties (key usage, basic constraints, etc.) to exercise each check path.
 */

#include <gtest/gtest.h>
#include "../src/common/doc9303_checklist.h"

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/ec.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace common;

// =============================================================================
// Certificate Builder Helper
// =============================================================================

/**
 * @brief Builder for creating test X.509 certificates with configurable properties.
 *
 * Allows unit tests to control version, serial number, key usage, basic
 * constraints, and extensions without relying on external certificate files.
 */
class CertificateBuilder {
public:
    struct Options {
        int version = 2;                      // 0=v1, 1=v2, 2=v3
        std::string countryCode = "KR";
        std::string issuerCountryCode = "KR";
        bool selfSigned = true;
        bool isCA = false;
        int pathLen = -1;                     // -1 = not set
        bool basicConstraintsCritical = true;
        bool includeKeyUsage = true;
        bool keyUsageCritical = true;
        unsigned int keyUsageBits = KU_DIGITAL_SIGNATURE;
        bool includeEku = false;
        bool ekuCritical = false;
        std::string ekuOid;                   // OID string for EKU
        bool includeSki = true;
        bool includeAki = false;
        bool netscapeExtension = false;
        int rsaKeyBits = 2048;
        bool useEcKey = false;                // true = EC P-256
        int serialLen = 4;                    // bytes
        bool serialNegative = false;
        bool includeCertPolicies = false;
        bool certPoliciesCritical = false;
    };

    /**
     * @brief Build a certificate with the given options.
     * @return Caller must X509_free() the returned pointer.
     */
    static X509* build(const Options& opts) {
        X509* cert = X509_new();
        if (!cert) return nullptr;

        // --- Version ---
        X509_set_version(cert, opts.version);

        // --- Serial Number ---
        {
            ASN1_INTEGER* serial = ASN1_INTEGER_new();
            if (opts.serialNegative) {
                // Create a negative serial: set high bit of first byte
                unsigned char neg_bytes[] = {0x80, 0x01};
                ASN1_STRING_set(serial, neg_bytes, 2);
            } else {
                BIGNUM* bn = BN_new();
                // Use (serialLen * 8 - 1) bits so the MSB of the first byte is
                // always 0, guaranteeing a positive serial number in DER encoding.
                // BN_RAND_TOP_ONE ensures the result actually uses serialLen bytes.
                BN_rand(bn, opts.serialLen * 8 - 1, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY);
                BN_to_ASN1_INTEGER(bn, serial);
                BN_free(bn);
            }
            X509_set_serialNumber(cert, serial);
            ASN1_INTEGER_free(serial);
        }

        // --- Generate Key ---
        EVP_PKEY* pkey = EVP_PKEY_new();
        if (opts.useEcKey) {
            EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
            EC_KEY_set_asn1_flag(ec, OPENSSL_EC_NAMED_CURVE);
            EC_KEY_generate_key(ec);
            EVP_PKEY_assign_EC_KEY(pkey, ec);
        } else {
            RSA* rsa = RSA_new();
            BIGNUM* e = BN_new();
            BN_set_word(e, RSA_F4);
            RSA_generate_key_ex(rsa, opts.rsaKeyBits, e, nullptr);
            BN_free(e);
            EVP_PKEY_assign_RSA(pkey, rsa);
        }
        X509_set_pubkey(cert, pkey);

        // --- Subject DN ---
        // Build subject first so we can reuse it as issuer when selfSigned=true.
        {
            X509_NAME* subjectName = X509_NAME_new();
            X509_NAME_add_entry_by_txt(subjectName, "C", MBSTRING_ASC,
                reinterpret_cast<const unsigned char*>(opts.countryCode.c_str()),
                -1, -1, 0);
            X509_NAME_add_entry_by_txt(subjectName, "O", MBSTRING_ASC,
                reinterpret_cast<const unsigned char*>("Test Org"), -1, -1, 0);
            X509_NAME_add_entry_by_txt(subjectName, "CN", MBSTRING_ASC,
                reinterpret_cast<const unsigned char*>("Test Certificate"), -1, -1, 0);
            X509_set_subject_name(cert, subjectName);
            X509_NAME_free(subjectName);
        }

        // --- Issuer DN ---
        // When selfSigned=true set issuer == subject so that x509::isSelfSigned()
        // (which uses X509_NAME_cmp) correctly identifies the certificate.
        {
            X509_NAME* issuerName = X509_NAME_new();
            if (opts.selfSigned) {
                // Mirror the subject name exactly (same O and CN, issuerCountryCode is used for C)
                X509_NAME_add_entry_by_txt(issuerName, "C", MBSTRING_ASC,
                    reinterpret_cast<const unsigned char*>(opts.countryCode.c_str()),
                    -1, -1, 0);
                X509_NAME_add_entry_by_txt(issuerName, "O", MBSTRING_ASC,
                    reinterpret_cast<const unsigned char*>("Test Org"), -1, -1, 0);
                X509_NAME_add_entry_by_txt(issuerName, "CN", MBSTRING_ASC,
                    reinterpret_cast<const unsigned char*>("Test Certificate"), -1, -1, 0);
            } else {
                X509_NAME_add_entry_by_txt(issuerName, "C", MBSTRING_ASC,
                    reinterpret_cast<const unsigned char*>(opts.issuerCountryCode.c_str()),
                    -1, -1, 0);
                X509_NAME_add_entry_by_txt(issuerName, "O", MBSTRING_ASC,
                    reinterpret_cast<const unsigned char*>("Test CA"), -1, -1, 0);
                X509_NAME_add_entry_by_txt(issuerName, "CN", MBSTRING_ASC,
                    reinterpret_cast<const unsigned char*>("Test Issuer CA"), -1, -1, 0);
            }
            X509_set_issuer_name(cert, issuerName);
            X509_NAME_free(issuerName);
        }

        // --- Validity ---
        ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) - 3600);
        ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 365 * 24 * 3600);

        // --- Extensions ---
        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

        // Key Usage
        if (opts.includeKeyUsage) {
            std::string kuVal;
            if (opts.keyUsageBits & KU_DIGITAL_SIGNATURE) kuVal += "digitalSignature,";
            if (opts.keyUsageBits & KU_KEY_CERT_SIGN)     kuVal += "keyCertSign,";
            if (opts.keyUsageBits & KU_CRL_SIGN)           kuVal += "cRLSign,";
            if (opts.keyUsageBits & KU_NON_REPUDIATION)    kuVal += "nonRepudiation,";
            if (!kuVal.empty() && kuVal.back() == ',') kuVal.pop_back();
            if (kuVal.empty()) kuVal = "digitalSignature";

            std::string kuStr = (opts.keyUsageCritical ? "critical," : "") + kuVal;
            X509_EXTENSION* ku_ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, kuStr.c_str());
            if (ku_ext) {
                X509_add_ext(cert, ku_ext, -1);
                X509_EXTENSION_free(ku_ext);
            }
        }

        // Basic Constraints
        // Only add for V3 certs.  OpenSSL 3.6+ auto-upgrades version to 2 (V3)
        // during X509_sign() when any extension is present, so adding extensions
        // to a non-V3 cert defeats the purpose of the version tests.
        if (opts.version == 2) {
            std::string bcVal;
            if (opts.basicConstraintsCritical) bcVal += "critical,";
            if (opts.isCA) {
                bcVal += "CA:TRUE";
                if (opts.pathLen >= 0) {
                    bcVal += ",pathlen:" + std::to_string(opts.pathLen);
                }
            } else {
                bcVal += "CA:FALSE";
            }
            X509_EXTENSION* bc_ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, bcVal.c_str());
            if (bc_ext) {
                X509_add_ext(cert, bc_ext, -1);
                X509_EXTENSION_free(bc_ext);
            }
        }

        // SKI
        if (opts.includeSki) {
            X509_EXTENSION* ski_ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_key_identifier, "hash");
            if (ski_ext) {
                X509_add_ext(cert, ski_ext, -1);
                X509_EXTENSION_free(ski_ext);
            }
        }

        // AKI
        if (opts.includeAki) {
            X509_EXTENSION* aki_ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_authority_key_identifier, "keyid:always");
            if (aki_ext) {
                X509_add_ext(cert, aki_ext, -1);
                X509_EXTENSION_free(aki_ext);
            }
        }

        // EKU
        if (opts.includeEku && !opts.ekuOid.empty()) {
            std::string ekuVal = (opts.ekuCritical ? "critical," : "") + opts.ekuOid;
            X509_EXTENSION* eku_ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, ekuVal.c_str());
            if (eku_ext) {
                X509_add_ext(cert, eku_ext, -1);
                X509_EXTENSION_free(eku_ext);
            }
        }

        // Certificate Policies
        // X509V3_EXT_conf_nid does not support bare OID strings for
        // certificate policies; build the extension with the raw ASN.1 API.
        if (opts.includeCertPolicies) {
            CERTIFICATEPOLICIES* pols = CERTIFICATEPOLICIES_new();
            if (pols) {
                POLICYINFO* pol = POLICYINFO_new();
                if (pol) {
                    pol->policyid = OBJ_txt2obj("1.3.6.1.5.5.7.2.1", 1);
                    sk_POLICYINFO_push(pols, pol);
                }
                unsigned char* buf = nullptr;
                int cpLen = i2d_CERTIFICATEPOLICIES(pols, &buf);
                if (cpLen > 0 && buf) {
                    ASN1_OCTET_STRING* ext_data = ASN1_OCTET_STRING_new();
                    if (ext_data) {
                        ASN1_OCTET_STRING_set(ext_data, buf, cpLen);
                        X509_EXTENSION* cp_ext = X509_EXTENSION_create_by_NID(
                            nullptr, NID_certificate_policies,
                            opts.certPoliciesCritical ? 1 : 0, ext_data);
                        if (cp_ext) {
                            X509_add_ext(cert, cp_ext, -1);
                            X509_EXTENSION_free(cp_ext);
                        }
                        ASN1_OCTET_STRING_free(ext_data);
                    }
                    OPENSSL_free(buf);
                }
                CERTIFICATEPOLICIES_free(pols);
            }
        }

        // Netscape Cert Type extension (deliberately broken for negative tests)
        if (opts.netscapeExtension) {
            // Add Netscape Cert Type OID manually
            ASN1_OBJECT* netscape_obj = OBJ_txt2obj("2.16.840.1.113730.1.1", 1);
            if (netscape_obj) {
                unsigned char ns_val[] = {0x03, 0x02, 0x07, 0x80}; // BIT STRING
                ASN1_OCTET_STRING* ns_str = ASN1_OCTET_STRING_new();
                ASN1_OCTET_STRING_set(ns_str, ns_val, sizeof(ns_val));
                X509_EXTENSION* ns_ext = X509_EXTENSION_create_by_OBJ(nullptr, netscape_obj, 0, ns_str);
                if (ns_ext) {
                    X509_add_ext(cert, ns_ext, -1);
                    X509_EXTENSION_free(ns_ext);
                }
                ASN1_OCTET_STRING_free(ns_str);
                ASN1_OBJECT_free(netscape_obj);
            }
        }

        // --- Sign ---
        X509_sign(cert, pkey, EVP_sha256());
        EVP_PKEY_free(pkey);

        return cert;
    }
};

// =============================================================================
// Helper functions
// =============================================================================

static bool hasItemWithId(const Doc9303ChecklistResult& result, const std::string& id) {
    for (const auto& item : result.items) {
        if (item.id == id) return true;
    }
    return false;
}

static std::string getItemStatus(const Doc9303ChecklistResult& result, const std::string& id) {
    for (const auto& item : result.items) {
        if (item.id == id) return item.status;
    }
    return "NOT_FOUND";
}

// =============================================================================
// Test Fixture
// =============================================================================

class Doc9303ChecklistTest : public ::testing::Test {
protected:
    void TearDown() override {
        for (X509* cert : certsToFree) {
            X509_free(cert);
        }
        certsToFree.clear();
    }

    X509* buildAndTrack(const CertificateBuilder::Options& opts) {
        X509* cert = CertificateBuilder::build(opts);
        if (cert) certsToFree.push_back(cert);
        return cert;
    }

    // --- Pre-built standard certificates ---

    X509* buildStandardCsca() {
        CertificateBuilder::Options opts;
        opts.isCA = true;
        opts.pathLen = 0;
        opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
        opts.selfSigned = true;
        opts.includeAki = false;
        return buildAndTrack(opts);
    }

    X509* buildStandardDsc() {
        CertificateBuilder::Options opts;
        opts.isCA = false;
        opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
        opts.includeEku = false;
        return buildAndTrack(opts);
    }

    X509* buildStandardMlsc() {
        CertificateBuilder::Options opts;
        opts.isCA = false;
        opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
        opts.includeEku = true;
        opts.ekuCritical = true;
        opts.ekuOid = "2.23.136.1.1.3";
        return buildAndTrack(opts);
    }

private:
    std::vector<X509*> certsToFree;
};

// =============================================================================
// NULL Certificate Input
// =============================================================================

TEST_F(Doc9303ChecklistTest, NullCert_ReturnsNonConformant) {
    Doc9303ChecklistResult result = runDoc9303Checklist(nullptr, "CSCA");
    EXPECT_EQ(result.overallStatus, "NON_CONFORMANT");
    EXPECT_EQ(result.failCount, 1);
    EXPECT_EQ(result.totalChecks, 1);
}

TEST_F(Doc9303ChecklistTest, NullCert_ErrorItemPresent) {
    Doc9303ChecklistResult result = runDoc9303Checklist(nullptr, "DSC");
    ASSERT_FALSE(result.items.empty());
    EXPECT_EQ(result.items[0].status, "FAIL");
}

// =============================================================================
// Version Check (version_v3)
// =============================================================================

TEST_F(Doc9303ChecklistTest, Version_V3_Passes) {
    CertificateBuilder::Options opts;
    opts.version = 2;  // 0-indexed: 2 = V3
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "version_v3"), "PASS");
}

TEST_F(Doc9303ChecklistTest, Version_V1_Fails) {
    CertificateBuilder::Options opts;
    opts.version = 0;  // V1
    // V1 cert cannot have extensions; suppress them
    opts.includeKeyUsage = false;
    opts.includeSki = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "version_v3"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, Version_V2_Fails) {
    CertificateBuilder::Options opts;
    opts.version = 1;  // V2
    opts.includeKeyUsage = false;
    opts.includeSki = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "version_v3"), "FAIL");
}

// =============================================================================
// Serial Number Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, SerialNumber_Positive_Passes) {
    CertificateBuilder::Options opts;
    opts.serialNegative = false;
    opts.serialLen = 4;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "serial_positive"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SerialNumber_MaxOctets_WithSmallSerial_Passes) {
    CertificateBuilder::Options opts;
    opts.serialLen = 4;  // well within 20-byte limit
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "serial_max_20_octets"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SerialNumber_Exactly20Bytes_Passes) {
    CertificateBuilder::Options opts;
    opts.serialLen = 20;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "serial_max_20_octets"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SerialNumber_21Bytes_Fails) {
    // Build a cert with 21-byte serial manually
    X509* cert = X509_new();
    ASSERT_NE(cert, nullptr);

    // 21-byte positive serial (leading 0x00 makes it 22 in DER if high bit set,
    // but ASN1_STRING_length will return 21)
    unsigned char big_serial[21];
    big_serial[0] = 0x01;  // positive (no high bit)
    for (int i = 1; i < 21; i++) big_serial[i] = (unsigned char)i;

    ASN1_INTEGER* asn1_serial = ASN1_INTEGER_new();
    ASN1_STRING_set(asn1_serial, big_serial, 21);
    X509_set_serialNumber(cert, asn1_serial);
    ASN1_INTEGER_free(asn1_serial);

    X509_set_version(cert, 2);
    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr));
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 86400);

    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);
    X509_NAME_free(name);

    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, e, nullptr);
    BN_free(e);
    EVP_PKEY_assign_RSA(pkey, rsa);
    X509_set_pubkey(cert, pkey);
    X509_sign(cert, pkey, EVP_sha256());
    EVP_PKEY_free(pkey);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "serial_max_20_octets"), "FAIL");

    X509_free(cert);
}

// =============================================================================
// Signature Algorithm Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, SigAlgo_OidMatch_Passes) {
    // Standard self-signed cert: TBS and outer both use sha256WithRSAEncryption
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "sig_algo_match"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SigAlgo_SHA256WithRSA_Passes) {
    // SHA-256 + RSA is Doc 9303-compliant
    CertificateBuilder::Options opts;
    opts.rsaKeyBits = 2048;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "sig_algo_approved"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SigAlgo_SHA256WithECDSA_Passes) {
    CertificateBuilder::Options opts;
    opts.useEcKey = true;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    // ECDSA SHA-256 should pass
    std::string status = getItemStatus(result, "sig_algo_approved");
    EXPECT_TRUE(status == "PASS" || status == "WARNING");
}

// =============================================================================
// Issuer / Subject Country Code Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, IssuerCountryPresent_Passes) {
    CertificateBuilder::Options opts;
    opts.issuerCountryCode = "US";
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "issuer_country_present"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SubjectCountryPresent_Passes) {
    CertificateBuilder::Options opts;
    opts.countryCode = "DE";
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "subject_country_present"), "PASS");
}

TEST_F(Doc9303ChecklistTest, IssuerCountryAbsent_Fails) {
    // Build cert without a country code in issuer
    X509* cert = X509_new();
    ASSERT_NE(cert, nullptr);

    X509_set_version(cert, 2);

    X509_NAME* issuer = X509_NAME_new();
    X509_NAME_add_entry_by_txt(issuer, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("No Country CA"), -1, -1, 0);
    X509_set_issuer_name(cert, issuer);
    X509_NAME_free(issuer);

    X509_NAME* subject = X509_NAME_new();
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Test Subject"), -1, -1, 0);
    X509_set_subject_name(cert, subject);
    X509_NAME_free(subject);

    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr));
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 86400);

    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, e, nullptr);
    BN_free(e);
    EVP_PKEY_assign_RSA(pkey, rsa);
    X509_set_pubkey(cert, pkey);
    X509_sign(cert, pkey, EVP_sha256());
    EVP_PKEY_free(pkey);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "issuer_country_present"), "FAIL");

    X509_free(cert);
}

TEST_F(Doc9303ChecklistTest, SubjectIssuerCountryMatch_DSC_SameCountry_Passes) {
    CertificateBuilder::Options opts;
    opts.countryCode = "FR";
    opts.issuerCountryCode = "FR";
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "subject_issuer_country_match"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SubjectIssuerCountryMatch_DSC_DifferentCountry_Fails) {
    CertificateBuilder::Options opts;
    opts.countryCode = "KR";
    opts.issuerCountryCode = "US";
    opts.selfSigned = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "subject_issuer_country_match"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, SubjectIssuerCountryMatch_CSCA_NotChecked) {
    // Country match check should NOT appear for CSCA
    CertificateBuilder::Options opts;
    opts.countryCode = "KR";
    opts.issuerCountryCode = "US";
    opts.isCA = true;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_FALSE(hasItemWithId(result, "subject_issuer_country_match"));
}

TEST_F(Doc9303ChecklistTest, SubjectIssuerCountryMatch_MLSC_Checked) {
    CertificateBuilder::Options opts;
    opts.countryCode = "JP";
    opts.issuerCountryCode = "JP";
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.includeEku = true;
    opts.ekuCritical = true;
    opts.ekuOid = "2.23.136.1.1.3";
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "MLSC");
    EXPECT_TRUE(hasItemWithId(result, "subject_issuer_country_match"));
    EXPECT_EQ(getItemStatus(result, "subject_issuer_country_match"), "PASS");
}

// =============================================================================
// Unique Identifiers Check
// =============================================================================

TEST_F(Doc9303ChecklistTest, UniqueIdentifiers_Absent_Passes) {
    // Normal certs built by builder never add unique identifiers
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "unique_id_absent"), "PASS");
}

// =============================================================================
// Key Usage Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, KeyUsage_Present_Passes) {
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_usage_present"), "PASS");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_Absent_Fails) {
    CertificateBuilder::Options opts;
    opts.includeKeyUsage = false;
    opts.includeSki = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_usage_present"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_Critical_Passes) {
    CertificateBuilder::Options opts;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.keyUsageCritical = true;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_usage_critical"), "PASS");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_NonCritical_Fails) {
    CertificateBuilder::Options opts;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.keyUsageCritical = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_usage_critical"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_CSCA_CorrectBits_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "key_usage_correct"), "PASS");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_CSCA_WrongBits_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;  // Wrong for CSCA
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "key_usage_correct"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_DSC_DigitalSignature_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_usage_correct"), "PASS");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_DSC_WrongBits_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_KEY_CERT_SIGN;  // Wrong for DSC
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_usage_correct"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, KeyUsage_MLSC_DigitalSignature_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.includeEku = true;
    opts.ekuCritical = true;
    opts.ekuOid = "2.23.136.1.1.3";
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "MLSC");
    EXPECT_EQ(getItemStatus(result, "key_usage_correct"), "PASS");
}

// =============================================================================
// Basic Constraints Checks (CSCA-specific)
// =============================================================================

TEST_F(Doc9303ChecklistTest, BasicConstraints_CSCA_Present_Passes) {
    X509* cert = buildStandardCsca();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_present"), "PASS");
}

TEST_F(Doc9303ChecklistTest, BasicConstraints_CSCA_Critical_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.basicConstraintsCritical = true;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_critical"), "PASS");
}

TEST_F(Doc9303ChecklistTest, BasicConstraints_CSCA_NonCritical_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.basicConstraintsCritical = false;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_critical"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, BasicConstraints_CSCA_CA_True_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_ca_true"), "PASS");
}

TEST_F(Doc9303ChecklistTest, BasicConstraints_CSCA_CA_False_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_ca_true"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, BasicConstraints_CSCA_PathLen0_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_pathlen_zero"), "PASS");
}

TEST_F(Doc9303ChecklistTest, BasicConstraints_CSCA_PathLen1_Warning) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 1;  // Not recommended (should be 0)
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_pathlen_zero"), "WARNING");
}

// --- DSC Basic Constraints ---

TEST_F(Doc9303ChecklistTest, BasicConstraints_DSC_CA_False_Passes) {
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_ca_false"), "PASS");
}

TEST_F(Doc9303ChecklistTest, BasicConstraints_DSC_CA_True_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = true;  // Wrong for DSC
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "basic_constraints_ca_false"), "FAIL");
}

// =============================================================================
// Extended Key Usage Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, EKU_CSCA_Absent_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    opts.includeEku = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "eku_absent"), "PASS");
}

TEST_F(Doc9303ChecklistTest, EKU_CSCA_Present_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    opts.includeEku = true;
    opts.ekuCritical = false;
    opts.ekuOid = "serverAuth";  // Any EKU on CSCA is non-compliant
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "eku_absent"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, EKU_DSC_Absent_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.includeEku = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "eku_absent"), "PASS");
}

TEST_F(Doc9303ChecklistTest, EKU_MLSC_Correct_OID_Passes) {
    X509* cert = buildStandardMlsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "MLSC");
    EXPECT_EQ(getItemStatus(result, "eku_mlsc_present"), "PASS");
}

TEST_F(Doc9303ChecklistTest, EKU_MLSC_Absent_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.includeEku = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "MLSC");
    EXPECT_EQ(getItemStatus(result, "eku_mlsc_present"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, EKU_MLSC_WrongOID_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.includeEku = true;
    opts.ekuCritical = true;
    opts.ekuOid = "serverAuth";  // Wrong OID for MLSC
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "MLSC");
    EXPECT_EQ(getItemStatus(result, "eku_mlsc_present"), "FAIL");
}

TEST_F(Doc9303ChecklistTest, EKU_MLSC_NonCritical_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.includeEku = true;
    opts.ekuCritical = false;  // Must be critical for MLSC
    opts.ekuOid = "2.23.136.1.1.3";
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "MLSC");
    EXPECT_EQ(getItemStatus(result, "eku_mlsc_critical"), "FAIL");
}

// =============================================================================
// AKI / SKI Extension Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, AKI_SelfSignedCSCA_Absent_IsWarning) {
    // Self-signed CSCA without AKI: warning, not fail
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    opts.includeAki = false;
    opts.selfSigned = true;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    // Self-signed: AKI is recommended, absence is WARNING not FAIL
    std::string status = getItemStatus(result, "aki_present");
    EXPECT_TRUE(status == "WARNING" || status == "PASS");
}

TEST_F(Doc9303ChecklistTest, AKI_NonCritical_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.includeAki = true;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "aki_non_critical"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SKI_CSCA_Present_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    opts.includeSki = true;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    EXPECT_EQ(getItemStatus(result, "ski_present"), "PASS");
}

TEST_F(Doc9303ChecklistTest, SKI_NonCritical_Passes) {
    CertificateBuilder::Options opts;
    opts.isCA = true;
    opts.pathLen = 0;
    opts.keyUsageBits = KU_KEY_CERT_SIGN | KU_CRL_SIGN;
    opts.includeSki = true;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");
    // SKI should be present and non-critical
    EXPECT_EQ(getItemStatus(result, "ski_non_critical"), "PASS");
}

// =============================================================================
// Netscape Extension Check
// =============================================================================

TEST_F(Doc9303ChecklistTest, NoNetscapeExtension_Passes) {
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "no_netscape_extensions"), "PASS");
}

TEST_F(Doc9303ChecklistTest, WithNetscapeExtension_Fails) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    opts.netscapeExtension = true;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "no_netscape_extensions"), "FAIL");
}

// =============================================================================
// Unknown Critical Extensions Check
// =============================================================================

TEST_F(Doc9303ChecklistTest, NoUnknownCriticalExtensions_Passes) {
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "no_unknown_critical_ext"), "PASS");
}

// =============================================================================
// Key Size Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, KeySize_RSA2048_MinimumPasses) {
    CertificateBuilder::Options opts;
    opts.rsaKeyBits = 2048;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_size_minimum"), "PASS");
}

TEST_F(Doc9303ChecklistTest, KeySize_RSA3072_RecommendedPasses) {
    CertificateBuilder::Options opts;
    opts.rsaKeyBits = 3072;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_size_recommended"), "PASS");
}

TEST_F(Doc9303ChecklistTest, KeySize_RSA2048_RecommendedIsWarning) {
    CertificateBuilder::Options opts;
    opts.rsaKeyBits = 2048;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    // 2048-bit RSA meets minimum but not the 3072-bit recommendation
    EXPECT_EQ(getItemStatus(result, "key_size_recommended"), "WARNING");
}

TEST_F(Doc9303ChecklistTest, KeySize_ECDSA256_MinimumPasses) {
    CertificateBuilder::Options opts;
    opts.useEcKey = true;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "key_size_minimum"), "PASS");
}

// =============================================================================
// Overall Status Checks
// =============================================================================

TEST_F(Doc9303ChecklistTest, OverallStatus_ConformantCSCA) {
    X509* cert = buildStandardCsca();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "CSCA");

    // Certificate type must be recorded
    EXPECT_EQ(result.certificateType, "CSCA");
    // Counts must sum to total
    EXPECT_EQ(result.totalChecks, result.passCount + result.failCount +
              result.warningCount + result.naCount);
    // overallStatus must be one of the expected values
    EXPECT_TRUE(result.overallStatus == "CONFORMANT" ||
                result.overallStatus == "WARNING" ||
                result.overallStatus == "NON_CONFORMANT");
    // A properly built CSCA should not have outright failures
    EXPECT_EQ(result.failCount, 0) << "Unexpected FAIL items in well-formed CSCA";
}

TEST_F(Doc9303ChecklistTest, OverallStatus_ConformantDSC) {
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");

    EXPECT_EQ(result.certificateType, "DSC");
    EXPECT_EQ(result.totalChecks,
              result.passCount + result.failCount + result.warningCount + result.naCount);
    EXPECT_EQ(result.failCount, 0) << "Unexpected FAIL items in well-formed DSC";
}

TEST_F(Doc9303ChecklistTest, OverallStatus_ConformantMLSC) {
    X509* cert = buildStandardMlsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "MLSC");

    EXPECT_EQ(result.certificateType, "MLSC");
    EXPECT_EQ(result.failCount, 0) << "Unexpected FAIL items in well-formed MLSC";
}

TEST_F(Doc9303ChecklistTest, OverallStatus_FailWhenHasFailItems) {
    // A V1 cert with no key usage will produce fail items
    CertificateBuilder::Options opts;
    opts.version = 0;
    opts.includeKeyUsage = false;
    opts.includeSki = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_GT(result.failCount, 0);
    EXPECT_EQ(result.overallStatus, "NON_CONFORMANT");
}

// =============================================================================
// JSON Serialization
// =============================================================================

TEST_F(Doc9303ChecklistTest, ToJson_HasRequiredFields) {
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    Json::Value json = result.toJson();

    EXPECT_TRUE(json.isMember("certificateType"));
    EXPECT_TRUE(json.isMember("totalChecks"));
    EXPECT_TRUE(json.isMember("passCount"));
    EXPECT_TRUE(json.isMember("failCount"));
    EXPECT_TRUE(json.isMember("warningCount"));
    EXPECT_TRUE(json.isMember("naCount"));
    EXPECT_TRUE(json.isMember("overallStatus"));
    EXPECT_TRUE(json.isMember("items"));
    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["certificateType"].asString(), "DSC");
    EXPECT_EQ(json["totalChecks"].asInt(), result.totalChecks);
}

TEST_F(Doc9303ChecklistTest, CheckItemToJson_HasAllFields) {
    Doc9303CheckItem item;
    item.id = "test_id";
    item.category = "테스트";
    item.label = "테스트 항목";
    item.status = "PASS";
    item.message = "정상";
    item.requirement = "요구사항";

    Json::Value json = item.toJson();
    EXPECT_EQ(json["id"].asString(), "test_id");
    EXPECT_EQ(json["category"].asString(), "테스트");
    EXPECT_EQ(json["label"].asString(), "테스트 항목");
    EXPECT_EQ(json["status"].asString(), "PASS");
    EXPECT_EQ(json["message"].asString(), "정상");
    EXPECT_EQ(json["requirement"].asString(), "요구사항");
}

// =============================================================================
// Certificate Policies Check
// =============================================================================

TEST_F(Doc9303ChecklistTest, CertPolicies_Absent_IsNA) {
    CertificateBuilder::Options opts;
    opts.includeCertPolicies = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "cert_policies_non_critical"), "NA");
}

TEST_F(Doc9303ChecklistTest, CertPolicies_NonCritical_Passes) {
    CertificateBuilder::Options opts;
    opts.includeCertPolicies = true;
    opts.certPoliciesCritical = false;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC");
    EXPECT_EQ(getItemStatus(result, "cert_policies_non_critical"), "PASS");
}

// =============================================================================
// DSC_NC Type
// =============================================================================

TEST_F(Doc9303ChecklistTest, DSC_NC_CA_False_CheckPresent) {
    CertificateBuilder::Options opts;
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC_NC");
    // DSC_NC should have the same CA=FALSE check as DSC
    EXPECT_TRUE(hasItemWithId(result, "basic_constraints_ca_false"));
    EXPECT_EQ(getItemStatus(result, "basic_constraints_ca_false"), "PASS");
}

TEST_F(Doc9303ChecklistTest, DSC_NC_SubjectIssuerMatch_Checked) {
    CertificateBuilder::Options opts;
    opts.countryCode = "KR";
    opts.issuerCountryCode = "KR";
    opts.isCA = false;
    opts.keyUsageBits = KU_DIGITAL_SIGNATURE;
    X509* cert = buildAndTrack(opts);
    ASSERT_NE(cert, nullptr);

    auto result = runDoc9303Checklist(cert, "DSC_NC");
    EXPECT_TRUE(hasItemWithId(result, "subject_issuer_country_match"));
}

// =============================================================================
// Idempotency
// =============================================================================

TEST_F(Doc9303ChecklistTest, SameInput_SameOutput_Idempotent) {
    X509* cert = buildStandardDsc();
    ASSERT_NE(cert, nullptr);

    auto result1 = runDoc9303Checklist(cert, "DSC");
    auto result2 = runDoc9303Checklist(cert, "DSC");

    EXPECT_EQ(result1.totalChecks, result2.totalChecks);
    EXPECT_EQ(result1.passCount, result2.passCount);
    EXPECT_EQ(result1.failCount, result2.failCount);
    EXPECT_EQ(result1.overallStatus, result2.overallStatus);
    ASSERT_EQ(result1.items.size(), result2.items.size());
    for (size_t i = 0; i < result1.items.size(); i++) {
        EXPECT_EQ(result1.items[i].id, result2.items[i].id);
        EXPECT_EQ(result1.items[i].status, result2.items[i].status);
    }
}

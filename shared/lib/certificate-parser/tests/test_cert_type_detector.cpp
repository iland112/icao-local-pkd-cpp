/**
 * @file test_cert_type_detector.cpp
 * @brief Unit tests for ICAO certificate type detector (cert_type_detector.h / .cpp)
 */

#include <gtest/gtest.h>
#include "cert_type_detector.h"
#include "test_helpers.h"
#include <string>
#include <algorithm>
#include <cctype>

using namespace icao::certificate_parser;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class CertTypeDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        cscaKey_.reset(test_helpers::generateRsaKey(2048));
        ASSERT_NE(cscaKey_.get(), nullptr);

        csca_.reset(test_helpers::createSelfSignedCert("KR", "Korea CSCA", 3650, cscaKey_.get()));
        ASSERT_NE(csca_.get(), nullptr);
    }

    test_helpers::EvpPkeyPtr cscaKey_;
    test_helpers::X509Ptr csca_;
};

// ---------------------------------------------------------------------------
// detectType
// ---------------------------------------------------------------------------

TEST_F(CertTypeDetectorTest, DetectType_CSCA) {
    CertificateInfo info = CertTypeDetector::detectType(csca_.get());

    EXPECT_EQ(info.type, CertificateType::CSCA);
    EXPECT_EQ(info.country, "KR");
    EXPECT_TRUE(info.is_self_signed);
    EXPECT_TRUE(info.is_ca);
    EXPECT_TRUE(info.has_key_cert_sign);
    EXPECT_TRUE(info.error_message.empty());
    EXPECT_FALSE(info.fingerprint.empty());
    EXPECT_EQ(info.fingerprint.length(), 64u);  // SHA-256 hex
    EXPECT_FALSE(info.subject_dn.empty());
    EXPECT_FALSE(info.issuer_dn.empty());
}

TEST_F(CertTypeDetectorTest, DetectType_DSC) {
    X509* dsc = test_helpers::createDscCert(
        csca_.get(), cscaKey_.get(), "KR", "Korea DSC");
    ASSERT_NE(dsc, nullptr);

    CertificateInfo info = CertTypeDetector::detectType(dsc);

    EXPECT_EQ(info.type, CertificateType::DSC);
    EXPECT_EQ(info.country, "KR");
    EXPECT_FALSE(info.is_self_signed);
    EXPECT_FALSE(info.is_ca);
    EXPECT_FALSE(info.has_key_cert_sign);

    X509_free(dsc);
}

TEST_F(CertTypeDetectorTest, DetectType_MLSC) {
    X509* mlsc = test_helpers::createMlscCert(csca_.get(), cscaKey_.get());
    ASSERT_NE(mlsc, nullptr);

    CertificateInfo info = CertTypeDetector::detectType(mlsc);

    EXPECT_EQ(info.type, CertificateType::MLSC);
    EXPECT_EQ(info.country, "DE");

    X509_free(mlsc);
}

TEST_F(CertTypeDetectorTest, DetectType_LinkCert) {
    X509* link = test_helpers::createLinkCert(csca_.get(), cscaKey_.get());
    ASSERT_NE(link, nullptr);

    CertificateInfo info = CertTypeDetector::detectType(link);

    EXPECT_EQ(info.type, CertificateType::LINK_CERT);
    EXPECT_FALSE(info.is_self_signed);
    EXPECT_TRUE(info.is_ca);
    EXPECT_TRUE(info.has_key_cert_sign);

    X509_free(link);
}

TEST_F(CertTypeDetectorTest, DetectType_NullCert) {
    CertificateInfo info = CertTypeDetector::detectType(nullptr);

    EXPECT_EQ(info.type, CertificateType::UNKNOWN);
    EXPECT_FALSE(info.error_message.empty());
}

// ---------------------------------------------------------------------------
// isMasterListSigner / isDeviationListSigner / isDocumentSigner
// ---------------------------------------------------------------------------

TEST_F(CertTypeDetectorTest, IsMasterListSigner_True) {
    X509* mlsc = test_helpers::createMlscCert(csca_.get(), cscaKey_.get());
    ASSERT_NE(mlsc, nullptr);

    EXPECT_TRUE(CertTypeDetector::isMasterListSigner(mlsc));
    EXPECT_FALSE(CertTypeDetector::isDeviationListSigner(mlsc));

    X509_free(mlsc);
}

TEST_F(CertTypeDetectorTest, IsMasterListSigner_False_ForCSCA) {
    EXPECT_FALSE(CertTypeDetector::isMasterListSigner(csca_.get()));
}

TEST_F(CertTypeDetectorTest, IsMasterListSigner_NullCert) {
    EXPECT_FALSE(CertTypeDetector::isMasterListSigner(nullptr));
}

TEST_F(CertTypeDetectorTest, IsDeviationListSigner_False_ForDsc) {
    X509* dsc = test_helpers::createDscCert(csca_.get(), cscaKey_.get());
    ASSERT_NE(dsc, nullptr);

    EXPECT_FALSE(CertTypeDetector::isDeviationListSigner(dsc));

    X509_free(dsc);
}

// ---------------------------------------------------------------------------
// typeToString / stringToType
// ---------------------------------------------------------------------------

TEST_F(CertTypeDetectorTest, TypeToString_AllTypes) {
    EXPECT_EQ(CertTypeDetector::typeToString(CertificateType::CSCA), "CSCA");
    EXPECT_EQ(CertTypeDetector::typeToString(CertificateType::DSC), "DSC");
    EXPECT_EQ(CertTypeDetector::typeToString(CertificateType::DSC_NC), "DSC_NC");
    EXPECT_EQ(CertTypeDetector::typeToString(CertificateType::MLSC), "MLSC");
    EXPECT_EQ(CertTypeDetector::typeToString(CertificateType::LINK_CERT), "LINK_CERT");
    EXPECT_EQ(CertTypeDetector::typeToString(CertificateType::DL_SIGNER), "DL_SIGNER");
    EXPECT_EQ(CertTypeDetector::typeToString(CertificateType::UNKNOWN), "UNKNOWN");
}

TEST_F(CertTypeDetectorTest, StringToType_AllTypes) {
    EXPECT_EQ(CertTypeDetector::stringToType("CSCA"), CertificateType::CSCA);
    EXPECT_EQ(CertTypeDetector::stringToType("DSC"), CertificateType::DSC);
    EXPECT_EQ(CertTypeDetector::stringToType("DSC_NC"), CertificateType::DSC_NC);
    EXPECT_EQ(CertTypeDetector::stringToType("MLSC"), CertificateType::MLSC);
    EXPECT_EQ(CertTypeDetector::stringToType("LINK_CERT"), CertificateType::LINK_CERT);
    EXPECT_EQ(CertTypeDetector::stringToType("DL_SIGNER"), CertificateType::DL_SIGNER);
}

TEST_F(CertTypeDetectorTest, StringToType_CaseInsensitive) {
    EXPECT_EQ(CertTypeDetector::stringToType("csca"), CertificateType::CSCA);
    EXPECT_EQ(CertTypeDetector::stringToType("dsc"), CertificateType::DSC);
    EXPECT_EQ(CertTypeDetector::stringToType("mlsc"), CertificateType::MLSC);
}

TEST_F(CertTypeDetectorTest, StringToType_Unknown) {
    EXPECT_EQ(CertTypeDetector::stringToType(""), CertificateType::UNKNOWN);
    EXPECT_EQ(CertTypeDetector::stringToType("INVALID"), CertificateType::UNKNOWN);
}

TEST_F(CertTypeDetectorTest, TypeToString_RoundTrip) {
    CertificateType types[] = {
        CertificateType::CSCA, CertificateType::DSC,
        CertificateType::DSC_NC, CertificateType::MLSC,
        CertificateType::LINK_CERT, CertificateType::DL_SIGNER,
        CertificateType::UNKNOWN
    };

    for (auto t : types) {
        std::string str = CertTypeDetector::typeToString(t);
        CertificateType roundTripped = CertTypeDetector::stringToType(str);
        EXPECT_EQ(roundTripped, t) << "Round trip failed for: " << str;
    }
}

// ---------------------------------------------------------------------------
// Fingerprint properties
// ---------------------------------------------------------------------------

TEST_F(CertTypeDetectorTest, Fingerprint_Is64HexChars) {
    CertificateInfo info = CertTypeDetector::detectType(csca_.get());

    EXPECT_EQ(info.fingerprint.length(), 64u);
    for (char c : info.fingerprint) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Unexpected character in fingerprint: " << c;
    }
}

TEST_F(CertTypeDetectorTest, Fingerprint_DifferentCertsDifferentFingerprints) {
    X509* dsc = test_helpers::createDscCert(csca_.get(), cscaKey_.get(), "JP", "Japan DSC");
    ASSERT_NE(dsc, nullptr);

    CertificateInfo infoCSCA = CertTypeDetector::detectType(csca_.get());
    CertificateInfo infoDSC = CertTypeDetector::detectType(dsc);

    EXPECT_NE(infoCSCA.fingerprint, infoDSC.fingerprint);

    X509_free(dsc);
}

// ---------------------------------------------------------------------------
// Country extraction
// ---------------------------------------------------------------------------

TEST_F(CertTypeDetectorTest, CountryExtraction_ValidCountry) {
    CertificateInfo info = CertTypeDetector::detectType(csca_.get());
    EXPECT_EQ(info.country, "KR");
}

TEST_F(CertTypeDetectorTest, CountryExtraction_DifferentCountries) {
    X509* de = test_helpers::createSelfSignedCert("DE", "Germany CSCA");
    ASSERT_NE(de, nullptr);

    CertificateInfo info = CertTypeDetector::detectType(de);
    EXPECT_EQ(info.country, "DE");

    X509_free(de);
}

// ---------------------------------------------------------------------------
// Self-signed detection
// ---------------------------------------------------------------------------

TEST_F(CertTypeDetectorTest, SelfSigned_CSCA) {
    CertificateInfo info = CertTypeDetector::detectType(csca_.get());
    EXPECT_TRUE(info.is_self_signed);
    EXPECT_EQ(info.subject_dn, info.issuer_dn);
}

TEST_F(CertTypeDetectorTest, NotSelfSigned_DSC) {
    X509* dsc = test_helpers::createDscCert(csca_.get(), cscaKey_.get());
    ASSERT_NE(dsc, nullptr);

    CertificateInfo info = CertTypeDetector::detectType(dsc);
    EXPECT_FALSE(info.is_self_signed);
    EXPECT_NE(info.subject_dn, info.issuer_dn);

    X509_free(dsc);
}

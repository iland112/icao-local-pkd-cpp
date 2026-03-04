/**
 * @file test_pem_parser.cpp
 * @brief Unit tests for PEM format parser (pem_parser.h / pem_parser.cpp)
 */

#include <gtest/gtest.h>
#include "pem_parser.h"
#include "test_helpers.h"
#include <vector>
#include <string>
#include <cstdint>

using namespace icao::certificate_parser;

// ---------------------------------------------------------------------------
// Fixture: creates a fresh self-signed cert for each test
// ---------------------------------------------------------------------------
class PemParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        csca_ = test_helpers::createSelfSignedCert("KR", "Test CSCA KR");
        ASSERT_NE(csca_, nullptr);

        pemString_ = test_helpers::certToPem(csca_);
        ASSERT_FALSE(pemString_.empty());

        pemData_.assign(pemString_.begin(), pemString_.end());
    }

    void TearDown() override {
        if (csca_) X509_free(csca_);
    }

    X509* csca_ = nullptr;
    std::string pemString_;
    std::vector<uint8_t> pemData_;
};

// ---------------------------------------------------------------------------
// PemParser::parse (vector<uint8_t>)
// ---------------------------------------------------------------------------

TEST_F(PemParserTest, Parse_ValidSingleCertificate) {
    PemParseResult result = PemParser::parse(pemData_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.certificateCount, 1);
    EXPECT_EQ(result.parseErrors, 0);
    ASSERT_EQ(result.certificates.size(), 1u);
    EXPECT_NE(result.certificates[0], nullptr);
    EXPECT_TRUE(result.errorMessage.empty());
}

TEST_F(PemParserTest, Parse_EmptyData) {
    std::vector<uint8_t> empty;
    PemParseResult result = PemParser::parse(empty);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificateCount, 0);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(PemParserTest, Parse_GarbageData) {
    std::string garbage = "this is not a PEM file at all";
    std::vector<uint8_t> data(garbage.begin(), garbage.end());
    PemParseResult result = PemParser::parse(data);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificateCount, 0);
}

TEST_F(PemParserTest, Parse_MultipleCertificates) {
    // Create a second certificate
    X509* dsc = test_helpers::createDscCert(nullptr, nullptr, "JP", "Test DSC JP");
    ASSERT_NE(dsc, nullptr);

    std::string multiPem = pemString_ + "\n" + test_helpers::certToPem(dsc);
    X509_free(dsc);

    std::vector<uint8_t> data(multiPem.begin(), multiPem.end());
    PemParseResult result = PemParser::parse(data);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.certificateCount, 2);
    EXPECT_EQ(result.parseErrors, 0);
    EXPECT_EQ(result.certificates.size(), 2u);
}

TEST_F(PemParserTest, Parse_InvalidPemBlock) {
    std::string invalidPem =
        "-----BEGIN CERTIFICATE-----\n"
        "INVALID_BASE64_DATA!!!\n"
        "-----END CERTIFICATE-----\n";
    std::vector<uint8_t> data(invalidPem.begin(), invalidPem.end());
    PemParseResult result = PemParser::parse(data);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificateCount, 0);
    EXPECT_GT(result.parseErrors, 0);
}

TEST_F(PemParserTest, Parse_MixedValidAndInvalid) {
    std::string mixed = pemString_ +
        "\n-----BEGIN CERTIFICATE-----\nBADDATA\n-----END CERTIFICATE-----\n";
    std::vector<uint8_t> data(mixed.begin(), mixed.end());
    PemParseResult result = PemParser::parse(data);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.certificateCount, 1);
    EXPECT_EQ(result.parseErrors, 1);
}

// ---------------------------------------------------------------------------
// PemParser::parse (string)
// ---------------------------------------------------------------------------

TEST_F(PemParserTest, ParseString_ValidPem) {
    PemParseResult result = PemParser::parse(pemString_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.certificateCount, 1);
}

TEST_F(PemParserTest, ParseString_EmptyString) {
    PemParseResult result = PemParser::parse(std::string(""));

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificateCount, 0);
}

// ---------------------------------------------------------------------------
// PemParser::parseSingle
// ---------------------------------------------------------------------------

TEST_F(PemParserTest, ParseSingle_Valid) {
    X509* cert = PemParser::parseSingle(pemData_);
    ASSERT_NE(cert, nullptr);

    // Verify we can read the subject
    X509_NAME* subject = X509_get_subject_name(cert);
    EXPECT_NE(subject, nullptr);

    X509_free(cert);
}

TEST_F(PemParserTest, ParseSingle_EmptyData) {
    std::vector<uint8_t> empty;
    X509* cert = PemParser::parseSingle(empty);
    EXPECT_EQ(cert, nullptr);
}

TEST_F(PemParserTest, ParseSingle_BinaryGarbage) {
    std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03};
    X509* cert = PemParser::parseSingle(garbage);
    EXPECT_EQ(cert, nullptr);
}

// ---------------------------------------------------------------------------
// PemParser::isPemFormat
// ---------------------------------------------------------------------------

TEST_F(PemParserTest, IsPemFormat_ValidPem) {
    EXPECT_TRUE(PemParser::isPemFormat(pemData_));
}

TEST_F(PemParserTest, IsPemFormat_DerData) {
    std::vector<uint8_t> der = test_helpers::certToDer(csca_);
    EXPECT_FALSE(PemParser::isPemFormat(der));
}

TEST_F(PemParserTest, IsPemFormat_TooSmall) {
    std::vector<uint8_t> tiny = {'a', 'b', 'c'};
    EXPECT_FALSE(PemParser::isPemFormat(tiny));
}

TEST_F(PemParserTest, IsPemFormat_Pkcs7Header) {
    std::string pkcs7 = "-----BEGIN PKCS7-----\ndata\n-----END PKCS7-----\n";
    std::vector<uint8_t> data(pkcs7.begin(), pkcs7.end());
    EXPECT_TRUE(PemParser::isPemFormat(data));
}

// ---------------------------------------------------------------------------
// PemParser::extractPemBlocks
// ---------------------------------------------------------------------------

TEST_F(PemParserTest, ExtractPemBlocks_SingleBlock) {
    auto blocks = PemParser::extractPemBlocks(pemString_);
    EXPECT_EQ(blocks.size(), 1u);
    EXPECT_NE(blocks[0].find("-----BEGIN CERTIFICATE-----"), std::string::npos);
    EXPECT_NE(blocks[0].find("-----END CERTIFICATE-----"), std::string::npos);
}

TEST_F(PemParserTest, ExtractPemBlocks_MultipleBlocks) {
    X509* dsc = test_helpers::createDscCert(nullptr, nullptr, "FR", "DSC FR");
    ASSERT_NE(dsc, nullptr);
    std::string multiPem = pemString_ + "\n" + test_helpers::certToPem(dsc);
    X509_free(dsc);

    auto blocks = PemParser::extractPemBlocks(multiPem);
    EXPECT_EQ(blocks.size(), 2u);
}

TEST_F(PemParserTest, ExtractPemBlocks_NoBlocks) {
    auto blocks = PemParser::extractPemBlocks("no PEM content here");
    EXPECT_TRUE(blocks.empty());
}

// ---------------------------------------------------------------------------
// PemParser::toPem
// ---------------------------------------------------------------------------

TEST_F(PemParserTest, ToPem_ValidCert) {
    std::string pem = PemParser::toPem(csca_);
    EXPECT_FALSE(pem.empty());
    EXPECT_NE(pem.find("-----BEGIN CERTIFICATE-----"), std::string::npos);
    EXPECT_NE(pem.find("-----END CERTIFICATE-----"), std::string::npos);
}

TEST_F(PemParserTest, ToPem_NullCert) {
    std::string pem = PemParser::toPem(nullptr);
    EXPECT_TRUE(pem.empty());
}

TEST_F(PemParserTest, ToPem_RoundTrip) {
    // cert -> PEM -> cert -> PEM should produce identical output
    std::string pem1 = PemParser::toPem(csca_);
    ASSERT_FALSE(pem1.empty());

    std::vector<uint8_t> data(pem1.begin(), pem1.end());
    X509* cert2 = PemParser::parseSingle(data);
    ASSERT_NE(cert2, nullptr);

    std::string pem2 = PemParser::toPem(cert2);
    X509_free(cert2);

    EXPECT_EQ(pem1, pem2);
}

// ---------------------------------------------------------------------------
// PemParseResult move semantics
// ---------------------------------------------------------------------------

TEST_F(PemParserTest, MoveSemantics_TransferOwnership) {
    PemParseResult result1 = PemParser::parse(pemData_);
    ASSERT_TRUE(result1.success);
    ASSERT_EQ(result1.certificates.size(), 1u);

    // Move to result2
    PemParseResult result2(std::move(result1));

    EXPECT_TRUE(result2.success);
    EXPECT_EQ(result2.certificateCount, 1);
    EXPECT_EQ(result2.certificates.size(), 1u);
    EXPECT_NE(result2.certificates[0], nullptr);

    // result1 should be empty after move
    EXPECT_TRUE(result1.certificates.empty());
}

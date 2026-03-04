/**
 * @file test_der_parser.cpp
 * @brief Unit tests for DER format parser (der_parser.h / der_parser.cpp)
 */

#include <gtest/gtest.h>
#include "der_parser.h"
#include "test_helpers.h"
#include <vector>
#include <cstdint>

using namespace icao::certificate_parser;

// ---------------------------------------------------------------------------
// Fixture: creates DER-encoded cert data for each test
// ---------------------------------------------------------------------------
class DerParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        csca_ = test_helpers::createSelfSignedCert("DE", "Test CSCA DE");
        ASSERT_NE(csca_, nullptr);

        derData_ = test_helpers::certToDer(csca_);
        ASSERT_FALSE(derData_.empty());
        ASSERT_EQ(derData_[0], 0x30);  // SEQUENCE tag
    }

    void TearDown() override {
        if (csca_) X509_free(csca_);
    }

    X509* csca_ = nullptr;
    std::vector<uint8_t> derData_;
};

// ---------------------------------------------------------------------------
// DerParser::parse
// ---------------------------------------------------------------------------

TEST_F(DerParserTest, Parse_ValidDer) {
    DerParseResult result = DerParser::parse(derData_);

    EXPECT_TRUE(result.success);
    EXPECT_NE(result.certificate, nullptr);
    EXPECT_TRUE(result.isValidDer);
    EXPECT_EQ(result.fileSize, derData_.size());
    EXPECT_TRUE(result.errorMessage.empty());
}

TEST_F(DerParserTest, Parse_EmptyData) {
    std::vector<uint8_t> empty;
    DerParseResult result = DerParser::parse(empty);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificate, nullptr);
    EXPECT_EQ(result.fileSize, 0u);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(DerParserTest, Parse_GarbageData) {
    std::vector<uint8_t> garbage = {0xFF, 0xFE, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    DerParseResult result = DerParser::parse(garbage);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificate, nullptr);
    EXPECT_FALSE(result.isValidDer);
}

TEST_F(DerParserTest, Parse_TruncatedData) {
    // Take only first 10 bytes of a valid DER certificate
    std::vector<uint8_t> truncated(derData_.begin(), derData_.begin() + 10);
    DerParseResult result = DerParser::parse(truncated);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificate, nullptr);
}

TEST_F(DerParserTest, Parse_PemDataShouldFail) {
    std::string pem = test_helpers::certToPem(csca_);
    std::vector<uint8_t> pemData(pem.begin(), pem.end());
    DerParseResult result = DerParser::parse(pemData);

    // PEM text starts with '-' (0x2D), not SEQUENCE (0x30)
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.certificate, nullptr);
}

// ---------------------------------------------------------------------------
// DerParser::isDerFormat
// ---------------------------------------------------------------------------

TEST_F(DerParserTest, IsDerFormat_ValidDer) {
    EXPECT_TRUE(DerParser::isDerFormat(derData_));
}

TEST_F(DerParserTest, IsDerFormat_TooSmall) {
    std::vector<uint8_t> tiny = {0x30};
    EXPECT_FALSE(DerParser::isDerFormat(tiny));
}

TEST_F(DerParserTest, IsDerFormat_EmptyData) {
    std::vector<uint8_t> empty;
    EXPECT_FALSE(DerParser::isDerFormat(empty));
}

TEST_F(DerParserTest, IsDerFormat_WrongTag) {
    std::vector<uint8_t> data = {0x31, 0x82, 0x03, 0x5D};  // SET tag instead of SEQUENCE
    EXPECT_FALSE(DerParser::isDerFormat(data));
}

TEST_F(DerParserTest, IsDerFormat_ShortFormLength) {
    // SEQUENCE tag with short-form length (< 128 bytes)
    std::vector<uint8_t> data = {0x30, 0x03, 0x01, 0x01, 0x00};
    EXPECT_TRUE(DerParser::isDerFormat(data));
}

TEST_F(DerParserTest, IsDerFormat_LongFormLength_81) {
    // 0x81 means 1 length byte follows
    std::vector<uint8_t> data = {0x30, 0x81, 0x80};
    data.resize(3 + 0x80, 0x00);  // fill content
    EXPECT_TRUE(DerParser::isDerFormat(data));
}

TEST_F(DerParserTest, IsDerFormat_LongFormLength_82) {
    // 0x82 means 2 length bytes follow
    std::vector<uint8_t> data = {0x30, 0x82, 0x01, 0x00};
    EXPECT_TRUE(DerParser::isDerFormat(data));
}

TEST_F(DerParserTest, IsDerFormat_InvalidLengthEncoding) {
    // 0x85 is invalid (> 4 length bytes)
    std::vector<uint8_t> data = {0x30, 0x85, 0x00, 0x00, 0x00, 0x00, 0x01};
    EXPECT_FALSE(DerParser::isDerFormat(data));
}

// ---------------------------------------------------------------------------
// DerParser::validateDerStructure
// ---------------------------------------------------------------------------

TEST_F(DerParserTest, ValidateDerStructure_ValidCert) {
    EXPECT_TRUE(DerParser::validateDerStructure(derData_));
}

TEST_F(DerParserTest, ValidateDerStructure_TruncatedContent) {
    // Create a DER header that claims more content than available
    std::vector<uint8_t> bad = {0x30, 0x82, 0xFF, 0xFF};  // claims 65535 bytes
    bad.resize(100, 0x00);  // but only 100 bytes
    EXPECT_FALSE(DerParser::validateDerStructure(bad));
}

TEST_F(DerParserTest, ValidateDerStructure_Empty) {
    std::vector<uint8_t> empty;
    EXPECT_FALSE(DerParser::validateDerStructure(empty));
}

// ---------------------------------------------------------------------------
// DerParser::getDerCertificateSize
// ---------------------------------------------------------------------------

TEST_F(DerParserTest, GetDerCertificateSize_ValidCert) {
    size_t size = DerParser::getDerCertificateSize(derData_);
    EXPECT_GT(size, 0u);
    // The reported size should match or be less than the actual data
    EXPECT_LE(size, derData_.size());
}

TEST_F(DerParserTest, GetDerCertificateSize_TooSmall) {
    std::vector<uint8_t> tiny = {0x30};
    EXPECT_EQ(DerParser::getDerCertificateSize(tiny), 0u);
}

TEST_F(DerParserTest, GetDerCertificateSize_WrongTag) {
    std::vector<uint8_t> data = {0x31, 0x82, 0x03, 0x5D};
    EXPECT_EQ(DerParser::getDerCertificateSize(data), 0u);
}

TEST_F(DerParserTest, GetDerCertificateSize_ShortForm) {
    // SEQUENCE of 5 bytes content: tag(1) + length(1) + content(5) = 7
    std::vector<uint8_t> data = {0x30, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05};
    size_t size = DerParser::getDerCertificateSize(data);
    EXPECT_EQ(size, 7u);  // 1 (tag) + 1 (length) + 5 (content)
}

// ---------------------------------------------------------------------------
// DerParser::toDer
// ---------------------------------------------------------------------------

TEST_F(DerParserTest, ToDer_ValidCert) {
    std::vector<uint8_t> der = DerParser::toDer(csca_);
    EXPECT_FALSE(der.empty());
    EXPECT_EQ(der[0], 0x30);  // SEQUENCE tag
}

TEST_F(DerParserTest, ToDer_NullCert) {
    std::vector<uint8_t> der = DerParser::toDer(nullptr);
    EXPECT_TRUE(der.empty());
}

TEST_F(DerParserTest, ToDer_RoundTrip) {
    // cert -> DER -> cert -> DER should produce identical bytes
    std::vector<uint8_t> der1 = DerParser::toDer(csca_);
    ASSERT_FALSE(der1.empty());

    DerParseResult result = DerParser::parse(der1);
    ASSERT_TRUE(result.success);
    ASSERT_NE(result.certificate, nullptr);

    std::vector<uint8_t> der2 = DerParser::toDer(result.certificate);
    EXPECT_EQ(der1, der2);
}

// ---------------------------------------------------------------------------
// DerParseResult move semantics
// ---------------------------------------------------------------------------

TEST_F(DerParserTest, MoveSemantics_TransferOwnership) {
    DerParseResult result1 = DerParser::parse(derData_);
    ASSERT_TRUE(result1.success);
    ASSERT_NE(result1.certificate, nullptr);

    DerParseResult result2(std::move(result1));

    EXPECT_TRUE(result2.success);
    EXPECT_NE(result2.certificate, nullptr);
    EXPECT_EQ(result1.certificate, nullptr);  // ownership transferred
}

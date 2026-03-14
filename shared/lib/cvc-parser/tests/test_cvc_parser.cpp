/**
 * @file test_cvc_parser.cpp
 * @brief Unit tests for CvcParser using real BSI TR-03110 Worked Example certificates
 *
 * Tests parse, type inference, OID extraction, date parsing, and CHR/CAR fields
 * using ECDH and DH certificate chains from the BSI EAC Worked Example (2011).
 */

#include <gtest/gtest.h>
#include "icao/cvc/cvc_parser.h"
#include "icao/cvc/cvc_certificate.h"
#include "icao/cvc/eac_oids.h"
#include "test_helpers.h"

using namespace icao::cvc;

// =============================================================================
// Fixture for ECDH chain
// =============================================================================

class EcdhCvcParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        cvcaData_ = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_CVCA_HEX);
        dvData_   = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_DV_HEX);
        isData_   = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_IS_HEX);
    }
    std::vector<uint8_t> cvcaData_, dvData_, isData_;
};

// =============================================================================
// Fixture for DH chain
// =============================================================================

class DhCvcParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        cvcaData_ = cvc_test_helpers::fromHex(cvc_test_helpers::DH_CVCA_HEX);
        isData_   = cvc_test_helpers::fromHex(cvc_test_helpers::DH_IS_HEX);
    }
    std::vector<uint8_t> cvcaData_, isData_;
};

// =============================================================================
// ECDH CVCA parsing tests
// =============================================================================

TEST_F(EcdhCvcParserTest, Parse_CVCA_Succeeds) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value()) << "ECDH CVCA parse failed";
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_Type) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->type, CvcType::CVCA);
    EXPECT_EQ(cvcTypeToString(cert->type), "CVCA");
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_ChrAndCar) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->chr, "DECVCAAT00001");
    EXPECT_EQ(cert->car, "DECVCAAT00001");  // self-signed: CAR == CHR
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_CountryCode) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->countryCode, "DE");
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_AlgorithmOid) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    // ECDH CVCA uses id-TA-ECDSA-SHA-512
    EXPECT_EQ(cert->publicKey.algorithmOid, std::string(oid::TA_ECDSA_SHA_512));
    EXPECT_EQ(cert->publicKey.algorithmName, "id-TA-ECDSA-SHA-512");
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_ValidityDates) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    // BSI date encoding: each byte = one decimal digit
    // Effective: 2010-09-30, Expiration: 2011-09-25
    EXPECT_EQ(cert->effectiveDate, "2010-09-30");
    EXPECT_EQ(cert->expirationDate, "2011-09-25");
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_Fingerprint_NotEmpty) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->fingerprintSha256.length(), 64u);
    EXPECT_FALSE(cert->fingerprintSha256.empty());
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_BodyRaw_NotEmpty) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_FALSE(cert->bodyRaw.empty());
    EXPECT_FALSE(cert->signature.empty());
}

TEST_F(EcdhCvcParserTest, Parse_CVCA_PublicKey_HasEcParams) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    // ECDH CVCA includes full EC domain params
    EXPECT_FALSE(cert->publicKey.prime.empty());
    EXPECT_FALSE(cert->publicKey.generator.empty());
    EXPECT_FALSE(cert->publicKey.order.empty());
    EXPECT_FALSE(cert->publicKey.publicPoint.empty());
}

// =============================================================================
// ECDH DV parsing tests
// =============================================================================

TEST_F(EcdhCvcParserTest, Parse_DV_Succeeds) {
    auto cert = CvcParser::parse(dvData_);
    ASSERT_TRUE(cert.has_value()) << "ECDH DV parse failed";
}

TEST_F(EcdhCvcParserTest, Parse_DV_Type) {
    auto cert = CvcParser::parse(dvData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->type, CvcType::DV_DOMESTIC);
}

TEST_F(EcdhCvcParserTest, Parse_DV_ChrAndCar) {
    auto cert = CvcParser::parse(dvData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->chr, "DETESTDVDE019");
    EXPECT_EQ(cert->car, "DECVCAAT00001");  // Issued by ECDH CVCA
}

TEST_F(EcdhCvcParserTest, Parse_DV_AlgorithmOid) {
    auto cert = CvcParser::parse(dvData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->publicKey.algorithmOid, std::string(oid::TA_ECDSA_SHA_512));
}

// =============================================================================
// ECDH IS (terminal) parsing tests
// =============================================================================

TEST_F(EcdhCvcParserTest, Parse_IS_Succeeds) {
    auto cert = CvcParser::parse(isData_);
    ASSERT_TRUE(cert.has_value()) << "ECDH IS parse failed";
}

TEST_F(EcdhCvcParserTest, Parse_IS_Type) {
    auto cert = CvcParser::parse(isData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->type, CvcType::IS);
}

TEST_F(EcdhCvcParserTest, Parse_IS_ChrAndCar) {
    auto cert = CvcParser::parse(isData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->chr, "DETESTATDE019");
    EXPECT_EQ(cert->car, "DETESTDVDE019");
}

TEST_F(EcdhCvcParserTest, Parse_IS_ChatRole_AT) {
    auto cert = CvcParser::parse(isData_);
    ASSERT_TRUE(cert.has_value());
    // CHAT role OID for AT: 0.4.0.127.0.7.3.1.2.2
    EXPECT_EQ(cert->chat.roleOid, std::string(oid::ROLE_AT));
    EXPECT_EQ(cert->chat.role, ChatRole::AT);
}

// =============================================================================
// DH CVCA parsing tests (RSA algorithm)
// =============================================================================

TEST_F(DhCvcParserTest, Parse_CVCA_Succeeds) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value()) << "DH CVCA parse failed";
}

TEST_F(DhCvcParserTest, Parse_CVCA_Type) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->type, CvcType::CVCA);
}

TEST_F(DhCvcParserTest, Parse_CVCA_ChrAndCar) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->chr, "DETESTCVCA00003");
    EXPECT_EQ(cert->car, "DETESTCVCA00003");
}

TEST_F(DhCvcParserTest, Parse_CVCA_AlgorithmOid_RSA) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    // DH CVCA uses id-TA-RSA-v1-5-SHA-1
    EXPECT_EQ(cert->publicKey.algorithmOid, std::string(oid::TA_RSA_V1_5_SHA_1));
    EXPECT_EQ(cert->publicKey.algorithmName, "id-TA-RSA-v1-5-SHA-1");
}

TEST_F(DhCvcParserTest, Parse_CVCA_RsaKey_HasModulusAndExponent) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    // RSA: modulus (tag 0x81) and exponent (tag 0x82)
    EXPECT_FALSE(cert->publicKey.modulus.empty());
    EXPECT_FALSE(cert->publicKey.exponent.empty());
}

TEST_F(DhCvcParserTest, Parse_CVCA_ValidityDates) {
    auto cert = CvcParser::parse(cvcaData_);
    ASSERT_TRUE(cert.has_value());
    // BSI date encoding: each byte = one decimal digit
    // Effective: 2010-03-24, Expiration: 2011-03-19
    EXPECT_EQ(cert->effectiveDate, "2010-03-24");
    EXPECT_EQ(cert->expirationDate, "2011-03-19");
}

TEST_F(DhCvcParserTest, Parse_IS_Type) {
    auto cert = CvcParser::parse(isData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->type, CvcType::IS);
}

TEST_F(DhCvcParserTest, Parse_IS_AlgorithmOid_RSA) {
    auto cert = CvcParser::parse(isData_);
    ASSERT_TRUE(cert.has_value());
    EXPECT_EQ(cert->publicKey.algorithmOid, std::string(oid::TA_RSA_V1_5_SHA_1));
}

// =============================================================================
// Fingerprint consistency
// =============================================================================

TEST(CvcParserFingerprint, SameBytesGiveSameFingerprint) {
    auto data = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_CVCA_HEX);
    auto cert1 = CvcParser::parse(data);
    auto cert2 = CvcParser::parse(data);
    ASSERT_TRUE(cert1.has_value());
    ASSERT_TRUE(cert2.has_value());
    EXPECT_EQ(cert1->fingerprintSha256, cert2->fingerprintSha256);
}

TEST(CvcParserFingerprint, DifferentCertsGiveDifferentFingerprints) {
    auto data1 = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_CVCA_HEX);
    auto data2 = cvc_test_helpers::fromHex(cvc_test_helpers::DH_CVCA_HEX);
    auto cert1 = CvcParser::parse(data1);
    auto cert2 = CvcParser::parse(data2);
    ASSERT_TRUE(cert1.has_value());
    ASSERT_TRUE(cert2.has_value());
    EXPECT_NE(cert1->fingerprintSha256, cert2->fingerprintSha256);
}

// =============================================================================
// Error handling
// =============================================================================

TEST(CvcParserErrors, Parse_EmptyData_ReturnsNullopt) {
    std::vector<uint8_t> empty;
    EXPECT_FALSE(CvcParser::parse(empty).has_value());
}

TEST(CvcParserErrors, Parse_TruncatedData_ReturnsNullopt) {
    auto data = cvc_test_helpers::fromHex(cvc_test_helpers::TRUNCATED_CVC_HEX);
    EXPECT_FALSE(CvcParser::parse(data).has_value());
}

TEST(CvcParserErrors, Parse_WrongOuterTag_ReturnsNullopt) {
    auto data = cvc_test_helpers::fromHex(cvc_test_helpers::WRONG_TAG_HEX);
    EXPECT_FALSE(CvcParser::parse(data).has_value());
}

TEST(CvcParserErrors, Parse_NullptrData_ReturnsNullopt) {
    EXPECT_FALSE(CvcParser::parse(nullptr, 0).has_value());
}

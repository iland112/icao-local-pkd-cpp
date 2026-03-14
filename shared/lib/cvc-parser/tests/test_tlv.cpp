/**
 * @file test_tlv.cpp
 * @brief Unit tests for TlvParser (tlv.h / tlv.cpp)
 *
 * Tests TLV parsing, OID decoding, BCD date decoding using
 * real BSI TR-03110 EAC Worked Example certificate bytes.
 */

#include <gtest/gtest.h>
#include "icao/cvc/tlv.h"
#include "icao/cvc/eac_oids.h"
#include "test_helpers.h"

using namespace icao::cvc;

// =============================================================================
// TlvParser::parse — basic parsing
// =============================================================================

TEST(TlvParser, Parse_SingleByteTag) {
    // Tag=0x42 (CAR), Length=3, Value={0x41, 0x42, 0x43}
    uint8_t data[] = {0x42, 0x03, 0x41, 0x42, 0x43};
    auto elem = TlvParser::parse(data, sizeof(data));

    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->tag, 0x42u);
    EXPECT_EQ(elem->value, (std::vector<uint8_t>{0x41, 0x42, 0x43}));
    EXPECT_EQ(elem->valueLength, 3u);
    EXPECT_EQ(elem->totalLength, 5u);
}

TEST(TlvParser, Parse_TwoByteTag) {
    // Tag=0x7F21 (CV_CERTIFICATE), Length=5, Value={0x01,0x02,0x03,0x04,0x05}
    uint8_t data[] = {0x7F, 0x21, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05};
    auto elem = TlvParser::parse(data, sizeof(data));

    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->tag, 0x7F21u);
    EXPECT_EQ(elem->valueLength, 5u);
    EXPECT_EQ(elem->totalLength, 8u);
}

TEST(TlvParser, Parse_LongFormLength_OneExtraByte) {
    // Tag=0x42, Length=0x81 0x80 (128 bytes)
    std::vector<uint8_t> data = {0x42, 0x81, 0x80};
    data.resize(3 + 128, 0xAB);
    auto elem = TlvParser::parse(data.data(), data.size());

    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->valueLength, 128u);
    EXPECT_EQ(elem->totalLength, 131u);
}

TEST(TlvParser, Parse_LongFormLength_TwoExtraBytes) {
    // Tag=0x42, Length=0x82 0x01 0x00 (256 bytes)
    std::vector<uint8_t> data = {0x42, 0x82, 0x01, 0x00};
    data.resize(4 + 256, 0xCD);
    auto elem = TlvParser::parse(data.data(), data.size());

    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->valueLength, 256u);
}

TEST(TlvParser, Parse_EmptyData_ReturnsNullopt) {
    auto elem = TlvParser::parse(nullptr, 0);
    EXPECT_FALSE(elem.has_value());

    uint8_t buf[] = {};
    auto elem2 = TlvParser::parse(buf, 0);
    EXPECT_FALSE(elem2.has_value());
}

TEST(TlvParser, Parse_TruncatedData_ReturnsNullopt) {
    // Tag=0x42, Length=10, but only 5 bytes of value
    uint8_t data[] = {0x42, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05};
    auto elem = TlvParser::parse(data, sizeof(data));
    EXPECT_FALSE(elem.has_value());
}

// =============================================================================
// TlvParser::parseChildren
// =============================================================================

TEST(TlvParser, ParseChildren_MultipleElements) {
    // Two TLV elements: [0x42, 0x02, 0xAA, 0xBB] and [0x53, 0x01, 0xFF]
    uint8_t data[] = {0x42, 0x02, 0xAA, 0xBB, 0x53, 0x01, 0xFF};
    auto children = TlvParser::parseChildren(data, sizeof(data));

    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0].tag, 0x42u);
    EXPECT_EQ(children[0].value, (std::vector<uint8_t>{0xAA, 0xBB}));
    EXPECT_EQ(children[1].tag, 0x53u);
    EXPECT_EQ(children[1].value, (std::vector<uint8_t>{0xFF}));
}

TEST(TlvParser, ParseChildren_EmptyData_ReturnsEmpty) {
    uint8_t data[] = {};
    auto children = TlvParser::parseChildren(data, 0);
    EXPECT_TRUE(children.empty());
}

// =============================================================================
// TlvParser::findTag
// =============================================================================

TEST(TlvParser, FindTag_Found) {
    uint8_t data[] = {0x42, 0x02, 0xAA, 0xBB, 0x53, 0x01, 0xFF};
    auto found = TlvParser::findTag(data, sizeof(data), 0x53);

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->tag, 0x53u);
    EXPECT_EQ(found->value, (std::vector<uint8_t>{0xFF}));
}

TEST(TlvParser, FindTag_NotFound_ReturnsNullopt) {
    uint8_t data[] = {0x42, 0x02, 0xAA, 0xBB};
    auto found = TlvParser::findTag(data, sizeof(data), 0x99);
    EXPECT_FALSE(found.has_value());
}

// =============================================================================
// TlvParser::decodeOid — BSI OID values
// =============================================================================

TEST(TlvParser, DecodeOid_RoleIS) {
    // 0.4.0.127.0.7.3.1.2.1 → DER: 04 00 7F 00 07 03 01 02 01
    std::vector<uint8_t> oidBytes = {0x04, 0x00, 0x7F, 0x00, 0x07, 0x03, 0x01, 0x02, 0x01};
    std::string oid = TlvParser::decodeOid(oidBytes);
    EXPECT_EQ(oid, std::string(icao::cvc::oid::ROLE_IS));
}

TEST(TlvParser, DecodeOid_RoleAT) {
    // 0.4.0.127.0.7.3.1.2.2
    std::vector<uint8_t> oidBytes = {0x04, 0x00, 0x7F, 0x00, 0x07, 0x03, 0x01, 0x02, 0x02};
    std::string oid = TlvParser::decodeOid(oidBytes);
    EXPECT_EQ(oid, std::string(icao::cvc::oid::ROLE_AT));
}

TEST(TlvParser, DecodeOid_TaEcdsaSha512) {
    // 0.4.0.127.0.7.2.2.2.2.5 (id-TA-ECDSA-SHA-512)
    // DER: 04 00 7F 00 07 02 02 02 02 05
    std::vector<uint8_t> oidBytes = {0x04, 0x00, 0x7F, 0x00, 0x07, 0x02, 0x02, 0x02, 0x02, 0x05};
    std::string oid = TlvParser::decodeOid(oidBytes);
    EXPECT_EQ(oid, std::string(icao::cvc::oid::TA_ECDSA_SHA_512));
}

TEST(TlvParser, DecodeOid_TaRsaSha1) {
    // 0.4.0.127.0.7.2.2.2.1.1 (id-TA-RSA-v1-5-SHA-1)
    // DER: 04 00 7F 00 07 02 02 02 01 01
    std::vector<uint8_t> oidBytes = {0x04, 0x00, 0x7F, 0x00, 0x07, 0x02, 0x02, 0x02, 0x01, 0x01};
    std::string oid = TlvParser::decodeOid(oidBytes);
    EXPECT_EQ(oid, std::string(icao::cvc::oid::TA_RSA_V1_5_SHA_1));
}

TEST(TlvParser, DecodeOid_EmptyBytes_ReturnsEmpty) {
    std::vector<uint8_t> empty;
    std::string oid = TlvParser::decodeOid(empty);
    EXPECT_TRUE(oid.empty());
}

// =============================================================================
// TlvParser::decodeBcdDate
// =============================================================================

TEST(TlvParser, DecodeBcdDate_ValidDate) {
    // Each byte is one decimal digit (BSI TR-03110 date format)
    // {0x01, 0x00, 0x00, 0x09, 0x03, 0x00} → digits "1","0","0","9","3","0"
    // → YY=10, MM=09, DD=30 → 2010-09-30
    std::vector<uint8_t> dateBytes = {0x01, 0x00, 0x00, 0x09, 0x03, 0x00};
    std::string date = TlvParser::decodeBcdDate(dateBytes);
    EXPECT_EQ(date, "2010-09-30");
}

TEST(TlvParser, DecodeBcdDate_AnotherDate) {
    // {0x01, 0x01, 0x00, 0x09, 0x02, 0x05} → YY=11, MM=09, DD=25 → 2011-09-25
    std::vector<uint8_t> dateBytes = {0x01, 0x01, 0x00, 0x09, 0x02, 0x05};
    std::string date = TlvParser::decodeBcdDate(dateBytes);
    EXPECT_EQ(date, "2011-09-25");
}

TEST(TlvParser, DecodeBcdDate_WrongLength_ReturnsEmpty) {
    std::vector<uint8_t> tooShort = {0x01, 0x00, 0x01};
    EXPECT_TRUE(TlvParser::decodeBcdDate(tooShort).empty());

    std::vector<uint8_t> empty;
    EXPECT_TRUE(TlvParser::decodeBcdDate(empty).empty());
}

// =============================================================================
// Real BSI certificate outer TLV structure
// =============================================================================

TEST(TlvParser, ParseRealCvcOuterTag_ECDH_CVCA) {
    auto data = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_CVCA_HEX);
    auto elem = TlvParser::parse(data.data(), data.size());

    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->tag, 0x7F21u);  // CV_CERTIFICATE tag
    EXPECT_GT(elem->valueLength, 0u);
}

TEST(TlvParser, ParseRealCvcOuterTag_DH_CVCA) {
    auto data = cvc_test_helpers::fromHex(cvc_test_helpers::DH_CVCA_HEX);
    auto elem = TlvParser::parse(data.data(), data.size());

    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->tag, 0x7F21u);
}

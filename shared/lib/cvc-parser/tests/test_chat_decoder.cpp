/**
 * @file test_chat_decoder.cpp
 * @brief Unit tests for ChatDecoder (chat_decoder.h / chat_decoder.cpp)
 *
 * Tests role OID decoding and bitmask permission decoding for IS, AT, ST roles
 * per BSI TR-03110 Part 3, Tables C.3 / C.4 / C.5.
 */

#include <gtest/gtest.h>
#include "icao/cvc/chat_decoder.h"
#include "icao/cvc/cvc_certificate.h"
#include "icao/cvc/eac_oids.h"
#include "icao/cvc/cvc_parser.h"
#include "test_helpers.h"

#include <algorithm>

using namespace icao::cvc;

// =============================================================================
// ChatDecoder::decodeRole
// =============================================================================

TEST(ChatDecoderRole, DecodeRole_IS) {
    EXPECT_EQ(ChatDecoder::decodeRole(std::string(oid::ROLE_IS)), ChatRole::IS);
}

TEST(ChatDecoderRole, DecodeRole_AT) {
    EXPECT_EQ(ChatDecoder::decodeRole(std::string(oid::ROLE_AT)), ChatRole::AT);
}

TEST(ChatDecoderRole, DecodeRole_ST) {
    EXPECT_EQ(ChatDecoder::decodeRole(std::string(oid::ROLE_ST)), ChatRole::ST);
}

TEST(ChatDecoderRole, DecodeRole_Unknown_OID) {
    EXPECT_EQ(ChatDecoder::decodeRole("0.4.0.127.0.7.3.1.2.9"), ChatRole::UNKNOWN);
}

TEST(ChatDecoderRole, DecodeRole_Empty_String) {
    EXPECT_EQ(ChatDecoder::decodeRole(""), ChatRole::UNKNOWN);
}

// =============================================================================
// IS permissions (BSI TR-03110-3 Table C.3)
// 1 byte:
//   Bit 0 (0x01): Read DG3 (Fingerprint)
//   Bit 1 (0x02): Read DG4 (Iris)
//   Bit 6 (0x40): Install Certificate
//   Bit 7 (0x80): Install Qualified Certificate
// =============================================================================

TEST(ChatDecoderIS, NoBits_EmptyPermissions) {
    std::vector<uint8_t> bits = {0x00};
    auto perms = ChatDecoder::decodeIsPermissions(bits);
    EXPECT_TRUE(perms.empty());
}

TEST(ChatDecoderIS, Bit0_ReadDG3) {
    std::vector<uint8_t> bits = {0x01};
    auto perms = ChatDecoder::decodeIsPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Read DG3 (Fingerprint)");
}

TEST(ChatDecoderIS, Bit1_ReadDG4) {
    std::vector<uint8_t> bits = {0x02};
    auto perms = ChatDecoder::decodeIsPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Read DG4 (Iris)");
}

TEST(ChatDecoderIS, Bit6_InstallCertificate) {
    std::vector<uint8_t> bits = {0x40};
    auto perms = ChatDecoder::decodeIsPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Install Certificate");
}

TEST(ChatDecoderIS, Bit7_InstallQualifiedCertificate) {
    std::vector<uint8_t> bits = {0x80};
    auto perms = ChatDecoder::decodeIsPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Install Qualified Certificate");
}

TEST(ChatDecoderIS, AllBits_0xC3_AllFourPermissions) {
    // CVCA uses 0xC3 = 11000011: bits 0,1,6,7 all set
    std::vector<uint8_t> bits = {0xC3};
    auto perms = ChatDecoder::decodeIsPermissions(bits);
    EXPECT_EQ(perms.size(), 4u);

    auto hasPermission = [&](const std::string& name) {
        return std::find(perms.begin(), perms.end(), name) != perms.end();
    };
    EXPECT_TRUE(hasPermission("Read DG3 (Fingerprint)"));
    EXPECT_TRUE(hasPermission("Read DG4 (Iris)"));
    EXPECT_TRUE(hasPermission("Install Certificate"));
    EXPECT_TRUE(hasPermission("Install Qualified Certificate"));
}

TEST(ChatDecoderIS, EmptyAuthBits_EmptyPermissions) {
    std::vector<uint8_t> empty;
    auto perms = ChatDecoder::decodeIsPermissions(empty);
    EXPECT_TRUE(perms.empty());
}

TEST(ChatDecoderIS, MultiByte_UsesLastByte) {
    // Only last byte matters for IS
    std::vector<uint8_t> bits = {0x00, 0x01};
    auto perms = ChatDecoder::decodeIsPermissions(bits);
    EXPECT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Read DG3 (Fingerprint)");
}

// =============================================================================
// AT permissions (BSI TR-03110-3 Table C.4)
// Up to 5 bytes (40 bits), right-aligned:
//   Bit 0: Age Verification
//   Bit 1: Community ID Verification
//   Bit 2: Restricted Identification
//   Bit 3: Privileged Terminal
//   Bit 4: CAN allowed
//   Bit 5: PIN Management
//   Bit 6: Install Certificate
//   Bit 7: Install Qualified Certificate
//   Bits 8-28: Read DG21..DG1
// =============================================================================

TEST(ChatDecoderAT, NoBits_EmptyPermissions) {
    std::vector<uint8_t> bits = {0x00, 0x00, 0x00, 0x00, 0x00};
    auto perms = ChatDecoder::decodeAtPermissions(bits);
    EXPECT_TRUE(perms.empty());
}

TEST(ChatDecoderAT, Bit0_AgeVerification) {
    std::vector<uint8_t> bits = {0x00, 0x00, 0x00, 0x00, 0x01};
    auto perms = ChatDecoder::decodeAtPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Age Verification");
}

TEST(ChatDecoderAT, Bit4_CanAllowed) {
    std::vector<uint8_t> bits = {0x00, 0x00, 0x00, 0x00, 0x10};
    auto perms = ChatDecoder::decodeAtPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "CAN allowed");
}

TEST(ChatDecoderAT, ReadDG1_Bit28) {
    // Bit 28 = Read DG1. In 5-byte big-endian: byte[0] bit4 = bit 36?
    // bits are right-aligned: bit28 in 5-byte = byte index (5-1) - 28/8 = byte 1, bit 28%8=4
    // 5 bytes, bit 28: byte = 5 - 1 - (28/8) = 5-1-3 = 1, bitPos = 28%8 = 4
    // So byte[1] = 0x10
    std::vector<uint8_t> bits = {0x00, 0x10, 0x00, 0x00, 0x00};
    auto perms = ChatDecoder::decodeAtPermissions(bits);
    auto hasDG1 = std::find(perms.begin(), perms.end(), "Read DG1") != perms.end();
    EXPECT_TRUE(hasDG1);
}

TEST(ChatDecoderAT, EmptyAuthBits_EmptyPermissions) {
    std::vector<uint8_t> empty;
    auto perms = ChatDecoder::decodeAtPermissions(empty);
    EXPECT_TRUE(perms.empty());
}

// Test with actual BSI Worked Example ECDH IS cert CHAT bits: 0x00 0x00 0x00 0x01 0x10
// = bit 4 (CAN allowed) + bit 8 (Read DG21)
TEST(ChatDecoderAT, BsiWorkedExample_EcdhIs_Bits) {
    // From ECDH IS cert CHAT: 0x05 0x00 0x00 0x00 0x01 0x10
    // Last 5 bytes: {0x00, 0x00, 0x00, 0x01, 0x10}
    std::vector<uint8_t> bits = {0x00, 0x00, 0x00, 0x01, 0x10};
    auto perms = ChatDecoder::decodeAtPermissions(bits);
    auto hasCanAllowed = std::find(perms.begin(), perms.end(), "CAN allowed") != perms.end();
    auto hasDG21 = std::find(perms.begin(), perms.end(), "Read DG21") != perms.end();
    EXPECT_TRUE(hasCanAllowed);
    EXPECT_TRUE(hasDG21);
}

// =============================================================================
// ST permissions (BSI TR-03110-3 Table C.5)
// 1 byte:
//   Bit 0: Generate Electronic Signature
//   Bit 1: Generate Qualified Electronic Signature
// =============================================================================

TEST(ChatDecoderST, NoBits_EmptyPermissions) {
    std::vector<uint8_t> bits = {0x00};
    auto perms = ChatDecoder::decodeStPermissions(bits);
    EXPECT_TRUE(perms.empty());
}

TEST(ChatDecoderST, Bit0_GenerateSignature) {
    std::vector<uint8_t> bits = {0x01};
    auto perms = ChatDecoder::decodeStPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Generate Electronic Signature");
}

TEST(ChatDecoderST, Bit1_GenerateQualifiedSignature) {
    std::vector<uint8_t> bits = {0x02};
    auto perms = ChatDecoder::decodeStPermissions(bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Generate Qualified Electronic Signature");
}

TEST(ChatDecoderST, BothBits) {
    std::vector<uint8_t> bits = {0x03};
    auto perms = ChatDecoder::decodeStPermissions(bits);
    EXPECT_EQ(perms.size(), 2u);
}

// =============================================================================
// decodePermissions dispatch
// =============================================================================

TEST(ChatDecoderDispatch, IS_Role_DispatchesToIsPermissions) {
    std::vector<uint8_t> bits = {0x01};
    auto perms = ChatDecoder::decodePermissions(ChatRole::IS, bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Read DG3 (Fingerprint)");
}

TEST(ChatDecoderDispatch, AT_Role_DispatchesToAtPermissions) {
    std::vector<uint8_t> bits = {0x00, 0x00, 0x00, 0x00, 0x01};
    auto perms = ChatDecoder::decodePermissions(ChatRole::AT, bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Age Verification");
}

TEST(ChatDecoderDispatch, ST_Role_DispatchesToStPermissions) {
    std::vector<uint8_t> bits = {0x01};
    auto perms = ChatDecoder::decodePermissions(ChatRole::ST, bits);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0], "Generate Electronic Signature");
}

TEST(ChatDecoderDispatch, Unknown_Role_ReturnsEmpty) {
    std::vector<uint8_t> bits = {0xFF};
    auto perms = ChatDecoder::decodePermissions(ChatRole::UNKNOWN, bits);
    EXPECT_TRUE(perms.empty());
}

// =============================================================================
// Integration: parsed CHAT from real BSI certs
// =============================================================================

TEST(ChatDecoderIntegration, EcdhCvca_ChatRole_IS_With0xC3) {
    // ECDH CVCA CHAT role OID = ROLE_IS, auth = 0xC3
    auto data = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_CVCA_HEX);
    auto cert = CvcParser::parse(data);
    ASSERT_TRUE(cert.has_value());

    EXPECT_EQ(cert->chat.roleOid, std::string(oid::ROLE_IS));
    EXPECT_EQ(cert->chat.role, ChatRole::IS);

    // 0xC3 = bits 0,1,6,7 → 4 permissions
    EXPECT_EQ(cert->chat.permissions.size(), 4u);
}

TEST(ChatDecoderIntegration, EcdhIs_ChatRole_AT) {
    auto data = cvc_test_helpers::fromHex(cvc_test_helpers::ECDH_IS_HEX);
    auto cert = CvcParser::parse(data);
    ASSERT_TRUE(cert.has_value());

    EXPECT_EQ(cert->chat.roleOid, std::string(oid::ROLE_AT));
    EXPECT_EQ(cert->chat.role, ChatRole::AT);
    EXPECT_FALSE(cert->chat.permissions.empty());
}

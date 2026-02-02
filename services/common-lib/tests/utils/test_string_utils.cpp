/**
 * @file test_string_utils.cpp
 * @brief Unit tests for string utility functions
 */

#include <gtest/gtest.h>
#include <icao/utils/string_utils.h>

using namespace icao::utils;

class StringUtilsTest : public ::testing::Test {
protected:
    // Test setup if needed
};

// toLower tests
TEST_F(StringUtilsTest, ToLower_AllUppercase) {
    EXPECT_EQ(toLower("HELLO"), "hello");
}

TEST_F(StringUtilsTest, ToLower_Mixed) {
    EXPECT_EQ(toLower("HeLLo WoRLd"), "hello world");
}

TEST_F(StringUtilsTest, ToLower_AlreadyLowercase) {
    EXPECT_EQ(toLower("hello"), "hello");
}

TEST_F(StringUtilsTest, ToLower_Empty) {
    EXPECT_EQ(toLower(""), "");
}

TEST_F(StringUtilsTest, ToLower_WithNumbers) {
    EXPECT_EQ(toLower("Test123"), "test123");
}

// toUpper tests
TEST_F(StringUtilsTest, ToUpper_AllLowercase) {
    EXPECT_EQ(toUpper("hello"), "HELLO");
}

TEST_F(StringUtilsTest, ToUpper_Mixed) {
    EXPECT_EQ(toUpper("HeLLo WoRLd"), "HELLO WORLD");
}

TEST_F(StringUtilsTest, ToUpper_AlreadyUppercase) {
    EXPECT_EQ(toUpper("HELLO"), "HELLO");
}

TEST_F(StringUtilsTest, ToUpper_Empty) {
    EXPECT_EQ(toUpper(""), "");
}

TEST_F(StringUtilsTest, ToUpper_WithNumbers) {
    EXPECT_EQ(toUpper("test123"), "TEST123");
}

// trim tests
TEST_F(StringUtilsTest, Trim_LeadingSpaces) {
    EXPECT_EQ(trim("   hello"), "hello");
}

TEST_F(StringUtilsTest, Trim_TrailingSpaces) {
    EXPECT_EQ(trim("hello   "), "hello");
}

TEST_F(StringUtilsTest, Trim_BothEnds) {
    EXPECT_EQ(trim("   hello   "), "hello");
}

TEST_F(StringUtilsTest, Trim_NoSpaces) {
    EXPECT_EQ(trim("hello"), "hello");
}

TEST_F(StringUtilsTest, Trim_OnlySpaces) {
    EXPECT_EQ(trim("     "), "");
}

TEST_F(StringUtilsTest, Trim_Empty) {
    EXPECT_EQ(trim(""), "");
}

TEST_F(StringUtilsTest, Trim_TabsAndNewlines) {
    EXPECT_EQ(trim("\t\nhello\n\t"), "hello");
}

// split tests
TEST_F(StringUtilsTest, Split_CommaDelimiter) {
    auto result = split("a,b,c", ',');
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
}

TEST_F(StringUtilsTest, Split_NoDelimiter) {
    auto result = split("hello", ',');
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "hello");
}

TEST_F(StringUtilsTest, Split_EmptyString) {
    auto result = split("", ',');
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "");
}

TEST_F(StringUtilsTest, Split_ConsecutiveDelimiters) {
    auto result = split("a,,c", ',');
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "");
    EXPECT_EQ(result[2], "c");
}

TEST_F(StringUtilsTest, Split_TrailingDelimiter) {
    auto result = split("a,b,", ',');
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "");
}

// bytesToHex tests
TEST_F(StringUtilsTest, BytesToHex_Basic) {
    uint8_t bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_EQ(bytesToHex(bytes, 4), "deadbeef");
}

TEST_F(StringUtilsTest, BytesToHex_SingleByte) {
    uint8_t bytes[] = {0xFF};
    EXPECT_EQ(bytesToHex(bytes, 1), "ff");
}

TEST_F(StringUtilsTest, BytesToHex_ZeroByte) {
    uint8_t bytes[] = {0x00};
    EXPECT_EQ(bytesToHex(bytes, 1), "00");
}

TEST_F(StringUtilsTest, BytesToHex_Empty) {
    uint8_t bytes[] = {0x00};
    EXPECT_EQ(bytesToHex(bytes, 0), "");
}

TEST_F(StringUtilsTest, BytesToHex_Null) {
    EXPECT_EQ(bytesToHex(nullptr, 0), "");
}

TEST_F(StringUtilsTest, BytesToHex_AllValues) {
    uint8_t bytes[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    EXPECT_EQ(bytesToHex(bytes, 8), "0123456789abcdef");
}

// hexToBytes tests
TEST_F(StringUtilsTest, HexToBytes_Basic) {
    auto result = hexToBytes("deadbeef");
    ASSERT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], 0xDE);
    EXPECT_EQ(result[1], 0xAD);
    EXPECT_EQ(result[2], 0xBE);
    EXPECT_EQ(result[3], 0xEF);
}

TEST_F(StringUtilsTest, HexToBytes_Uppercase) {
    auto result = hexToBytes("DEADBEEF");
    ASSERT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], 0xDE);
    EXPECT_EQ(result[1], 0xAD);
    EXPECT_EQ(result[2], 0xBE);
    EXPECT_EQ(result[3], 0xEF);
}

TEST_F(StringUtilsTest, HexToBytes_Mixed) {
    auto result = hexToBytes("DeAdBeEf");
    ASSERT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], 0xDE);
    EXPECT_EQ(result[1], 0xAD);
    EXPECT_EQ(result[2], 0xBE);
    EXPECT_EQ(result[3], 0xEF);
}

TEST_F(StringUtilsTest, HexToBytes_Empty) {
    auto result = hexToBytes("");
    EXPECT_EQ(result.size(), 0);
}

TEST_F(StringUtilsTest, HexToBytes_OddLength) {
    EXPECT_THROW(hexToBytes("abc"), std::invalid_argument);
}

TEST_F(StringUtilsTest, HexToBytes_InvalidCharacter) {
    EXPECT_THROW(hexToBytes("abgz"), std::invalid_argument);
}

TEST_F(StringUtilsTest, HexToBytes_AllValues) {
    auto result = hexToBytes("0123456789abcdef");
    ASSERT_EQ(result.size(), 8);
    EXPECT_EQ(result[0], 0x01);
    EXPECT_EQ(result[1], 0x23);
    EXPECT_EQ(result[2], 0x45);
    EXPECT_EQ(result[3], 0x67);
    EXPECT_EQ(result[4], 0x89);
    EXPECT_EQ(result[5], 0xAB);
    EXPECT_EQ(result[6], 0xCD);
    EXPECT_EQ(result[7], 0xEF);
}

// Round-trip test
TEST_F(StringUtilsTest, BytesToHex_HexToBytes_RoundTrip) {
    uint8_t original[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    std::string hex = bytesToHex(original, 8);
    auto result = hexToBytes(hex);
    
    ASSERT_EQ(result.size(), 8);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(result[i], original[i]);
    }
}

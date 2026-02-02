/**
 * @file test_dn_parser.cpp
 * @brief Unit tests for DN parser
 */

#include <gtest/gtest.h>
#include <icao/x509/dn_parser.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

using namespace icao::x509;

class DnParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test X509_NAME
        testName = X509_NAME_new();
        ASSERT_NE(testName, nullptr);

        // Add some components
        X509_NAME_add_entry_by_txt(testName, "C", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("US"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "O", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("Test Org"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "CN", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("Test Name"), -1, -1, 0);
    }

    void TearDown() override {
        if (testName) {
            X509_NAME_free(testName);
        }
    }

    X509_NAME* testName = nullptr;
};

TEST_F(DnParserTest, X509NameToString_RFC2253) {
    auto result = x509NameToString(testName, DnFormat::RFC2253);
    ASSERT_TRUE(result.has_value());

    // RFC2253 format: CN=Test Name,O=Test Org,C=US
    std::string dn = *result;
    EXPECT_TRUE(dn.find("CN=Test Name") != std::string::npos);
    EXPECT_TRUE(dn.find("O=Test Org") != std::string::npos);
    EXPECT_TRUE(dn.find("C=US") != std::string::npos);
}

TEST_F(DnParserTest, X509NameToString_Oneline) {
    auto result = x509NameToString(testName, DnFormat::ONELINE);
    ASSERT_TRUE(result.has_value());

    // Oneline format: C = US, O = Test Org, CN = Test Name (OpenSSL format)
    std::string dn = *result;
    EXPECT_TRUE(dn.find("C = US") != std::string::npos || dn.find("C=US") != std::string::npos);
    EXPECT_TRUE(dn.find("Test Org") != std::string::npos);
    EXPECT_TRUE(dn.find("Test Name") != std::string::npos);
}

TEST_F(DnParserTest, X509NameToString_Null) {
    auto result = x509NameToString(nullptr, DnFormat::RFC2253);
    EXPECT_FALSE(result.has_value());
}

TEST_F(DnParserTest, CompareX509Names_Equal) {
    X509_NAME* name2 = X509_NAME_dup(testName);
    ASSERT_NE(name2, nullptr);

    EXPECT_TRUE(compareX509Names(testName, name2));

    X509_NAME_free(name2);
}

TEST_F(DnParserTest, CompareX509Names_Different) {
    X509_NAME* name2 = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name2, "C", MBSTRING_UTF8,
                               reinterpret_cast<const unsigned char*>("UK"), -1, -1, 0);

    EXPECT_FALSE(compareX509Names(testName, name2));

    X509_NAME_free(name2);
}

TEST_F(DnParserTest, CompareX509Names_Null) {
    EXPECT_FALSE(compareX509Names(nullptr, testName));
    EXPECT_FALSE(compareX509Names(testName, nullptr));
    EXPECT_FALSE(compareX509Names(nullptr, nullptr));
}

TEST_F(DnParserTest, NormalizeDnForComparison_RFC2253) {
    std::string dn = "CN=Test,O=Org,C=US";
    auto result = normalizeDnForComparison(dn);

    ASSERT_TRUE(result.has_value());

    // Should be lowercase and sorted
    std::string normalized = *result;
    EXPECT_TRUE(normalized.find("cn=test") != std::string::npos);
    EXPECT_TRUE(normalized.find("o=org") != std::string::npos);
    EXPECT_TRUE(normalized.find("c=us") != std::string::npos);
}

TEST_F(DnParserTest, NormalizeDnForComparison_Oneline) {
    std::string dn = "/C=US/O=Org/CN=Test";
    auto result = normalizeDnForComparison(dn);

    ASSERT_TRUE(result.has_value());

    // Should be lowercase and sorted (same result as RFC2253)
    std::string normalized = *result;
    EXPECT_TRUE(normalized.find("cn=test") != std::string::npos);
    EXPECT_TRUE(normalized.find("o=org") != std::string::npos);
    EXPECT_TRUE(normalized.find("c=us") != std::string::npos);
}

TEST_F(DnParserTest, NormalizeDnForComparison_FormatIndependent) {
    std::string dn1 = "CN=Test,O=Org,C=US";
    std::string dn2 = "/C=US/O=Org/CN=Test";

    auto norm1 = normalizeDnForComparison(dn1);
    auto norm2 = normalizeDnForComparison(dn2);

    ASSERT_TRUE(norm1.has_value());
    ASSERT_TRUE(norm2.has_value());

    // Both should normalize to the same value
    EXPECT_EQ(*norm1, *norm2);
}

TEST_F(DnParserTest, NormalizeDnForComparison_Empty) {
    auto result = normalizeDnForComparison("");
    EXPECT_FALSE(result.has_value());
}

TEST_F(DnParserTest, ParseDnString_RFC2253) {
    std::string dn = "CN=Test Name,O=Test Org,C=US";
    X509_NAME* parsed = parseDnString(dn);

    ASSERT_NE(parsed, nullptr);
    EXPECT_GT(X509_NAME_entry_count(parsed), 0);

    X509_NAME_free(parsed);
}

TEST_F(DnParserTest, ParseDnString_Oneline) {
    std::string dn = "/C=US/O=Test Org/CN=Test Name";
    X509_NAME* parsed = parseDnString(dn);

    ASSERT_NE(parsed, nullptr);
    EXPECT_GT(X509_NAME_entry_count(parsed), 0);

    X509_NAME_free(parsed);
}

TEST_F(DnParserTest, ParseDnString_Empty) {
    X509_NAME* parsed = parseDnString("");
    EXPECT_EQ(parsed, nullptr);
}

TEST_F(DnParserTest, ParseDnString_WithEscapes) {
    std::string dn = "CN=Test\\, Name,O=Test Org,C=US";
    X509_NAME* parsed = parseDnString(dn);

    ASSERT_NE(parsed, nullptr);
    EXPECT_GT(X509_NAME_entry_count(parsed), 0);

    X509_NAME_free(parsed);
}

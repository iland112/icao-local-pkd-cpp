/**
 * @file test_dn_components.cpp
 * @brief Unit tests for DN components extraction
 */

#include <gtest/gtest.h>
#include <icao/x509/dn_components.h>
#include <openssl/x509.h>

using namespace icao::x509;

class DnComponentsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test X509_NAME with multiple components
        testName = X509_NAME_new();
        ASSERT_NE(testName, nullptr);

        X509_NAME_add_entry_by_txt(testName, "C", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("US"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "ST", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("California"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "L", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("San Francisco"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "O", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("Test Organization"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "OU", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("Engineering"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "CN", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("John Doe"), -1, -1, 0);
        X509_NAME_add_entry_by_txt(testName, "emailAddress", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>("john@example.com"), -1, -1, 0);
    }

    void TearDown() override {
        if (testName) {
            X509_NAME_free(testName);
        }
    }

    X509_NAME* testName = nullptr;
};

TEST_F(DnComponentsTest, ExtractAllComponents) {
    DnComponents components = extractDnComponents(testName);

    EXPECT_TRUE(components.country.has_value());
    EXPECT_EQ(*components.country, "US");

    EXPECT_TRUE(components.stateOrProvince.has_value());
    EXPECT_EQ(*components.stateOrProvince, "California");

    EXPECT_TRUE(components.locality.has_value());
    EXPECT_EQ(*components.locality, "San Francisco");

    EXPECT_TRUE(components.organization.has_value());
    EXPECT_EQ(*components.organization, "Test Organization");

    EXPECT_TRUE(components.organizationalUnit.has_value());
    EXPECT_EQ(*components.organizationalUnit, "Engineering");

    EXPECT_TRUE(components.commonName.has_value());
    EXPECT_EQ(*components.commonName, "John Doe");

    EXPECT_TRUE(components.email.has_value());
    EXPECT_EQ(*components.email, "john@example.com");
}

TEST_F(DnComponentsTest, ExtractFromNull) {
    DnComponents components = extractDnComponents(nullptr);
    EXPECT_TRUE(components.isEmpty());
}

TEST_F(DnComponentsTest, IsEmpty) {
    DnComponents empty;
    EXPECT_TRUE(empty.isEmpty());

    DnComponents notEmpty = extractDnComponents(testName);
    EXPECT_FALSE(notEmpty.isEmpty());
}

TEST_F(DnComponentsTest, ToRfc2253) {
    DnComponents components = extractDnComponents(testName);
    std::string rfc2253 = components.toRfc2253();

    EXPECT_FALSE(rfc2253.empty());
    EXPECT_TRUE(rfc2253.find("CN=John Doe") != std::string::npos);
    EXPECT_TRUE(rfc2253.find("O=Test Organization") != std::string::npos);
    EXPECT_TRUE(rfc2253.find("C=US") != std::string::npos);
}

TEST_F(DnComponentsTest, GetDisplayName_WithCN) {
    DnComponents components = extractDnComponents(testName);
    EXPECT_EQ(components.getDisplayName(), "John Doe");
}

TEST_F(DnComponentsTest, GetDisplayName_NoCN_WithOrg) {
    DnComponents components;
    components.organization = "My Org";
    EXPECT_EQ(components.getDisplayName(), "My Org");
}

TEST_F(DnComponentsTest, GetDisplayName_NoInfo) {
    DnComponents components;
    EXPECT_EQ(components.getDisplayName(), "Unknown");
}

TEST_F(DnComponentsTest, GetDnComponentByNid) {
    auto cn = getDnComponentByNid(testName, NID_commonName);
    ASSERT_TRUE(cn.has_value());
    EXPECT_EQ(*cn, "John Doe");

    auto country = getDnComponentByNid(testName, NID_countryName);
    ASSERT_TRUE(country.has_value());
    EXPECT_EQ(*country, "US");

    // Non-existent component
    auto title = getDnComponentByNid(testName, NID_title);
    EXPECT_FALSE(title.has_value());
}

TEST_F(DnComponentsTest, GetDnComponentAllValues) {
    // Add multiple OU values
    X509_NAME* multiName = X509_NAME_new();
    X509_NAME_add_entry_by_txt(multiName, "OU", MBSTRING_UTF8,
                               reinterpret_cast<const unsigned char*>("Engineering"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(multiName, "OU", MBSTRING_UTF8,
                               reinterpret_cast<const unsigned char*>("Security"), -1, -1, 0);

    auto ous = getDnComponentAllValues(multiName, NID_organizationalUnitName);
    EXPECT_EQ(ous.size(), 2);
    EXPECT_TRUE(std::find(ous.begin(), ous.end(), "Engineering") != ous.end());
    EXPECT_TRUE(std::find(ous.begin(), ous.end(), "Security") != ous.end());

    X509_NAME_free(multiName);
}

TEST_F(DnComponentsTest, GetDnComponentAllValues_Empty) {
    auto titles = getDnComponentAllValues(testName, NID_title);
    EXPECT_TRUE(titles.empty());
}

/**
 * @file ldap_helpers_test.cpp
 * @brief Unit tests for LDAP repository helper functions
 *
 * Tests DN normalization, filter escaping, and LDAP utility functions
 * These tests don't require actual LDAP connection - they test pure logic
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#include <gtest/gtest.h>
#include <string>
#include <algorithm>

// =============================================================================
// Helper Function Declarations (from ldap_certificate_repository.cpp)
// =============================================================================

namespace {

/**
 * @brief Extract attribute value from DN
 * @param dn Distinguished Name
 * @param attribute Attribute name (e.g., "CN", "C", "O")
 * @return Attribute value or empty string
 */
std::string extractDnAttribute(const std::string& dn, const std::string& attribute) {
    size_t pos = dn.find(attribute + "=");
    if (pos == std::string::npos) {
        return "";
    }

    size_t start = pos + attribute.length() + 1;
    size_t end = dn.find(',', start);
    if (end == std::string::npos) {
        end = dn.length();
    }

    return dn.substr(start, end - start);
}

/**
 * @brief Escape special characters in LDAP filter value
 * @param value Unescaped value
 * @return Escaped value safe for LDAP filter
 *
 * Escapes: * ( ) \ NUL according to RFC 4515
 */
std::string escapeLdapFilterValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.length() * 2);

    for (char c : value) {
        switch (c) {
            case '*':
                escaped += "\\2a";
                break;
            case '(':
                escaped += "\\28";
                break;
            case ')':
                escaped += "\\29";
                break;
            case '\\':
                escaped += "\\5c";
                break;
            case '\0':
                escaped += "\\00";
                break;
            default:
                escaped += c;
        }
    }

    return escaped;
}

/**
 * @brief Normalize DN for comparison
 * @param dn Distinguished Name
 * @return Normalized DN (lowercase, no spaces)
 *
 * Used by LdapCrlRepository for accurate DN comparison
 */
std::string normalizeDn(const std::string& dn) {
    std::string normalized = dn;

    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

    // Remove spaces
    normalized.erase(std::remove(normalized.begin(), normalized.end(), ' '), normalized.end());

    return normalized;
}

} // namespace

// =============================================================================
// DN Attribute Extraction Tests
// =============================================================================

class DnAttributeExtractionTest : public ::testing::Test {};

TEST_F(DnAttributeExtractionTest, ExtractCn) {
    std::string dn = "CN=Test Certificate,O=SmartCore,C=KR";
    EXPECT_EQ(extractDnAttribute(dn, "CN"), "Test Certificate");
}

TEST_F(DnAttributeExtractionTest, ExtractCountry) {
    std::string dn = "CN=Test,O=SmartCore,C=KR";
    EXPECT_EQ(extractDnAttribute(dn, "C"), "KR");
}

TEST_F(DnAttributeExtractionTest, ExtractOrganization) {
    std::string dn = "CN=Test,O=SmartCore Inc.,C=KR";
    EXPECT_EQ(extractDnAttribute(dn, "O"), "SmartCore Inc.");
}

TEST_F(DnAttributeExtractionTest, ExtractMissingAttribute) {
    std::string dn = "CN=Test,O=SmartCore,C=KR";
    EXPECT_EQ(extractDnAttribute(dn, "OU"), "");
}

TEST_F(DnAttributeExtractionTest, ExtractLastAttribute) {
    std::string dn = "CN=Test,O=SmartCore,C=KR";
    EXPECT_EQ(extractDnAttribute(dn, "C"), "KR");  // No trailing comma
}

TEST_F(DnAttributeExtractionTest, ExtractWithSpaces) {
    std::string dn = "CN=Test Certificate, O=SmartCore Inc., C=KR";
    EXPECT_EQ(extractDnAttribute(dn, "CN"), "Test Certificate");
}

TEST_F(DnAttributeExtractionTest, ExtractComplexDn) {
    std::string dn = "CN=CSCA-KOREA,OU=Passport,O=Ministry of Foreign Affairs,C=KR";
    EXPECT_EQ(extractDnAttribute(dn, "CN"), "CSCA-KOREA");
    EXPECT_EQ(extractDnAttribute(dn, "OU"), "Passport");
    EXPECT_EQ(extractDnAttribute(dn, "O"), "Ministry of Foreign Affairs");
    EXPECT_EQ(extractDnAttribute(dn, "C"), "KR");
}

// =============================================================================
// LDAP Filter Escaping Tests
// =============================================================================

class LdapFilterEscapingTest : public ::testing::Test {};

TEST_F(LdapFilterEscapingTest, EscapeAsterisk) {
    EXPECT_EQ(escapeLdapFilterValue("test*value"), "test\\2avalue");
}

TEST_F(LdapFilterEscapingTest, EscapeParentheses) {
    EXPECT_EQ(escapeLdapFilterValue("test(value)"), "test\\28value\\29");
}

TEST_F(LdapFilterEscapingTest, EscapeBackslash) {
    EXPECT_EQ(escapeLdapFilterValue("test\\value"), "test\\5cvalue");
}

TEST_F(LdapFilterEscapingTest, EscapeMultipleSpecialChars) {
    EXPECT_EQ(escapeLdapFilterValue("test*()\\"), "test\\2a\\28\\29\\5c");
}

TEST_F(LdapFilterEscapingTest, NoEscapeNeeded) {
    std::string normal = "TestValue123";
    EXPECT_EQ(escapeLdapFilterValue(normal), normal);
}

TEST_F(LdapFilterEscapingTest, EscapeInDnValue) {
    // DN with special characters
    std::string dnValue = "CN=Test*(Corp)";
    std::string escaped = escapeLdapFilterValue(dnValue);
    EXPECT_EQ(escaped, "CN=Test\\2a\\28Corp\\29");
}

TEST_F(LdapFilterEscapingTest, SqlInjectionAttempt) {
    // Even though this is LDAP, test that SQL injection chars are preserved
    std::string injection = "'; DROP TABLE users; --";
    std::string escaped = escapeLdapFilterValue(injection);
    // Only LDAP special chars should be escaped, SQL chars are literal
    EXPECT_NE(escaped.find("DROP"), std::string::npos);
}

// =============================================================================
// DN Normalization Tests
// =============================================================================

class DnNormalizationTest : public ::testing::Test {};

TEST_F(DnNormalizationTest, LowercaseConversion) {
    std::string dn = "CN=TEST,O=SMARTCORE,C=KR";
    EXPECT_EQ(normalizeDn(dn), "cn=test,o=smartcore,c=kr");
}

TEST_F(DnNormalizationTest, SpaceRemoval) {
    std::string dn = "CN=Test, O=SmartCore, C=KR";
    EXPECT_EQ(normalizeDn(dn), "cn=test,o=smartcore,c=kr");
}

TEST_F(DnNormalizationTest, CombinedNormalization) {
    std::string dn1 = "CN=CSCA-KOREA, O=Government, C=KR";
    std::string dn2 = "cn=csca-korea,o=government,c=kr";
    EXPECT_EQ(normalizeDn(dn1), normalizeDn(dn2));
}

TEST_F(DnNormalizationTest, OpenSslSlashFormat) {
    // OpenSSL DN format: /C=KR/O=Government/CN=CSCA
    std::string dn = "/C=KR/O=Government/CN=CSCA";
    std::string normalized = normalizeDn(dn);
    EXPECT_EQ(normalized, "/c=kr/o=government/cn=csca");
}

TEST_F(DnNormalizationTest, Rfc2253CommaFormat) {
    // RFC2253 DN format: CN=CSCA,O=Government,C=KR
    std::string dn = "CN=CSCA,O=Government,C=KR";
    std::string normalized = normalizeDn(dn);
    EXPECT_EQ(normalized, "cn=csca,o=government,c=kr");
}

TEST_F(DnNormalizationTest, CompareSlashAndCommaFormats) {
    // After normalization, these should still be different (format not unified)
    std::string slash = normalizeDn("/C=KR/O=Gov/CN=CSCA");
    std::string comma = normalizeDn("CN=CSCA,O=Gov,C=KR");
    // They won't match because slash vs comma separator
    EXPECT_NE(slash, comma);
}

TEST_F(DnNormalizationTest, MultipleSpaces) {
    std::string dn = "CN=Test  Certificate,  O=SmartCore,  C=KR";
    std::string normalized = normalizeDn(dn);
    EXPECT_EQ(normalized.find("  "), std::string::npos);  // No double spaces
}

// =============================================================================
// Integration Tests
// =============================================================================

class LdapHelpersIntegrationTest : public ::testing::Test {};

TEST_F(LdapHelpersIntegrationTest, ExtractAndEscapeWorkflow) {
    // Workflow: Extract CN from DN, then escape for LDAP filter
    std::string dn = "CN=Test*(Certificate),O=Corp,C=KR";
    std::string cn = extractDnAttribute(dn, "CN");
    EXPECT_EQ(cn, "Test*(Certificate)");

    std::string escaped = escapeLdapFilterValue(cn);
    EXPECT_EQ(escaped, "Test\\2a\\28Certificate\\29");
}

TEST_F(LdapHelpersIntegrationTest, NormalizeAndCompare) {
    // Workflow: Normalize two DNs for comparison
    std::string dn1 = "CN=CSCA-KOREA, O=Ministry of Foreign Affairs, C=KR";
    std::string dn2 = "cn=csca-korea,o=ministry of foreign affairs,c=kr";

    EXPECT_EQ(normalizeDn(dn1), normalizeDn(dn2));
}

TEST_F(LdapHelpersIntegrationTest, BuildFilterWithExtractedCn) {
    // Simulate building LDAP filter with extracted and escaped CN
    std::string subjectDn = "CN=CSCA-TEST,O=Gov,C=KR";
    std::string cn = extractDnAttribute(subjectDn, "CN");
    std::string escapedCn = escapeLdapFilterValue(cn);

    std::string filter = "(&(objectClass=pkdDownload)(cn=*" + escapedCn + "*))";
    EXPECT_EQ(filter, "(&(objectClass=pkdDownload)(cn=*CSCA-TEST*))");
}

TEST_F(LdapHelpersIntegrationTest, HandleSpecialCharsInDnFilter) {
    // DN with special characters in CN
    std::string subjectDn = "CN=Test*(Corp),O=Gov,C=KR";
    std::string cn = extractDnAttribute(subjectDn, "CN");
    std::string escapedCn = escapeLdapFilterValue(cn);

    // Verify proper escaping for LDAP filter
    EXPECT_EQ(escapedCn, "Test\\2a\\28Corp\\29");

    std::string filter = "(cn=*" + escapedCn + "*)";
    EXPECT_EQ(filter, "(cn=*Test\\2a\\28Corp\\29*)");
}

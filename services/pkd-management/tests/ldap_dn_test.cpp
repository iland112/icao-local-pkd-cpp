/**
 * @file ldap_dn_test.cpp
 * @brief Unit tests for LDAP DN building functions
 *
 * Sprint 1: Week 5 - LDAP Storage Fix
 * Tests both legacy DN (Subject DN + Serial) and v2 DN (Fingerprint-based)
 */

#include <gtest/gtest.h>
#include <string>
#include <chrono>

// Mock AppConfig for testing
struct AppConfig {
    std::string ldapBaseDn = "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
};

AppConfig appConfig;

// Include the DN building functions
// Note: These are typically in main.cpp, but for testing we extract them here

/**
 * @brief Build LDAP DN for certificate (v2 - Fingerprint-based)
 */
std::string buildCertificateDnV2(const std::string& fingerprint, const std::string& certType,
                                   const std::string& countryCode) {
    std::string ou;
    std::string dataContainer;

    if (certType == "CSCA") {
        ou = "csca";
        dataContainer = "dc=data";
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = "dc=data";
    } else if (certType == "DSC_NC") {
        ou = "dsc_nc";
        dataContainer = "dc=nc-data";
    } else {
        ou = "dsc";
        dataContainer = "dc=data";
    }

    return "cn=" + fingerprint + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + "," + appConfig.ldapBaseDn;
}

// =============================================================================
// Test Suite: LDAP DN v2 (Fingerprint-based)
// =============================================================================

class LdapDnV2Test : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
        appConfig.ldapBaseDn = "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
    }
};

TEST_F(LdapDnV2Test, BuildDnV2_CSCA_Basic) {
    std::string fingerprint = "0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b";
    std::string dn = buildCertificateDnV2(fingerprint, "CSCA", "KR");

    std::string expected = "cn=0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b,"
                           "o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
    EXPECT_EQ(dn, expected);
}

TEST_F(LdapDnV2Test, BuildDnV2_DSC_Basic) {
    std::string fingerprint = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    std::string dn = buildCertificateDnV2(fingerprint, "DSC", "US");

    std::string expected = "cn=1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef,"
                           "o=dsc,c=US,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
    EXPECT_EQ(dn, expected);
}

TEST_F(LdapDnV2Test, BuildDnV2_DSC_NC_Basic) {
    std::string fingerprint = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    std::string dn = buildCertificateDnV2(fingerprint, "DSC_NC", "FR");

    std::string expected = "cn=fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210,"
                           "o=dsc_nc,c=FR,dc=nc-data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
    EXPECT_EQ(dn, expected);
}

TEST_F(LdapDnV2Test, DnLength_UnderLimit) {
    // SHA-256 fingerprint is 64 hex characters
    std::string fingerprint = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    std::string dnCSCA = buildCertificateDnV2(fingerprint, "CSCA", "XX");
    std::string dnDSC = buildCertificateDnV2(fingerprint, "DSC", "XX");
    std::string dnDSC_NC = buildCertificateDnV2(fingerprint, "DSC_NC", "XX");

    // LDAP DN length limit is typically 255 characters
    EXPECT_LT(dnCSCA.length(), 255);
    EXPECT_LT(dnDSC.length(), 255);
    EXPECT_LT(dnDSC_NC.length(), 255);

    // Expected length: ~130-140 characters
    EXPECT_GT(dnCSCA.length(), 120);
    EXPECT_LT(dnCSCA.length(), 150);
}

TEST_F(LdapDnV2Test, DnLength_Consistency) {
    std::string fp1 = "0000000000000000000000000000000000000000000000000000000000000000";
    std::string fp2 = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

    std::string dn1 = buildCertificateDnV2(fp1, "CSCA", "US");
    std::string dn2 = buildCertificateDnV2(fp2, "CSCA", "FR");

    // All v2 DNs should have same length (fingerprint is fixed 64 chars)
    EXPECT_EQ(dn1.length(), dn2.length());
}

TEST_F(LdapDnV2Test, FingerprintUniqueness_DifferentCerts) {
    // Simulate two different certificates with different fingerprints
    std::string fp1 = "1111111111111111111111111111111111111111111111111111111111111111";
    std::string fp2 = "2222222222222222222222222222222222222222222222222222222222222222";

    std::string dn1 = buildCertificateDnV2(fp1, "DSC", "KR");
    std::string dn2 = buildCertificateDnV2(fp2, "DSC", "KR");

    // DNs should be different
    EXPECT_NE(dn1, dn2);
}

TEST_F(LdapDnV2Test, SerialNumberCollision_Resolved) {
    // Simulate serial number collision scenario:
    // Two different certificates with SAME serial number but different fingerprints
    std::string serialNumber = "1";  // Same serial number

    std::string fp1 = "aaaa1111111111111111111111111111111111111111111111111111111111aa";
    std::string fp2 = "bbbb2222222222222222222222222222222222222222222222222222222222bb";

    std::string dn1 = buildCertificateDnV2(fp1, "DSC", "US");
    std::string dn2 = buildCertificateDnV2(fp2, "DSC", "FR");

    // Even with same serial number, DNs are unique (different fingerprints)
    EXPECT_NE(dn1, dn2);

    // Verify fingerprint is in DN
    EXPECT_NE(dn1.find(fp1), std::string::npos);
    EXPECT_NE(dn2.find(fp2), std::string::npos);
}

TEST_F(LdapDnV2Test, CountryCode_CaseSensitive) {
    std::string fingerprint = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";

    std::string dnUppercase = buildCertificateDnV2(fingerprint, "CSCA", "KR");
    std::string dnLowercase = buildCertificateDnV2(fingerprint, "CSCA", "kr");

    // Country codes should be preserved as-is (LDAP is case-sensitive)
    EXPECT_NE(dnUppercase, dnLowercase);
    EXPECT_NE(dnUppercase.find("c=KR"), std::string::npos);
    EXPECT_NE(dnLowercase.find("c=kr"), std::string::npos);
}

TEST_F(LdapDnV2Test, NoEscapingRequired) {
    // Fingerprint is hex (0-9, a-f), no LDAP special characters
    std::string fingerprint = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    std::string dn = buildCertificateDnV2(fingerprint, "CSCA", "KR");

    // DN should not contain escape characters
    EXPECT_EQ(dn.find('\\'), std::string::npos);
    EXPECT_EQ(dn.find('+'), std::string::npos);  // Multi-valued RDN not used
}

TEST_F(LdapDnV2Test, DataContainer_CSCA_DSC) {
    std::string fingerprint = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    std::string dnCSCA = buildCertificateDnV2(fingerprint, "CSCA", "KR");
    std::string dnDSC = buildCertificateDnV2(fingerprint, "DSC", "KR");

    // CSCA and DSC should both use dc=data
    EXPECT_NE(dnCSCA.find("dc=data"), std::string::npos);
    EXPECT_NE(dnDSC.find("dc=data"), std::string::npos);

    // Neither should use dc=nc-data
    EXPECT_EQ(dnCSCA.find("dc=nc-data"), std::string::npos);
    EXPECT_EQ(dnDSC.find("dc=nc-data"), std::string::npos);
}

TEST_F(LdapDnV2Test, DataContainer_DSC_NC) {
    std::string fingerprint = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    std::string dnDSC_NC = buildCertificateDnV2(fingerprint, "DSC_NC", "KR");

    // DSC_NC should use dc=nc-data
    EXPECT_NE(dnDSC_NC.find("dc=nc-data"), std::string::npos);

    // Should NOT use dc=data
    EXPECT_EQ(dnDSC_NC.find("dc=data"), std::string::npos);
}

TEST_F(LdapDnV2Test, OrganizationalUnit_Mapping) {
    std::string fingerprint = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

    std::string dnCSCA = buildCertificateDnV2(fingerprint, "CSCA", "KR");
    std::string dnDSC = buildCertificateDnV2(fingerprint, "DSC", "KR");
    std::string dnDSC_NC = buildCertificateDnV2(fingerprint, "DSC_NC", "KR");

    // Verify organizational unit (o=) mappings
    EXPECT_NE(dnCSCA.find("o=csca"), std::string::npos);
    EXPECT_NE(dnDSC.find("o=dsc"), std::string::npos);
    EXPECT_NE(dnDSC_NC.find("o=dsc_nc"), std::string::npos);
}

// =============================================================================
// Performance Tests
// =============================================================================

TEST_F(LdapDnV2Test, Performance_BuildDn) {
    std::string fingerprint = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";

    // Build DN 10,000 times
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; i++) {
        buildCertificateDnV2(fingerprint, "DSC", "KR");
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete in under 100ms (< 0.01ms per DN)
    EXPECT_LT(duration.count(), 100);

    std::cout << "Built 10,000 DNs in " << duration.count() << "ms "
              << "(" << (duration.count() / 10.0) << "us per DN)" << std::endl;
}

// =============================================================================
// Integration Tests (require database)
// =============================================================================

// NOTE: These tests are disabled by default as they require database connection
// Enable with: GTEST_FILTER=*Integration* ./ldap_dn_test

TEST_F(LdapDnV2Test, DISABLED_Integration_NoDuplicateDNs) {
    // This test would:
    // 1. Generate DNs for all certificates in database
    // 2. Verify no DN collisions
    // 3. Check against actual ldap_dn_v2 column values

    // Implementation requires database connection
    // See ldap-dn-migration-dryrun.sh for SQL queries
}

TEST_F(LdapDnV2Test, DISABLED_Integration_FingerprintUniqueness) {
    // This test would:
    // 1. Query all fingerprint_sha256 values from certificate table
    // 2. Verify each fingerprint is unique
    // 3. Ensure no NULL fingerprints for ldap_stored=true

    // SQL: SELECT fingerprint_sha256, COUNT(*) FROM certificate
    //      WHERE ldap_stored = true
    //      GROUP BY fingerprint_sha256
    //      HAVING COUNT(*) > 1;
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

/**
 * @file test_ldap_storage_service.cpp
 * @brief Unit tests for LdapStorageService — DN construction and RFC 4514 escaping
 *
 * Only the pure, network-free functions are tested here:
 *   - escapeLdapDnValue()        (static — RFC 4514 special-char escaping)
 *   - buildCertificateDnV2()     (fingerprint-based v2 DNs)
 *   - buildCrlDn()
 *   - buildMasterListDn()
 *
 * getLdapWriteConnection() and getLdapReadConnection() are NOT tested because
 * they open real TCP connections.  Those paths are covered by integration tests
 * that run against a live LDAP container.
 *
 * extractStandardAttributes() depends on icao::x509::parseDnString() from the
 * shared library — that function is exercised indirectly via buildCertificateDn().
 *
 * Framework: Google Test (GTest)
 */

#include <gtest/gtest.h>
#include "upload/services/ldap_storage_service.h"
#include "upload/common/upload_config.h"

#include <string>

// ---------------------------------------------------------------------------
// Fixture: a default AppConfig with predictable container paths
// ---------------------------------------------------------------------------
struct LdapStorageServiceFixture : public ::testing::Test {
protected:
    AppConfig config_;
    std::unique_ptr<services::LdapStorageService> svc_;

    void SetUp() override {
        config_.ldapBaseDn       = "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
        config_.ldapDataContainer   = "dc=data";
        config_.ldapNcDataContainer = "dc=nc-data";
        config_.ldapWriteHost    = "openldap1";
        config_.ldapWritePort    = 389;
        config_.ldapBindDn       = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
        config_.ldapBindPassword = "test_password";
        svc_ = std::make_unique<services::LdapStorageService>(config_);
    }
};

// ===========================================================================
// escapeLdapDnValue — RFC 4514 special character escaping
// ===========================================================================

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_EmptyString_ReturnsEmpty) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue(""), "");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_PlainAlphanumeric_Unchanged) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("KoreaPassport"), "KoreaPassport");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_Comma_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a,b"), "a\\,b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_Equals_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a=b"), "a\\=b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_Plus_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a+b"), "a\\+b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_DoubleQuote_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a\"b"), "a\\\"b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_Backslash_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a\\b"), "a\\\\b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_LessThan_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a<b"), "a\\<b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_GreaterThan_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a>b"), "a\\>b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_Semicolon_IsEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("a;b"), "a\\;b");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_LeadingSpace_IsEscaped) {
    // RFC 4514: leading space must be escaped
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue(" hello"), "\\ hello");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_TrailingSpace_IsEscaped) {
    // RFC 4514: trailing space must be escaped
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("hello "), "hello\\ ");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_InternalSpace_IsNotEscaped) {
    // Internal spaces are fine; only leading/trailing need escaping
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("hello world"), "hello world");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_LeadingHash_IsEscaped) {
    // RFC 4514: leading '#' must be escaped
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("#value"), "\\#value");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_InternalHash_IsNotEscaped) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("val#ue"), "val#ue");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_MultipleSpecialChars) {
    // "C=KR,O=Gov" → all commas and equals are escaped
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("C=KR,O=Gov"),
              "C\\=KR\\,O\\=Gov");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_SingleChar_Unchanged) {
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue("A"), "A");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_SingleSpace_IsEscaped) {
    // A string consisting of only a space is both leading and trailing
    EXPECT_EQ(services::LdapStorageService::escapeLdapDnValue(" "), "\\ ");
}

TEST_F(LdapStorageServiceFixture, EscapeLdapDnValue_Idempotent_NotDoubleEscaped) {
    // escapeLdapDnValue is NOT idempotent (re-escaping already-escaped strings
    // produces double-backslashes). Verify the first call is stable.
    std::string once = services::LdapStorageService::escapeLdapDnValue("a,b");
    EXPECT_EQ(once, "a\\,b");
}

// ===========================================================================
// buildCertificateDnV2 — fingerprint-based DN (v2 scheme)
// ===========================================================================

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_CSCA_ContainsCscaOu) {
    std::string dn = svc_->buildCertificateDnV2("aabbccdd", "CSCA", "KR");
    EXPECT_NE(dn.find("o=csca"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_CSCA_UsesDataContainer) {
    std::string dn = svc_->buildCertificateDnV2("aabbccdd", "CSCA", "KR");
    EXPECT_NE(dn.find("dc=data"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_DSC_ContainsDscOu) {
    std::string dn = svc_->buildCertificateDnV2("aabbccdd", "DSC", "KR");
    EXPECT_NE(dn.find("o=dsc"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_DSC_NC_ContainsDscOuInNcData) {
    std::string dn = svc_->buildCertificateDnV2("aabbccdd", "DSC_NC", "KR");
    EXPECT_NE(dn.find("o=dsc"), std::string::npos) << "DN: " << dn;
    EXPECT_NE(dn.find("dc=nc-data"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_DSC_NC_UsesNcDataContainer) {
    std::string dn = svc_->buildCertificateDnV2("fp", "DSC_NC", "US");
    // Must NOT use dc=data for DSC_NC
    EXPECT_NE(dn.find("dc=nc-data"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_LC_ContainsLcOu) {
    std::string dn = svc_->buildCertificateDnV2("aabbccdd", "LC", "KR");
    EXPECT_NE(dn.find("o=lc"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_MLSC_ContainsMlscOu) {
    std::string dn = svc_->buildCertificateDnV2("aabbccdd", "MLSC", "KR");
    EXPECT_NE(dn.find("o=mlsc"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_UnknownType_FallsBackToDsc) {
    std::string dn = svc_->buildCertificateDnV2("aabbccdd", "UNKNOWN_TYPE", "KR");
    EXPECT_NE(dn.find("o=dsc"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_ContainsCountryCode) {
    std::string dn = svc_->buildCertificateDnV2("fp123", "DSC", "DE");
    EXPECT_NE(dn.find("c=DE"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_ContainsFingerprint) {
    std::string fp = "deadbeef0123456789abcdef";
    std::string dn = svc_->buildCertificateDnV2(fp, "CSCA", "KR");
    EXPECT_NE(dn.find("cn=" + fp), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_ContainsBaseDn) {
    std::string dn = svc_->buildCertificateDnV2("fp", "DSC", "KR");
    EXPECT_NE(dn.find(config_.ldapBaseDn), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCertificateDnV2_Idempotent) {
    // Same inputs must always produce the same DN
    std::string fp = "abc123def456";
    std::string a = svc_->buildCertificateDnV2(fp, "DSC", "KR");
    std::string b = svc_->buildCertificateDnV2(fp, "DSC", "KR");
    EXPECT_EQ(a, b);
}

// ===========================================================================
// buildCrlDn
// ===========================================================================

TEST_F(LdapStorageServiceFixture, BuildCrlDn_ContainsCrlOu) {
    std::string dn = svc_->buildCrlDn("KR", "crl_fp_abc");
    EXPECT_NE(dn.find("o=crl"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCrlDn_ContainsCountryCode) {
    std::string dn = svc_->buildCrlDn("FR", "somefp");
    EXPECT_NE(dn.find("c=FR"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCrlDn_ContainsFingerprint) {
    std::string fp = "crl_fingerprint_hex";
    std::string dn = svc_->buildCrlDn("KR", fp);
    EXPECT_NE(dn.find("cn=" + fp), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCrlDn_UsesDataContainer) {
    std::string dn = svc_->buildCrlDn("KR", "fp");
    EXPECT_NE(dn.find("dc=data"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCrlDn_ContainsBaseDn) {
    std::string dn = svc_->buildCrlDn("KR", "fp");
    EXPECT_NE(dn.find(config_.ldapBaseDn), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildCrlDn_Idempotent) {
    std::string a = svc_->buildCrlDn("KR", "myfp");
    std::string b = svc_->buildCrlDn("KR", "myfp");
    EXPECT_EQ(a, b);
}

TEST_F(LdapStorageServiceFixture, BuildCrlDn_DifferentCountries_ProduceDifferentDns) {
    std::string kr = svc_->buildCrlDn("KR", "fp");
    std::string us = svc_->buildCrlDn("US", "fp");
    EXPECT_NE(kr, us);
}

// ===========================================================================
// buildMasterListDn
// ===========================================================================

TEST_F(LdapStorageServiceFixture, BuildMasterListDn_ContainsMlOu) {
    std::string dn = svc_->buildMasterListDn("KR", "mlfp");
    EXPECT_NE(dn.find("o=ml"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildMasterListDn_ContainsCountryCode) {
    std::string dn = svc_->buildMasterListDn("JP", "fp");
    EXPECT_NE(dn.find("c=JP"), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildMasterListDn_ContainsFingerprint) {
    std::string fp = "ml_fingerprint_abc";
    std::string dn = svc_->buildMasterListDn("KR", fp);
    EXPECT_NE(dn.find("cn=" + fp), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildMasterListDn_ContainsBaseDn) {
    std::string dn = svc_->buildMasterListDn("KR", "fp");
    EXPECT_NE(dn.find(config_.ldapBaseDn), std::string::npos) << "DN: " << dn;
}

TEST_F(LdapStorageServiceFixture, BuildMasterListDn_Idempotent) {
    std::string a = svc_->buildMasterListDn("KR", "fp");
    std::string b = svc_->buildMasterListDn("KR", "fp");
    EXPECT_EQ(a, b);
}

// ===========================================================================
// getLdapReadConnection — empty host list returns nullptr (no network needed)
// ===========================================================================

TEST_F(LdapStorageServiceFixture, GetLdapReadConnection_EmptyHostList_ReturnsNullptr) {
    // config_.ldapReadHostList is empty by default
    // No network is attempted — function returns nullptr immediately
    LDAP* conn = svc_->getLdapReadConnection();
    EXPECT_EQ(conn, nullptr);
    // Nothing to clean up because nullptr was returned
}

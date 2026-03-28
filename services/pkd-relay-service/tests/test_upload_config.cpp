/**
 * @file test_upload_config.cpp
 * @brief Unit tests for AppConfig (upload_config.h)
 *
 * AppConfig is a plain struct with a loadFromEnv() method.  All functions are
 * pure C++ with no external dependencies — no DB, no LDAP, no OpenSSL.
 *
 * Tested:
 *   - Default field values
 *   - loadFromEnv() reads each recognised environment variable
 *   - loadFromEnv() silently ignores unparseable numeric values (uses default)
 *   - loadFromEnv() is idempotent (calling twice with same env produces same result)
 *   - Multiple env overrides in a single call
 *
 * Framework: Google Test (GTest)
 */

#include <gtest/gtest.h>
#include "upload/common/upload_config.h"

#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// Helper: RAII environment variable setter
// ---------------------------------------------------------------------------
struct EnvGuard {
    std::string name_;
    bool had_previous_;
    std::string previous_value_;

    explicit EnvGuard(const std::string& name, const std::string& value)
        : name_(name), had_previous_(false)
    {
        if (const char* prev = std::getenv(name.c_str())) {
            had_previous_ = true;
            previous_value_ = prev;
        }
        ::setenv(name.c_str(), value.c_str(), /*overwrite=*/1);
    }

    ~EnvGuard() {
        if (had_previous_) {
            ::setenv(name_.c_str(), previous_value_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }
};

// ---------------------------------------------------------------------------
// Default Values
// ---------------------------------------------------------------------------

TEST(AppConfigDefaults, LdapWriteHost_IsOpenldap1) {
    AppConfig cfg;
    EXPECT_EQ(cfg.ldapWriteHost, "openldap1");
}

TEST(AppConfigDefaults, LdapWritePort_Is389) {
    AppConfig cfg;
    EXPECT_EQ(cfg.ldapWritePort, 389);
}

TEST(AppConfigDefaults, LdapBindDn_HasExpectedValue) {
    AppConfig cfg;
    EXPECT_EQ(cfg.ldapBindDn, "cn=admin,dc=ldap,dc=smartcoreinc,dc=com");
}

TEST(AppConfigDefaults, LdapBindPassword_IsEmpty) {
    AppConfig cfg;
    EXPECT_TRUE(cfg.ldapBindPassword.empty());
}

TEST(AppConfigDefaults, LdapBaseDn_HasExpectedValue) {
    AppConfig cfg;
    EXPECT_EQ(cfg.ldapBaseDn, "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com");
}

TEST(AppConfigDefaults, LdapReadHostList_IsEmpty) {
    AppConfig cfg;
    EXPECT_TRUE(cfg.ldapReadHostList.empty());
}

TEST(AppConfigDefaults, LdapDataContainer_IsDcData) {
    AppConfig cfg;
    EXPECT_EQ(cfg.ldapDataContainer, "dc=data");
}

TEST(AppConfigDefaults, LdapNcDataContainer_IsDcNcData) {
    AppConfig cfg;
    EXPECT_EQ(cfg.ldapNcDataContainer, "dc=nc-data");
}

TEST(AppConfigDefaults, TrustAnchorPath_IsDefaultPemPath) {
    AppConfig cfg;
    EXPECT_EQ(cfg.trustAnchorPath, "/app/data/cert/UN_CSCA_2.pem");
}

TEST(AppConfigDefaults, Asn1MaxLines_Is100) {
    AppConfig cfg;
    EXPECT_EQ(cfg.asn1MaxLines, 100);
}

// ---------------------------------------------------------------------------
// loadFromEnv — individual variable overrides
// ---------------------------------------------------------------------------

TEST(AppConfigLoadFromEnv, OverrideTrustAnchorPath) {
    EnvGuard g("TRUST_ANCHOR_PATH", "/custom/path/trust.pem");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.trustAnchorPath, "/custom/path/trust.pem");
}

TEST(AppConfigLoadFromEnv, OverrideAsn1MaxLines_ValidNumber) {
    EnvGuard g("ASN1_MAX_LINES", "500");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.asn1MaxLines, 500);
}

TEST(AppConfigLoadFromEnv, OverrideAsn1MaxLines_InvalidString_KeepsDefault) {
    EnvGuard g("ASN1_MAX_LINES", "not_a_number");
    AppConfig cfg;
    cfg.loadFromEnv();
    // Default must be preserved when env value is unparseable
    EXPECT_EQ(cfg.asn1MaxLines, 100);
}

TEST(AppConfigLoadFromEnv, OverrideAsn1MaxLines_EmptyString_KeepsDefault) {
    // Some setenv("VAR", "") implementations set an empty string
    EnvGuard g("ASN1_MAX_LINES", "");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.asn1MaxLines, 100);
}

TEST(AppConfigLoadFromEnv, OverrideLdapWriteHost) {
    EnvGuard g("LDAP_WRITE_HOST", "my-ldap-master.internal");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapWriteHost, "my-ldap-master.internal");
}

TEST(AppConfigLoadFromEnv, OverrideLdapWritePort_ValidNumber) {
    EnvGuard g("LDAP_WRITE_PORT", "636");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapWritePort, 636);
}

TEST(AppConfigLoadFromEnv, OverrideLdapWritePort_InvalidString_KeepsDefault) {
    EnvGuard g("LDAP_WRITE_PORT", "abc");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapWritePort, 389);
}

TEST(AppConfigLoadFromEnv, OverrideLdapWritePort_NegativeValue) {
    // Negative port is rejected by validation — default is preserved
    EnvGuard g("LDAP_WRITE_PORT", "-1");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapWritePort, 389);  // Default preserved (port range: 1-65535)
}

TEST(AppConfigLoadFromEnv, OverrideLdapBindDn) {
    EnvGuard g("LDAP_BIND_DN", "cn=svc,dc=example,dc=com");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapBindDn, "cn=svc,dc=example,dc=com");
}

TEST(AppConfigLoadFromEnv, OverrideLdapBindPassword) {
    EnvGuard g("LDAP_BIND_PASSWORD", "s3cr3t!");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapBindPassword, "s3cr3t!");
}

TEST(AppConfigLoadFromEnv, OverrideLdapBaseDn) {
    EnvGuard g("LDAP_BASE_DN", "dc=custom,dc=base,dc=com");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapBaseDn, "dc=custom,dc=base,dc=com");
}

TEST(AppConfigLoadFromEnv, OverrideLdapDataContainer) {
    EnvGuard g("LDAP_DATA_CONTAINER", "dc=pkd-data");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapDataContainer, "dc=pkd-data");
}

TEST(AppConfigLoadFromEnv, OverrideLdapNcDataContainer) {
    EnvGuard g("LDAP_NC_DATA_CONTAINER", "dc=nc");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapNcDataContainer, "dc=nc");
}

// ---------------------------------------------------------------------------
// loadFromEnv — multiple overrides in one call
// ---------------------------------------------------------------------------

TEST(AppConfigLoadFromEnv, MultipleOverrides_AllApplied) {
    EnvGuard g1("LDAP_WRITE_HOST", "primary.ldap");
    EnvGuard g2("LDAP_WRITE_PORT", "1389");
    EnvGuard g3("LDAP_BIND_PASSWORD", "pass123");
    EnvGuard g4("ASN1_MAX_LINES", "200");

    AppConfig cfg;
    cfg.loadFromEnv();

    EXPECT_EQ(cfg.ldapWriteHost, "primary.ldap");
    EXPECT_EQ(cfg.ldapWritePort, 1389);
    EXPECT_EQ(cfg.ldapBindPassword, "pass123");
    EXPECT_EQ(cfg.asn1MaxLines, 200);
}

// ---------------------------------------------------------------------------
// Idempotency
// ---------------------------------------------------------------------------

TEST(AppConfigLoadFromEnv, CalledTwice_SameResult) {
    EnvGuard g("LDAP_WRITE_HOST", "stable.host");
    AppConfig cfg;
    cfg.loadFromEnv();
    std::string firstResult = cfg.ldapWriteHost;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapWriteHost, firstResult);
}

TEST(AppConfigLoadFromEnv, CalledTwice_AsN1Lines_SameResult) {
    EnvGuard g("ASN1_MAX_LINES", "300");
    AppConfig cfg;
    cfg.loadFromEnv();
    int first = cfg.asn1MaxLines;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.asn1MaxLines, first);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(AppConfigDefaults, TwoIndependentInstances_DoNotShareState) {
    AppConfig a;
    AppConfig b;
    a.ldapWriteHost = "hostA";
    b.ldapWriteHost = "hostB";
    EXPECT_EQ(a.ldapWriteHost, "hostA");
    EXPECT_EQ(b.ldapWriteHost, "hostB");
}

TEST(AppConfigLoadFromEnv, LdapBindPassword_UnicodeCharacters) {
    // Passwords may contain non-ASCII characters (e.g., Korean input)
    EnvGuard g("LDAP_BIND_PASSWORD", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4");  // "한국어" in UTF-8
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_FALSE(cfg.ldapBindPassword.empty());
}

TEST(AppConfigLoadFromEnv, LdapBindPassword_SpecialShellCharacters) {
    EnvGuard g("LDAP_BIND_PASSWORD", "p@ss!#$%^&*()");
    AppConfig cfg;
    cfg.loadFromEnv();
    EXPECT_EQ(cfg.ldapBindPassword, "p@ss!#$%^&*()");
}

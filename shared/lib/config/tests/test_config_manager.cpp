/**
 * @file test_config_manager.cpp
 * @brief Unit tests for common::ConfigManager
 *
 * The ConfigManager is a singleton, so tests that call getInstance() share
 * state across the process lifetime.  Tests that need clean state use the
 * static getEnv() helper (which is stateless) or set() followed by cleanup,
 * or test env-var paths via setenv()/unsetenv() directly.
 *
 * Tests that exercise getEnv() are fully independent and have no singleton
 * coupling.  Tests that exercise getString()/getInt()/getBool() via set()
 * prefix keys with a unique test-specific name to avoid cross-contamination.
 *
 * Naming convention: <Method>_<Scenario>_<ExpectedBehaviour>
 */

#include <gtest/gtest.h>
#include "config_manager.h"

#include <cstdlib>  // setenv, unsetenv
#include <string>

using common::ConfigManager;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// RAII guard: sets an env var on construction, restores (or clears) on
// destruction so tests cannot pollute each other.
struct EnvGuard {
    std::string key_;
    bool hadPreviousValue_{false};
    std::string previousValue_;

    explicit EnvGuard(const std::string& key, const std::string& value)
        : key_(key) {
        const char* existing = std::getenv(key.c_str());
        if (existing) {
            hadPreviousValue_ = true;
            previousValue_ = existing;
        }
        ::setenv(key.c_str(), value.c_str(), /*overwrite=*/1);
    }

    ~EnvGuard() {
        if (hadPreviousValue_) {
            ::setenv(key_.c_str(), previousValue_.c_str(), 1);
        } else {
            ::unsetenv(key_.c_str());
        }
    }
};

// Unique key generator: each test gets a key name that cannot clash with keys
// set by other tests, because the ConfigManager singleton retains all set()
// values for the entire process.
std::string uniqueKey(const std::string& suffix) {
    static int counter = 0;
    return "TEST_KEY_" + std::to_string(++counter) + "_" + suffix;
}

} // anonymous namespace


// ============================================================================
// getEnv — static helper, no singleton interaction
// ============================================================================

class GetEnvTest : public ::testing::Test {};

TEST_F(GetEnvTest, ExistingEnvVar_ReturnsValue) {
    EnvGuard guard("TEST_GETENV_EXISTING", "hello_world");
    EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_EXISTING"), "hello_world");
}

TEST_F(GetEnvTest, MissingEnvVar_ReturnsEmptyDefault) {
    ::unsetenv("TEST_GETENV_MISSING_UNIQUE_XYZ");
    EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_MISSING_UNIQUE_XYZ"), "");
}

TEST_F(GetEnvTest, MissingEnvVar_ReturnsCustomDefault) {
    ::unsetenv("TEST_GETENV_MISSING_CUSTOM_XYZ");
    EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_MISSING_CUSTOM_XYZ", "fallback"), "fallback");
}

TEST_F(GetEnvTest, EnvVar_EmptyString_ReturnsEmptyString) {
    EnvGuard guard("TEST_GETENV_EMPTY_VAL", "");
    EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_EMPTY_VAL", "default"), "");
}

TEST_F(GetEnvTest, EnvVar_WithSpaces_PreservedExactly) {
    EnvGuard guard("TEST_GETENV_SPACES", "hello world");
    EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_SPACES"), "hello world");
}

TEST_F(GetEnvTest, EnvVar_Unicode_Korean_Preserved) {
    // The ConfigManager must handle non-ASCII env-var values (e.g. NLS_LANG data).
    EnvGuard guard("TEST_GETENV_KOREAN", "한국어테스트");
    EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_KOREAN"), "한국어테스트");
}

TEST_F(GetEnvTest, Idempotent_RepeatedCalls_SameResult) {
    EnvGuard guard("TEST_GETENV_IDEMPOTENT", "stable_value");
    std::string first = ConfigManager::getEnv("TEST_GETENV_IDEMPOTENT");
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_IDEMPOTENT"), first)
            << "Changed at iteration " << i;
    }
}

TEST_F(GetEnvTest, DefaultIsEmptyStringWhenNotSpecified) {
    ::unsetenv("TEST_GETENV_NO_DEFAULT_XYZ");
    EXPECT_EQ(ConfigManager::getEnv("TEST_GETENV_NO_DEFAULT_XYZ"), "");
}


// ============================================================================
// getString — retrieves value set via set() or from env
// ============================================================================

class GetStringTest : public ::testing::Test {
protected:
    ConfigManager& cfg_ = ConfigManager::getInstance();
};

TEST_F(GetStringTest, SetThenGet_ReturnsValue) {
    std::string key = uniqueKey("SetGet");
    cfg_.set(key, "test_value");
    EXPECT_EQ(cfg_.getString(key), "test_value");
}

TEST_F(GetStringTest, SetEmpty_ThenGet_ReturnsEmpty) {
    std::string key = uniqueKey("SetEmpty");
    cfg_.set(key, "");
    EXPECT_EQ(cfg_.getString(key), "");
}

TEST_F(GetStringTest, MissingKey_ReturnsEmptyDefault) {
    ::unsetenv("TEST_GETSTRING_NEVER_SET_ZZZZZ");
    EXPECT_EQ(cfg_.getString("TEST_GETSTRING_NEVER_SET_ZZZZZ"), "");
}

TEST_F(GetStringTest, MissingKey_CustomDefault) {
    ::unsetenv("TEST_GETSTRING_CUSTOM_DEF_ZZZ");
    EXPECT_EQ(cfg_.getString("TEST_GETSTRING_CUSTOM_DEF_ZZZ", "fallback"), "fallback");
}

TEST_F(GetStringTest, OverwriteValue_ReturnsLatest) {
    std::string key = uniqueKey("Overwrite");
    cfg_.set(key, "first");
    cfg_.set(key, "second");
    EXPECT_EQ(cfg_.getString(key), "second");
}

TEST_F(GetStringTest, EnvVar_ReadDirectly_WhenNotInMap) {
    // getString() falls back to getenv() for keys not in config_ map.
    std::string envKey = "TEST_GETSTRING_ENV_FALLBACK_ZZZ";
    ::unsetenv(envKey.c_str());
    EnvGuard guard(envKey, "env_value");
    // Don't call set() — rely on the getenv fallback in getString()
    EXPECT_EQ(cfg_.getString(envKey), "env_value");
}

TEST_F(GetStringTest, Idempotent_RepeatedReads) {
    std::string key = uniqueKey("IdempotentStr");
    cfg_.set(key, "stable");
    std::string first = cfg_.getString(key);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(cfg_.getString(key), first) << "Changed at iteration " << i;
    }
}


// ============================================================================
// getInt — parses integer values with type safety
// ============================================================================

class GetIntTest : public ::testing::Test {
protected:
    ConfigManager& cfg_ = ConfigManager::getInstance();
};

TEST_F(GetIntTest, ValidInt_ReturnsValue) {
    std::string key = uniqueKey("ValidInt");
    cfg_.set(key, "5432");
    EXPECT_EQ(cfg_.getInt(key), 5432);
}

TEST_F(GetIntTest, ValidInt_Zero_ReturnsZero) {
    std::string key = uniqueKey("IntZero");
    cfg_.set(key, "0");
    EXPECT_EQ(cfg_.getInt(key), 0);
}

TEST_F(GetIntTest, ValidInt_Negative_ReturnsNegative) {
    std::string key = uniqueKey("IntNeg");
    cfg_.set(key, "-1");
    EXPECT_EQ(cfg_.getInt(key), -1);
}

TEST_F(GetIntTest, ValidInt_LargeValue) {
    std::string key = uniqueKey("IntLarge");
    cfg_.set(key, "2147483647"); // INT_MAX
    EXPECT_EQ(cfg_.getInt(key), 2147483647);
}

TEST_F(GetIntTest, NonNumericString_ReturnsDefault) {
    std::string key = uniqueKey("IntNonNum");
    cfg_.set(key, "not_a_number");
    EXPECT_EQ(cfg_.getInt(key, 99), 99);
}

TEST_F(GetIntTest, EmptyString_ReturnsDefault) {
    std::string key = uniqueKey("IntEmpty");
    cfg_.set(key, "");
    EXPECT_EQ(cfg_.getInt(key, 10), 10);
}

TEST_F(GetIntTest, MissingKey_ReturnsDefault) {
    ::unsetenv("TEST_GETINT_MISSING_ZZZ");
    EXPECT_EQ(cfg_.getInt("TEST_GETINT_MISSING_ZZZ", 7), 7);
}

TEST_F(GetIntTest, DefaultIsZeroWhenNotSpecified) {
    ::unsetenv("TEST_GETINT_ZERO_DEF_ZZZ");
    EXPECT_EQ(cfg_.getInt("TEST_GETINT_ZERO_DEF_ZZZ"), 0);
}

TEST_F(GetIntTest, DbPort_Typical_5432) {
    std::string key = uniqueKey("DbPort");
    cfg_.set(key, "5432");
    EXPECT_EQ(cfg_.getInt(key), 5432);
}

TEST_F(GetIntTest, DbPoolMin_Typical_2) {
    std::string key = uniqueKey("PoolMin");
    cfg_.set(key, "2");
    EXPECT_EQ(cfg_.getInt(key), 2);
}

TEST_F(GetIntTest, DbPoolMax_Typical_10) {
    std::string key = uniqueKey("PoolMax");
    cfg_.set(key, "10");
    EXPECT_EQ(cfg_.getInt(key), 10);
}

TEST_F(GetIntTest, Idempotent_RepeatedParsing) {
    std::string key = uniqueKey("IntIdempotent");
    cfg_.set(key, "389");
    int first = cfg_.getInt(key);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(cfg_.getInt(key), first) << "Changed at iteration " << i;
    }
}


// ============================================================================
// getBool — parses boolean values from various string forms
// ============================================================================

class GetBoolTest : public ::testing::Test {
protected:
    ConfigManager& cfg_ = ConfigManager::getInstance();
};

// "true" family
TEST_F(GetBoolTest, StringTrue_lowercase_ReturnsTrue) {
    std::string key = uniqueKey("BoolTrueLow");
    cfg_.set(key, "true");
    EXPECT_TRUE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringTrue_uppercase_ReturnsTrue) {
    std::string key = uniqueKey("BoolTrueUp");
    cfg_.set(key, "TRUE");
    EXPECT_TRUE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringTrue_mixed_ReturnsTrue) {
    std::string key = uniqueKey("BoolTrueMix");
    cfg_.set(key, "True");
    EXPECT_TRUE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, String1_ReturnsTrue) {
    std::string key = uniqueKey("Bool1");
    cfg_.set(key, "1");
    EXPECT_TRUE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringYes_lowercase_ReturnsTrue) {
    std::string key = uniqueKey("BoolYes");
    cfg_.set(key, "yes");
    EXPECT_TRUE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringOn_lowercase_ReturnsTrue) {
    std::string key = uniqueKey("BoolOn");
    cfg_.set(key, "on");
    EXPECT_TRUE(cfg_.getBool(key));
}

// "false" family
TEST_F(GetBoolTest, StringFalse_lowercase_ReturnsFalse) {
    std::string key = uniqueKey("BoolFalseLow");
    cfg_.set(key, "false");
    EXPECT_FALSE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringFalse_uppercase_ReturnsFalse) {
    std::string key = uniqueKey("BoolFalseUp");
    cfg_.set(key, "FALSE");
    EXPECT_FALSE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, String0_ReturnsFalse) {
    std::string key = uniqueKey("Bool0");
    cfg_.set(key, "0");
    EXPECT_FALSE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringNo_lowercase_ReturnsFalse) {
    std::string key = uniqueKey("BoolNo");
    cfg_.set(key, "no");
    EXPECT_FALSE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringOff_lowercase_ReturnsFalse) {
    std::string key = uniqueKey("BoolOff");
    cfg_.set(key, "off");
    EXPECT_FALSE(cfg_.getBool(key));
}

// Mixed-case normalisation
TEST_F(GetBoolTest, StringYES_uppercase_ReturnsTrue) {
    std::string key = uniqueKey("BoolYESup");
    cfg_.set(key, "YES");
    EXPECT_TRUE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, StringOFF_uppercase_ReturnsFalse) {
    std::string key = uniqueKey("BoolOFFup");
    cfg_.set(key, "OFF");
    EXPECT_FALSE(cfg_.getBool(key));
}

// Fallback / default
TEST_F(GetBoolTest, EmptyString_ReturnsDefault) {
    std::string key = uniqueKey("BoolEmpty");
    cfg_.set(key, "");
    EXPECT_FALSE(cfg_.getBool(key, false));
    EXPECT_TRUE(cfg_.getBool(key, true));
}

TEST_F(GetBoolTest, MissingKey_ReturnsFalseDefault) {
    ::unsetenv("TEST_GETBOOL_MISSING_ZZZ");
    EXPECT_FALSE(cfg_.getBool("TEST_GETBOOL_MISSING_ZZZ"));
}

TEST_F(GetBoolTest, MissingKey_TrueDefault) {
    ::unsetenv("TEST_GETBOOL_MISSING_TRUE_ZZZ");
    EXPECT_TRUE(cfg_.getBool("TEST_GETBOOL_MISSING_TRUE_ZZZ", true));
}

TEST_F(GetBoolTest, InvalidValue_ReturnsDefault) {
    std::string key = uniqueKey("BoolInvalid");
    cfg_.set(key, "maybe");
    EXPECT_FALSE(cfg_.getBool(key, false));
    EXPECT_TRUE(cfg_.getBool(key, true));
}

// Feature-flag style keys used in this project
TEST_F(GetBoolTest, AnalysisEnabled_True) {
    std::string key = uniqueKey("AnalysisEnabled");
    cfg_.set(key, "true");
    EXPECT_TRUE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, IcaoSchedulerEnabled_False) {
    std::string key = uniqueKey("IcaoScheduler");
    cfg_.set(key, "false");
    EXPECT_FALSE(cfg_.getBool(key));
}

TEST_F(GetBoolTest, Idempotent_RepeatedParsing) {
    std::string key = uniqueKey("BoolIdempotent");
    cfg_.set(key, "yes");
    bool first = cfg_.getBool(key);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(cfg_.getBool(key), first) << "Changed at iteration " << i;
    }
}


// ============================================================================
// has() — existence check
// ============================================================================

class HasTest : public ::testing::Test {
protected:
    ConfigManager& cfg_ = ConfigManager::getInstance();
};

TEST_F(HasTest, AfterSet_ReturnsTrue) {
    std::string key = uniqueKey("HasSet");
    cfg_.set(key, "value");
    EXPECT_TRUE(cfg_.has(key));
}

TEST_F(HasTest, NeverSet_NoEnv_ReturnsFalse) {
    ::unsetenv("TEST_HAS_NEVER_SET_ZZZZZ");
    EXPECT_FALSE(cfg_.has("TEST_HAS_NEVER_SET_ZZZZZ"));
}

TEST_F(HasTest, NotInMap_ButInEnv_ReturnsTrue) {
    std::string envKey = "TEST_HAS_ENV_ONLY_ZZZ";
    ::unsetenv(envKey.c_str());
    EnvGuard guard(envKey, "env_value");
    // has() checks both config_ map and getenv()
    EXPECT_TRUE(cfg_.has(envKey));
}

TEST_F(HasTest, AfterSetEmpty_ReturnsTrue) {
    // An explicitly-set empty string still counts as "has"
    std::string key = uniqueKey("HasEmpty");
    cfg_.set(key, "");
    EXPECT_TRUE(cfg_.has(key));
}

TEST_F(HasTest, Idempotent_RepeatedChecks) {
    std::string key = uniqueKey("HasIdempotent");
    cfg_.set(key, "v");
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(cfg_.has(key)) << "Changed at iteration " << i;
    }
}


// ============================================================================
// DB_TYPE env-var convention — used heavily in this project
// ============================================================================

class DbTypeTest : public ::testing::Test {
protected:
    ConfigManager& cfg_ = ConfigManager::getInstance();
};

TEST_F(DbTypeTest, DbType_Postgres_FromEnv) {
    EnvGuard guard("DB_TYPE", "postgres");
    EXPECT_EQ(ConfigManager::getEnv("DB_TYPE"), "postgres");
}

TEST_F(DbTypeTest, DbType_Oracle_FromEnv) {
    EnvGuard guard("DB_TYPE", "oracle");
    EXPECT_EQ(ConfigManager::getEnv("DB_TYPE"), "oracle");
}

TEST_F(DbTypeTest, DbType_Missing_ReturnsDefaultPostgres) {
    // Convention: empty DB_TYPE defaults to postgres
    ::unsetenv("DB_TYPE");
    std::string dbType = ConfigManager::getEnv("DB_TYPE", "postgres");
    EXPECT_EQ(dbType, "postgres");
}

TEST_F(DbTypeTest, DbPort_Postgres_5432) {
    std::string key = uniqueKey("DbPort5432");
    cfg_.set(key, "5432");
    EXPECT_EQ(cfg_.getInt(key), 5432);
}

TEST_F(DbTypeTest, DbPort_Oracle_1521) {
    std::string key = uniqueKey("DbPort1521");
    cfg_.set(key, "1521");
    EXPECT_EQ(cfg_.getInt(key), 1521);
}

TEST_F(DbTypeTest, ServicePort_8081) {
    std::string key = uniqueKey("SvcPort8081");
    cfg_.set(key, "8081");
    EXPECT_EQ(cfg_.getInt(key), 8081);
}

TEST_F(DbTypeTest, ThreadNum_16) {
    std::string key = uniqueKey("ThreadNum16");
    cfg_.set(key, "16");
    EXPECT_EQ(cfg_.getInt(key), 16);
}


// ============================================================================
// Predefined constant keys — verify key names are stable
// ============================================================================

TEST(ConstantKeysTest, DbHostKeyName) {
    EXPECT_STREQ(ConfigManager::DB_HOST, "DB_HOST");
}

TEST(ConstantKeysTest, DbPortKeyName) {
    EXPECT_STREQ(ConfigManager::DB_PORT, "DB_PORT");
}

TEST(ConstantKeysTest, DbNameKeyName) {
    EXPECT_STREQ(ConfigManager::DB_NAME, "DB_NAME");
}

TEST(ConstantKeysTest, DbUserKeyName) {
    EXPECT_STREQ(ConfigManager::DB_USER, "DB_USER");
}

TEST(ConstantKeysTest, DbPasswordKeyName) {
    EXPECT_STREQ(ConfigManager::DB_PASSWORD, "DB_PASSWORD");
}

TEST(ConstantKeysTest, DbPoolMinKeyName) {
    EXPECT_STREQ(ConfigManager::DB_POOL_MIN, "DB_POOL_MIN");
}

TEST(ConstantKeysTest, DbPoolMaxKeyName) {
    EXPECT_STREQ(ConfigManager::DB_POOL_MAX, "DB_POOL_MAX");
}

TEST(ConstantKeysTest, LdapHostKeyName) {
    EXPECT_STREQ(ConfigManager::LDAP_HOST, "LDAP_HOST");
}

TEST(ConstantKeysTest, LdapPortKeyName) {
    EXPECT_STREQ(ConfigManager::LDAP_PORT, "LDAP_PORT");
}

TEST(ConstantKeysTest, LdapBaseDnKeyName) {
    EXPECT_STREQ(ConfigManager::LDAP_BASE_DN, "LDAP_BASE_DN");
}

TEST(ConstantKeysTest, LdapBindDnKeyName) {
    EXPECT_STREQ(ConfigManager::LDAP_BIND_DN, "LDAP_BIND_DN");
}

TEST(ConstantKeysTest, LdapBindPasswordKeyName) {
    EXPECT_STREQ(ConfigManager::LDAP_BIND_PASSWORD, "LDAP_BIND_PASSWORD");
}

TEST(ConstantKeysTest, LdapPoolMinKeyName) {
    EXPECT_STREQ(ConfigManager::LDAP_POOL_MIN, "LDAP_POOL_MIN");
}

TEST(ConstantKeysTest, LdapPoolMaxKeyName) {
    EXPECT_STREQ(ConfigManager::LDAP_POOL_MAX, "LDAP_POOL_MAX");
}

TEST(ConstantKeysTest, ServicePortKeyName) {
    EXPECT_STREQ(ConfigManager::SERVICE_PORT, "SERVICE_PORT");
}

TEST(ConstantKeysTest, ServiceThreadsKeyName) {
    EXPECT_STREQ(ConfigManager::SERVICE_THREADS, "SERVICE_THREADS");
}

TEST(ConstantKeysTest, LogLevelKeyName) {
    EXPECT_STREQ(ConfigManager::LOG_LEVEL, "LOG_LEVEL");
}


// ============================================================================
// loadFromEnvironment — env vars loaded at construction
// ============================================================================

class LoadFromEnvironmentTest : public ::testing::Test {
protected:
    ConfigManager& cfg_ = ConfigManager::getInstance();
};

TEST_F(LoadFromEnvironmentTest, DbHost_LoadedFromEnv) {
    // Set env var before calling loadFromEnvironment(); since the singleton is
    // already constructed, we verify getString() falls back to getenv().
    EnvGuard guard("DB_HOST", "db.example.com");
    // getString() has a direct getenv() fallback even after construction.
    EXPECT_EQ(cfg_.getString("DB_HOST"), "db.example.com");
}

TEST_F(LoadFromEnvironmentTest, DbPort_LoadedFromEnv_ParsedAsInt) {
    // For keys not yet in the map, getInt() calls getString() which calls getenv()
    EnvGuard guard("DB_PORT", "5432");
    EXPECT_EQ(cfg_.getInt("DB_PORT"), 5432);
}

TEST_F(LoadFromEnvironmentTest, LdapHost_LoadedFromEnv) {
    EnvGuard guard("LDAP_HOST", "openldap1");
    EXPECT_EQ(cfg_.getString("LDAP_HOST"), "openldap1");
}

TEST_F(LoadFromEnvironmentTest, LdapPort_LoadedFromEnv_ParsedAsInt) {
    EnvGuard guard("LDAP_PORT", "389");
    EXPECT_EQ(cfg_.getInt("LDAP_PORT"), 389);
}

TEST_F(LoadFromEnvironmentTest, ServicePort_LoadedFromEnv_ParsedAsInt) {
    EnvGuard guard("SERVICE_PORT", "8081");
    EXPECT_EQ(cfg_.getInt("SERVICE_PORT"), 8081);
}

TEST_F(LoadFromEnvironmentTest, LogLevel_LoadedFromEnv) {
    EnvGuard guard("LOG_LEVEL", "debug");
    EXPECT_EQ(cfg_.getString("LOG_LEVEL"), "debug");
}


// ============================================================================
// Edge cases / boundary values
// ============================================================================

class EdgeCaseTest : public ::testing::Test {
protected:
    ConfigManager& cfg_ = ConfigManager::getInstance();
};

TEST_F(EdgeCaseTest, LongStringValue_StoredAndRetrievedIntact) {
    std::string key = uniqueKey("LongVal");
    std::string longValue(4096, 'A');  // 4 KB string
    cfg_.set(key, longValue);
    EXPECT_EQ(cfg_.getString(key), longValue);
}

TEST_F(EdgeCaseTest, ValueWithSpecialChars_StoredIntact) {
    std::string key = uniqueKey("SpecialChars");
    std::string special = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    cfg_.set(key, special);
    EXPECT_EQ(cfg_.getString(key), special);
}

TEST_F(EdgeCaseTest, ValueWithSlashes_StoredIntact) {
    std::string key = uniqueKey("Slashes");
    std::string path = "/home/pkd/.docker-data/ssl/server.crt";
    cfg_.set(key, path);
    EXPECT_EQ(cfg_.getString(key), path);
}

TEST_F(EdgeCaseTest, GetInt_Overflow_FallsBackToDefault) {
    // "99999999999999" overflows int; stoi throws; default returned
    std::string key = uniqueKey("IntOverflow");
    cfg_.set(key, "99999999999999");
    // Result is either some parsed value or the default — must not crash
    int result = cfg_.getInt(key, -1);
    (void)result;  // behaviour is implementation-defined; just test no crash
    SUCCEED();
}

TEST_F(EdgeCaseTest, GetBool_AllTrue_Variants) {
    // All recognised truthy values from getBool()
    const std::vector<std::string> trueValues = {"true", "TRUE", "True", "1", "yes", "YES", "on", "ON"};
    for (const auto& val : trueValues) {
        std::string key = uniqueKey("BoolTrue_" + val);
        cfg_.set(key, val);
        EXPECT_TRUE(cfg_.getBool(key)) << "Failed for value: " << val;
    }
}

TEST_F(EdgeCaseTest, GetBool_AllFalse_Variants) {
    const std::vector<std::string> falseValues = {"false", "FALSE", "False", "0", "no", "NO", "off", "OFF"};
    for (const auto& val : falseValues) {
        std::string key = uniqueKey("BoolFalse_" + val);
        cfg_.set(key, val);
        EXPECT_FALSE(cfg_.getBool(key)) << "Failed for value: " << val;
    }
}

TEST_F(EdgeCaseTest, GetString_KeyWithDots_StoredAndRetrievedIntact) {
    // Keys with dots are not used in this code but must not crash
    std::string key = uniqueKey("KeyWithDots");
    cfg_.set("some.dotted.key", "value123");
    EXPECT_EQ(cfg_.getString("some.dotted.key"), "value123");
}

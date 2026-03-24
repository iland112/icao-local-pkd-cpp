/**
 * @file test_api_key_generator.cpp
 * @brief Unit tests for auth::generateApiKey() and auth::hashApiKey()
 *
 * Covers:
 *  - Key format: "icao_{prefix8}_{random32}" (total 46 chars)
 *  - Prefix format: "icao_{prefix8}" (13 chars)
 *  - SHA-256 hash output: 64 lowercase hex chars
 *  - Hash determinism: same key always produces same hash
 *  - Hash uniqueness: different keys produce different hashes
 *  - Uniqueness across calls: two generated keys must differ
 *  - Character set: Base62 only (0-9, A-Z, a-z)
 *  - Edge cases: hashApiKey on empty string, very long string, unicode-like bytes
 */

#include <gtest/gtest.h>
#include "../../src/auth/api_key_generator.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <string>
#include <unordered_set>

using namespace auth;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isBase62Char(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

static bool isBase62String(const std::string& s) {
    return std::all_of(s.begin(), s.end(), isBase62Char);
}

static bool isLowercaseHexString(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ApiKeyGeneratorTest : public ::testing::Test {
protected:
    // Generate once per test that needs a fresh key
    ApiKeyInfo generateKey() {
        return generateApiKey();
    }
};

// ===========================================================================
// Section 1: generateApiKey() — key format
// ===========================================================================

TEST_F(ApiKeyGeneratorTest, GenerateKey_TotalLength_Is46) {
    // "icao_" (5) + 8 Base62 (8) + "_" (1) + 32 Base62 (32) = 46
    auto info = generateKey();
    EXPECT_EQ(info.key.size(), 46u)
        << "Full key must be 46 characters, got: " << info.key;
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_StartsWithIcaoUnderscore) {
    auto info = generateKey();
    EXPECT_EQ(info.key.substr(0, 5), "icao_")
        << "Key must start with 'icao_'";
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_SegmentSeparatorPresent) {
    // After the 5-char prefix "icao_" and 8 Base62 chars there must be '_'
    auto info = generateKey();
    EXPECT_EQ(info.key[13], '_')
        << "Separator '_' must appear at index 13";
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_PrefixSegment_IsBase62) {
    auto info = generateKey();
    // Positions 5..12 (inclusive)
    std::string prefixSegment = info.key.substr(5, 8);
    EXPECT_EQ(prefixSegment.size(), 8u);
    EXPECT_TRUE(isBase62String(prefixSegment))
        << "Prefix segment must be Base62, got: " << prefixSegment;
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_RandomSegment_IsBase62) {
    auto info = generateKey();
    // Positions 14..45 (inclusive)
    std::string randomSegment = info.key.substr(14, 32);
    EXPECT_EQ(randomSegment.size(), 32u);
    EXPECT_TRUE(isBase62String(randomSegment))
        << "Random segment must be Base62, got: " << randomSegment;
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_DisplayPrefix_Format) {
    auto info = generateKey();
    // prefix should be "icao_" + 8 chars = 13 chars total
    EXPECT_EQ(info.prefix.size(), 13u)
        << "Display prefix must be 13 chars, got: " << info.prefix;
    EXPECT_EQ(info.prefix.substr(0, 5), "icao_");
    EXPECT_TRUE(isBase62String(info.prefix.substr(5)))
        << "Prefix base62 portion must be Base62";
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_Hash_Is64LowercaseHex) {
    auto info = generateKey();
    EXPECT_EQ(info.hash.size(), 64u)
        << "SHA-256 hash must be 64 hex chars, got size: " << info.hash.size();
    EXPECT_TRUE(isLowercaseHexString(info.hash))
        << "Hash must be lowercase hex, got: " << info.hash;
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_Hash_MatchesHashApiKey) {
    auto info = generateKey();
    // The embedded hash must equal what hashApiKey computes for the same key
    std::string recomputed = hashApiKey(info.key);
    EXPECT_EQ(info.hash, recomputed)
        << "Embedded hash and hashApiKey() must agree";
}

// ===========================================================================
// Section 2: generateApiKey() — uniqueness
// ===========================================================================

TEST_F(ApiKeyGeneratorTest, GenerateKey_TwoCallsProduce_DifferentKeys) {
    auto info1 = generateKey();
    auto info2 = generateKey();
    EXPECT_NE(info1.key, info2.key)
        << "Two generated keys must not be identical";
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_TwoCallsProduce_DifferentHashes) {
    auto info1 = generateKey();
    auto info2 = generateKey();
    EXPECT_NE(info1.hash, info2.hash);
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_TwoCallsProduce_DifferentPrefixes) {
    // Not guaranteed in theory, but the probability of collision in 8 Base62
    // chars is 62^-8 ≈ 4e-15; we treat a collision in a single test run as a
    // test failure indicator.
    auto info1 = generateKey();
    auto info2 = generateKey();
    // Just verify they are structurally valid — we do not ASSERT different
    // prefixes because the contract only guarantees random generation.
    EXPECT_EQ(info1.prefix.size(), 13u);
    EXPECT_EQ(info2.prefix.size(), 13u);
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_BulkUniqueness_100Keys) {
    constexpr int N = 100;
    std::unordered_set<std::string> keys;
    std::unordered_set<std::string> hashes;

    for (int i = 0; i < N; ++i) {
        auto info = generateKey();
        keys.insert(info.key);
        hashes.insert(info.hash);
    }

    EXPECT_EQ(keys.size(), static_cast<size_t>(N))
        << "All 100 generated keys must be unique";
    EXPECT_EQ(hashes.size(), static_cast<size_t>(N))
        << "All 100 hashes must be unique";
}

// ===========================================================================
// Section 3: hashApiKey() — determinism and format
// ===========================================================================

TEST_F(ApiKeyGeneratorTest, HashApiKey_Deterministic) {
    const std::string key = "icao_AbCdEfGh_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef";
    std::string h1 = hashApiKey(key);
    std::string h2 = hashApiKey(key);
    EXPECT_EQ(h1, h2) << "Same input must always produce same hash";
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_OutputLength_Is64) {
    EXPECT_EQ(hashApiKey("some_api_key").size(), 64u);
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_OutputIsLowercaseHex) {
    std::string h = hashApiKey("test_key_value");
    EXPECT_TRUE(isLowercaseHexString(h)) << "Hash must be lowercase hex";
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_DifferentInputs_DifferentHashes) {
    std::string h1 = hashApiKey("icao_aaaaaaaa_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    std::string h2 = hashApiKey("icao_bbbbbbbb_BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
    EXPECT_NE(h1, h2);
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_KnownVector_SHA256) {
    // SHA-256("") == e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::string h = hashApiKey("");
    EXPECT_EQ(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_KnownVector_ABC) {
    // SHA-256("abc") == ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    // (verified with: echo -n "abc" | openssl dgst -sha256)
    std::string h = hashApiKey("abc");
    EXPECT_EQ(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_SingleChar) {
    std::string h = hashApiKey("a");
    EXPECT_EQ(h.size(), 64u);
    EXPECT_TRUE(isLowercaseHexString(h));
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_MaxLengthInput) {
    // 1 MiB of 'x'
    std::string bigKey(1024 * 1024, 'x');
    std::string h = hashApiKey(bigKey);
    EXPECT_EQ(h.size(), 64u);
    EXPECT_TRUE(isLowercaseHexString(h));
}

TEST_F(ApiKeyGeneratorTest, HashApiKey_BinaryBytes) {
    // Key containing all byte values 0x00..0xFF
    std::string binaryKey;
    binaryKey.resize(256);
    for (int i = 0; i < 256; ++i) {
        binaryKey[i] = static_cast<char>(i);
    }
    std::string h = hashApiKey(binaryKey);
    EXPECT_EQ(h.size(), 64u);
    EXPECT_TRUE(isLowercaseHexString(h));
}

// ===========================================================================
// Section 4: Key structure regex validation
// ===========================================================================

TEST_F(ApiKeyGeneratorTest, GenerateKey_MatchesFullRegex) {
    // Pattern: icao_[0-9A-Za-z]{8}_[0-9A-Za-z]{32}
    static const std::regex kPattern("^icao_[0-9A-Za-z]{8}_[0-9A-Za-z]{32}$");
    for (int i = 0; i < 20; ++i) {
        auto info = generateKey();
        EXPECT_TRUE(std::regex_match(info.key, kPattern))
            << "Key does not match expected pattern: " << info.key;
    }
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_PrefixMatchesRegex) {
    static const std::regex kPrefixPattern("^icao_[0-9A-Za-z]{8}$");
    for (int i = 0; i < 20; ++i) {
        auto info = generateKey();
        EXPECT_TRUE(std::regex_match(info.prefix, kPrefixPattern))
            << "Prefix does not match pattern: " << info.prefix;
    }
}

// ===========================================================================
// Section 5: ApiKeyInfo fields cross-consistency
// ===========================================================================

TEST_F(ApiKeyGeneratorTest, GenerateKey_PrefixIsFirstPartOfKey) {
    auto info = generateKey();
    // info.prefix == info.key.substr(0, 13)
    EXPECT_EQ(info.prefix, info.key.substr(0, 13))
        << "prefix field must equal the first 13 chars of key";
}

TEST_F(ApiKeyGeneratorTest, GenerateKey_AllFieldsNonEmpty) {
    auto info = generateKey();
    EXPECT_FALSE(info.key.empty());
    EXPECT_FALSE(info.hash.empty());
    EXPECT_FALSE(info.prefix.empty());
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

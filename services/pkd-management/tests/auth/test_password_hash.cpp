/**
 * @file test_password_hash.cpp
 * @brief Unit tests for auth::hashPassword(), verifyPassword(),
 *        extractSalt(), and extractIterations()
 *
 * Covers:
 *  - Hash format:  $pbkdf2$<iterations>$<salt_hex>$<hash_hex>
 *  - Salt:         32 hex chars (16 random bytes)
 *  - Hash output:  64 hex chars (32-byte PBKDF2 output)
 *  - Iteration default: 310 000 (OWASP 2023)
 *  - Uniqueness:   two calls on same password produce different salts/hashes
 *  - verifyPassword: correct password → true, wrong password → false
 *  - extractSalt / extractIterations: round-trip accuracy
 *  - Edge cases:   empty password, very long password, unicode / Korean text,
 *                  tampered hash, invalid format
 */

#include <gtest/gtest.h>
#include "../../src/auth/password_hash.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

using namespace auth;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isHexString(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    });
}

// Parse the hash string and return the four components.
// Returns false if the format is unexpected.
static bool parseHash(const std::string& stored,
                      std::string& alg,
                      int&         iters,
                      std::string& saltHex,
                      std::string& hashHex) {
    // Expected: $pbkdf2$<iter>$<salt>$<hash>
    if (stored.size() < 10) return false;
    if (stored[0] != '$') return false;

    auto split = [](const std::string& s, char delim) {
        std::vector<std::string> tokens;
        std::string cur;
        for (char c : s) {
            if (c == delim) { tokens.push_back(cur); cur.clear(); }
            else            { cur += c; }
        }
        tokens.push_back(cur);
        return tokens;
    };

    // Drop leading '$' then split on '$'
    auto parts = split(stored.substr(1), '$');
    if (parts.size() != 4) return false;

    alg     = parts[0];
    try { iters = std::stoi(parts[1]); } catch (...) { return false; }
    saltHex = parts[2];
    hashHex = parts[3];
    return true;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class PasswordHashTest : public ::testing::Test {
protected:
    const std::string kShortSecret    = "password";
    const std::string kLongSecret     = std::string(1024, 'x');
    // Korean text
    const std::string kKoreanPassword = "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4\xEB\xB9\x84\xEB\xB0\x80\xEB\xB2\x88\xED\x98\xB8";  // "한국어비밀번호"
    const std::string kUnicodePassword = "\xF0\x9F\x94\x90secret";  // emoji + ascii
};

// ===========================================================================
// Section 1: hashPassword() — output format
// ===========================================================================

TEST_F(PasswordHashTest, HashFormat_StartsWithDollarPbkdf2) {
    std::string h = hashPassword(kShortSecret);
    EXPECT_EQ(h.substr(0, 8), "$pbkdf2$")
        << "Hash must start with '$pbkdf2$', got: " << h.substr(0, 8);
}

TEST_F(PasswordHashTest, HashFormat_FourDollarSignSegments) {
    std::string h = hashPassword(kShortSecret);
    // Count '$' chars: should be 4 (one at start, then three separators)
    int count = static_cast<int>(std::count(h.begin(), h.end(), '$'));
    EXPECT_EQ(count, 4)
        << "Hash string must contain exactly 4 '$' separators, got: " << h;
}

TEST_F(PasswordHashTest, HashFormat_SaltIs32HexChars) {
    std::string h = hashPassword(kShortSecret);
    std::string alg; int iters; std::string salt; std::string hashHex;
    ASSERT_TRUE(parseHash(h, alg, iters, salt, hashHex));
    EXPECT_EQ(salt.size(), 32u)
        << "Salt must be 32 hex chars (16 bytes), got size: " << salt.size();
    EXPECT_TRUE(isHexString(salt)) << "Salt must be hex, got: " << salt;
}

TEST_F(PasswordHashTest, HashFormat_HashIs64HexChars) {
    std::string h = hashPassword(kShortSecret);
    std::string alg; int iters; std::string salt; std::string hashHex;
    ASSERT_TRUE(parseHash(h, alg, iters, salt, hashHex));
    EXPECT_EQ(hashHex.size(), 64u)
        << "Hash segment must be 64 hex chars (32 bytes), got: " << hashHex.size();
    EXPECT_TRUE(isHexString(hashHex));
}

TEST_F(PasswordHashTest, HashFormat_AlgorithmField_IsPbkdf2) {
    std::string h = hashPassword(kShortSecret);
    std::string alg; int iters; std::string salt; std::string hashHex;
    ASSERT_TRUE(parseHash(h, alg, iters, salt, hashHex));
    EXPECT_EQ(alg, "pbkdf2");
}

// ===========================================================================
// Section 2: hashPassword() — default and custom iterations
// ===========================================================================

TEST_F(PasswordHashTest, DefaultIterations_Is310000) {
    std::string h = hashPassword(kShortSecret);
    std::string alg; int iters; std::string salt; std::string hashHex;
    ASSERT_TRUE(parseHash(h, alg, iters, salt, hashHex));
    EXPECT_EQ(iters, 310000);
}

TEST_F(PasswordHashTest, CustomIterations_StoredInHash) {
    std::string h = hashPassword(kShortSecret, 10000);
    std::string alg; int iters; std::string salt; std::string hashHex;
    ASSERT_TRUE(parseHash(h, alg, iters, salt, hashHex));
    EXPECT_EQ(iters, 10000);
}

// ===========================================================================
// Section 3: hashPassword() — uniqueness (salt randomness)
// ===========================================================================

TEST_F(PasswordHashTest, TwoCalls_SamePassword_DifferentSalts) {
    std::string h1 = hashPassword(kShortSecret);
    std::string h2 = hashPassword(kShortSecret);

    std::string alg1, alg2;
    int iters1, iters2;
    std::string salt1, salt2, hash1, hash2;

    ASSERT_TRUE(parseHash(h1, alg1, iters1, salt1, hash1));
    ASSERT_TRUE(parseHash(h2, alg2, iters2, salt2, hash2));

    EXPECT_NE(salt1, salt2) << "Two hashes of the same password must use different salts";
    EXPECT_NE(hash1, hash2) << "Two hashes of the same password must produce different hash values";
    EXPECT_NE(h1, h2)       << "Full stored strings must differ";
}

// ===========================================================================
// Section 4: verifyPassword() — happy path
// ===========================================================================

TEST_F(PasswordHashTest, VerifyPassword_CorrectPassword_ReturnsTrue) {
    std::string h = hashPassword(kShortSecret);
    EXPECT_TRUE(verifyPassword(kShortSecret, h));
}

TEST_F(PasswordHashTest, VerifyPassword_CorrectPassword_Idempotent) {
    std::string h = hashPassword(kShortSecret);
    // Calling verifyPassword twice must not alter state
    EXPECT_TRUE(verifyPassword(kShortSecret, h));
    EXPECT_TRUE(verifyPassword(kShortSecret, h));
}

TEST_F(PasswordHashTest, VerifyPassword_LongPassword_ReturnsTrue) {
    std::string h = hashPassword(kLongSecret, 1000);
    EXPECT_TRUE(verifyPassword(kLongSecret, h));
}

TEST_F(PasswordHashTest, VerifyPassword_KoreanPassword_ReturnsTrue) {
    std::string h = hashPassword(kKoreanPassword, 1000);
    EXPECT_TRUE(verifyPassword(kKoreanPassword, h));
}

TEST_F(PasswordHashTest, VerifyPassword_UnicodeEmojiPassword_ReturnsTrue) {
    std::string h = hashPassword(kUnicodePassword, 1000);
    EXPECT_TRUE(verifyPassword(kUnicodePassword, h));
}

TEST_F(PasswordHashTest, VerifyPassword_EmptyPassword_Roundtrip) {
    // Empty passwords are technically valid; the implementation must not crash
    std::string h = hashPassword("", 1000);
    EXPECT_TRUE(verifyPassword("", h));
}

// ===========================================================================
// Section 5: verifyPassword() — rejection
// ===========================================================================

TEST_F(PasswordHashTest, VerifyPassword_WrongPassword_ReturnsFalse) {
    std::string h = hashPassword(kShortSecret);
    EXPECT_FALSE(verifyPassword("wrongpassword", h));
}

TEST_F(PasswordHashTest, VerifyPassword_EmptyInputVsNonEmptyHash_ReturnsFalse) {
    std::string h = hashPassword(kShortSecret);
    EXPECT_FALSE(verifyPassword("", h));
}

TEST_F(PasswordHashTest, VerifyPassword_NonEmptyInputVsEmptyPasswordHash_ReturnsFalse) {
    std::string h = hashPassword("", 1000);
    EXPECT_FALSE(verifyPassword(kShortSecret, h));
}

TEST_F(PasswordHashTest, VerifyPassword_CaseSensitive) {
    std::string h = hashPassword("Password");
    EXPECT_FALSE(verifyPassword("password", h));
    EXPECT_FALSE(verifyPassword("PASSWORD", h));
}

TEST_F(PasswordHashTest, VerifyPassword_InvalidHashFormat_ReturnsFalse) {
    EXPECT_FALSE(verifyPassword(kShortSecret, "not-a-valid-hash"));
    EXPECT_FALSE(verifyPassword(kShortSecret, ""));
    EXPECT_FALSE(verifyPassword(kShortSecret, "$bcrypt$..."));
}

TEST_F(PasswordHashTest, VerifyPassword_TamperedHash_ReturnsFalse) {
    std::string h = hashPassword(kShortSecret, 1000);
    // Flip one hex digit in the stored hash segment
    // The hash segment starts after the last '$'
    std::string tampered = h;
    size_t lastDollar = tampered.rfind('$');
    ASSERT_NE(lastDollar, std::string::npos);
    // Flip 'a' -> 'b' or first char -> '0'
    char& c = tampered[lastDollar + 1];
    c = (c == '0') ? '1' : '0';

    EXPECT_FALSE(verifyPassword(kShortSecret, tampered));
}

TEST_F(PasswordHashTest, VerifyPassword_TamperedSalt_ReturnsFalse) {
    std::string h = hashPassword(kShortSecret, 1000);
    // Parse and flip a salt character
    std::string alg; int iters; std::string salt; std::string hashHex;
    ASSERT_TRUE(parseHash(h, alg, iters, salt, hashHex));

    // Rebuild with flipped salt
    std::string newSalt = salt;
    newSalt[0] = (newSalt[0] == '0') ? 'f' : '0';
    std::string tampered = "$pbkdf2$" +
                           std::to_string(iters) + "$" +
                           newSalt + "$" + hashHex;

    EXPECT_FALSE(verifyPassword(kShortSecret, tampered));
}

TEST_F(PasswordHashTest, VerifyPassword_TruncatedHash_ReturnsFalse) {
    std::string h = hashPassword(kShortSecret, 1000);
    // Truncate the hash — must not crash
    EXPECT_FALSE(verifyPassword(kShortSecret, h.substr(0, h.size() / 2)));
}

// ===========================================================================
// Section 6: extractSalt() and extractIterations()
// ===========================================================================

TEST_F(PasswordHashTest, ExtractSalt_ReturnsCorrectSalt) {
    std::string h = hashPassword(kShortSecret, 1000);
    std::string alg; int iters; std::string expectedSalt; std::string hashHex;
    ASSERT_TRUE(parseHash(h, alg, iters, expectedSalt, hashHex));

    std::string extracted = extractSalt(h);
    EXPECT_EQ(extracted, expectedSalt)
        << "extractSalt() must return the salt segment";
}

TEST_F(PasswordHashTest, ExtractIterations_ReturnsDefaultIterations) {
    std::string h = hashPassword(kShortSecret);  // default 310000
    int extracted = extractIterations(h);
    EXPECT_EQ(extracted, 310000);
}

TEST_F(PasswordHashTest, ExtractIterations_ReturnsCustomIterations) {
    std::string h = hashPassword(kShortSecret, 50000);
    EXPECT_EQ(extractIterations(h), 50000);
}

TEST_F(PasswordHashTest, ExtractSalt_Throws_OnInvalidFormat) {
    EXPECT_THROW(extractSalt("not-a-valid-hash"), std::exception);
}

TEST_F(PasswordHashTest, ExtractIterations_Throws_OnInvalidFormat) {
    EXPECT_THROW(extractIterations("not-a-valid-hash"), std::exception);
}

// ===========================================================================
// Section 7: Cross-check — verify uses extracted salt & iters
// ===========================================================================

TEST_F(PasswordHashTest, VerifyUsesEmbeddedSaltAndIterations) {
    // Hash with non-default iterations to confirm verify reads them correctly
    const int customIter = 5000;
    std::string h = hashPassword("mypassword", customIter);

    EXPECT_EQ(extractIterations(h), customIter);
    EXPECT_TRUE(verifyPassword("mypassword", h));
    EXPECT_FALSE(verifyPassword("wrongpassword", h));
}

// ===========================================================================
// Section 8: Edge cases
// ===========================================================================

TEST_F(PasswordHashTest, HashPassword_PasswordWithSpecialChars) {
    const std::string special = "p@$$w0rd!#%^&*()_+-=[]{}|;':\",./<>?";
    std::string h = hashPassword(special, 1000);
    EXPECT_TRUE(verifyPassword(special, h));
    EXPECT_FALSE(verifyPassword("p@$$w0rd", h));
}

TEST_F(PasswordHashTest, HashPassword_PasswordWithNullBytes) {
    // std::string can hold embedded NUL bytes
    std::string nullPassword("pass\x00word", 9);
    std::string h = hashPassword(nullPassword, 1000);
    EXPECT_TRUE(verifyPassword(nullPassword, h));
    // The version without the NUL must fail
    EXPECT_FALSE(verifyPassword("password", h));
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

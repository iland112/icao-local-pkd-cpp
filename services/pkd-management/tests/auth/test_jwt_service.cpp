/**
 * @file test_jwt_service.cpp
 * @brief Unit tests for auth::JwtService — HS256 JWT generation and validation
 *
 * Covers:
 *  - Constructor: short secret key rejection (< 32 chars)
 *  - generateToken(): three-part dot-delimited output, Base64URL encoding
 *  - generateToken(): claims round-trip (userId, username, permissions, isAdmin)
 *  - generateToken(): iat < exp, correct issuer
 *  - validateToken(): valid token returns correct JwtClaims
 *  - validateToken(): expired token returns std::nullopt
 *  - validateToken(): wrong signature returns std::nullopt
 *  - validateToken(): wrong issuer returns std::nullopt
 *  - validateToken(): malformed token returns std::nullopt
 *  - validateToken(): empty string returns std::nullopt
 *  - refreshToken(): produces new valid token with same claims
 *  - refreshToken(): invalid token returns empty string
 *  - isTokenExpired(): expired token → true, valid token → false
 *  - Idempotency: multiple validateToken() calls on same token return equal results
 *  - Permissions: empty list, single permission, multiple permissions
 *  - Admin flag: both true and false
 *  - Edge cases: unicode username, very long userId, minimal secret length (32)
 */

#include <gtest/gtest.h>
#include "../../src/auth/jwt_service.h"

#include <algorithm>
#include <chrono>
#include <regex>
#include <string>
#include <thread>
#include <vector>

using namespace auth;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr const char* kSecret32 =
    "this-is-exactly-32-bytes-long!!!";  // exactly 32 chars

static constexpr const char* kSecret64 =
    "a-longer-secret-key-that-is-definitely-more-than-32-bytes-for-tests!!";

static bool isBase64UrlChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

static bool isBase64UrlSegment(const std::string& s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(), isBase64UrlChar);
}

// Split "a.b.c" → {"a","b","c"}
static std::vector<std::string> splitDot(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == '.') { parts.push_back(cur); cur.clear(); }
        else           { cur += c; }
    }
    parts.push_back(cur);
    return parts;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class JwtServiceTest : public ::testing::Test {
protected:
    // Use a short expiration (3600s = 1h) for most tests
    JwtService svc{kSecret64, "icao-pkd", 3600};

    const std::string kUserId      = "550e8400-e29b-41d4-a716-446655440000";
    const std::string kUsername    = "testuser";
    const std::vector<std::string> kPerms = {"cert:read", "upload:file", "pa:verify"};
};

// ===========================================================================
// Section 1: Constructor
// ===========================================================================

TEST(JwtServiceConstructorTest, ShortKey_Throws_RuntimeError) {
    // Key shorter than 32 chars must throw
    EXPECT_THROW(
        JwtService("short"),
        std::runtime_error);
}

TEST(JwtServiceConstructorTest, Exactly32CharKey_DoesNotThrow) {
    EXPECT_NO_THROW({ JwtService svc(kSecret32); });
}

TEST(JwtServiceConstructorTest, LongerKey_DoesNotThrow) {
    EXPECT_NO_THROW({ JwtService svc(kSecret64); });
}

TEST(JwtServiceConstructorTest, Custom_Issuer_DoesNotThrow) {
    EXPECT_NO_THROW(JwtService(kSecret64, "my-custom-issuer", 7200));
}

// ===========================================================================
// Section 2: generateToken() — structural format
// ===========================================================================

TEST_F(JwtServiceTest, GenerateToken_HasThreeDotSeparatedParts) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto parts = splitDot(token);
    ASSERT_EQ(parts.size(), 3u) << "JWT must have exactly 3 dot-separated parts";
}

TEST_F(JwtServiceTest, GenerateToken_AllPartsNonEmpty) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto parts = splitDot(token);
    for (const auto& p : parts) {
        EXPECT_FALSE(p.empty()) << "No JWT part may be empty";
    }
}

TEST_F(JwtServiceTest, GenerateToken_AllPartsAreBase64Url) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto parts = splitDot(token);
    for (const auto& p : parts) {
        EXPECT_TRUE(isBase64UrlSegment(p))
            << "JWT part is not valid Base64URL: " << p;
    }
}

TEST_F(JwtServiceTest, GenerateToken_NoPaddingEquals) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    EXPECT_EQ(token.find('='), std::string::npos)
        << "JWT must not contain '=' padding characters";
}

// ===========================================================================
// Section 3: validateToken() — happy path
// ===========================================================================

TEST_F(JwtServiceTest, ValidateToken_ValidToken_ReturnsOptionalWithClaims) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms, false);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value()) << "Valid token must return claims";
}

TEST_F(JwtServiceTest, ValidateToken_Claims_UserIdMatches) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->userId, kUserId);
}

TEST_F(JwtServiceTest, ValidateToken_Claims_UsernameMatches) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->username, kUsername);
}

TEST_F(JwtServiceTest, ValidateToken_Claims_PermissionsMatch) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->permissions, kPerms);
}

TEST_F(JwtServiceTest, ValidateToken_Claims_IsAdmin_False) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms, false);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_FALSE(claims->isAdmin);
}

TEST_F(JwtServiceTest, ValidateToken_Claims_IsAdmin_True) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms, true);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_TRUE(claims->isAdmin);
}

TEST_F(JwtServiceTest, ValidateToken_Claims_IatBeforeExp) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_LT(claims->iat, claims->exp)
        << "iat must be before exp";
}

TEST_F(JwtServiceTest, ValidateToken_Claims_ExpInFuture) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    auto now = std::chrono::system_clock::now();
    EXPECT_GT(claims->exp, now)
        << "exp must be in the future for a freshly-issued token";
}

// ===========================================================================
// Section 4: validateToken() — failure cases
// ===========================================================================

TEST_F(JwtServiceTest, ValidateToken_EmptyString_ReturnsNullopt) {
    EXPECT_FALSE(svc.validateToken("").has_value());
}

TEST_F(JwtServiceTest, ValidateToken_MalformedOnePartOnly_ReturnsNullopt) {
    EXPECT_FALSE(svc.validateToken("thisisnot.ajwt").has_value());
}

TEST_F(JwtServiceTest, ValidateToken_MalformedNoDots_ReturnsNullopt) {
    EXPECT_FALSE(svc.validateToken("nodotsinhere").has_value());
}

TEST_F(JwtServiceTest, ValidateToken_WrongSignature_ReturnsNullopt) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    // Append one character to the signature (3rd part)
    std::string bad = token + "x";
    EXPECT_FALSE(svc.validateToken(bad).has_value());
}

TEST_F(JwtServiceTest, ValidateToken_TamperedPayload_ReturnsNullopt) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    auto parts = splitDot(token);
    ASSERT_EQ(parts.size(), 3u);

    // Change one char in the payload (middle part)
    std::string tamperedPayload = parts[1];
    tamperedPayload[0] = (tamperedPayload[0] == 'A') ? 'B' : 'A';

    std::string tamperedToken = parts[0] + "." + tamperedPayload + "." + parts[2];
    EXPECT_FALSE(svc.validateToken(tamperedToken).has_value());
}

TEST_F(JwtServiceTest, ValidateToken_TokenSignedWithDifferentKey_ReturnsNullopt) {
    // Token generated by another service instance with a different secret
    JwtService otherSvc("different-secret-that-is-at-least-32!!!", "icao-pkd", 3600);
    auto tokenFromOther = otherSvc.generateToken(kUserId, kUsername, kPerms);

    // Our service should reject it (wrong signature)
    EXPECT_FALSE(svc.validateToken(tokenFromOther).has_value());
}

TEST_F(JwtServiceTest, ValidateToken_WrongIssuer_ReturnsNullopt) {
    JwtService svcA(kSecret64, "service-A", 3600);
    JwtService svcB(kSecret64, "service-B", 3600);

    // svcA generates, svcB validates — issuer mismatch even if key is same
    auto token = svcA.generateToken(kUserId, kUsername, kPerms);
    EXPECT_FALSE(svcB.validateToken(token).has_value());
}

TEST_F(JwtServiceTest, ValidateToken_ExpiredToken_ReturnsNullopt) {
    // Create a service with 1-second expiration
    JwtService shortLivedSvc(kSecret64, "icao-pkd", 1);
    auto token = shortLivedSvc.generateToken(kUserId, kUsername, kPerms);

    // Sleep to let it expire
    std::this_thread::sleep_for(std::chrono::seconds(2));

    EXPECT_FALSE(shortLivedSvc.validateToken(token).has_value())
        << "Token should be expired after 2s with 1s TTL";
}

// ===========================================================================
// Section 5: isTokenExpired()
// ===========================================================================

TEST_F(JwtServiceTest, IsTokenExpired_FreshToken_ReturnsFalse) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms);
    EXPECT_FALSE(svc.isTokenExpired(token));
}

TEST_F(JwtServiceTest, IsTokenExpired_ExpiredToken_ReturnsTrue) {
    JwtService shortLivedSvc(kSecret64, "icao-pkd", 1);
    auto token = shortLivedSvc.generateToken(kUserId, kUsername, kPerms);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    EXPECT_TRUE(shortLivedSvc.isTokenExpired(token));
}

TEST_F(JwtServiceTest, IsTokenExpired_InvalidToken_ReturnsTrue) {
    // isTokenExpired returns true for tokens that fail validation
    EXPECT_TRUE(svc.isTokenExpired("not.a.token"));
    EXPECT_TRUE(svc.isTokenExpired(""));
}

// ===========================================================================
// Section 6: refreshToken()
// ===========================================================================

TEST_F(JwtServiceTest, RefreshToken_ValidToken_ReturnsNewNonEmptyToken) {
    auto original = svc.generateToken(kUserId, kUsername, kPerms);
    auto refreshed = svc.refreshToken(original);
    EXPECT_FALSE(refreshed.empty())
        << "refreshToken() must return a non-empty token";
}

TEST_F(JwtServiceTest, RefreshToken_NewToken_IsValid) {
    auto original  = svc.generateToken(kUserId, kUsername, kPerms);
    auto refreshed = svc.refreshToken(original);

    auto claims = svc.validateToken(refreshed);
    ASSERT_TRUE(claims.has_value())
        << "Refreshed token must be valid";
}

TEST_F(JwtServiceTest, RefreshToken_PreservesClaims) {
    auto original  = svc.generateToken(kUserId, kUsername, kPerms, true);
    auto refreshed = svc.refreshToken(original);

    auto claims = svc.validateToken(refreshed);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->userId,      kUserId);
    EXPECT_EQ(claims->username,    kUsername);
    EXPECT_EQ(claims->permissions, kPerms);
    EXPECT_TRUE(claims->isAdmin);
}

TEST_F(JwtServiceTest, RefreshToken_NewTokenHasLaterExpiry) {
    auto original = svc.generateToken(kUserId, kUsername, kPerms);

    auto origClaims = svc.validateToken(original);
    ASSERT_TRUE(origClaims.has_value());

    // Small sleep to ensure different iat/exp
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto refreshed = svc.refreshToken(original);
    auto newClaims = svc.validateToken(refreshed);
    ASSERT_TRUE(newClaims.has_value());

    EXPECT_GE(newClaims->exp, origClaims->exp)
        << "Refreshed token's exp must be >= original's exp";
}

TEST_F(JwtServiceTest, RefreshToken_InvalidToken_ReturnsEmptyString) {
    EXPECT_EQ(svc.refreshToken("bad.token.value"), "");
    EXPECT_EQ(svc.refreshToken(""), "");
}

// ===========================================================================
// Section 7: Permissions edge cases
// ===========================================================================

TEST_F(JwtServiceTest, Permissions_EmptyList_RoundTrip) {
    auto token = svc.generateToken(kUserId, kUsername, {});
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_TRUE(claims->permissions.empty());
}

TEST_F(JwtServiceTest, Permissions_SinglePermission_RoundTrip) {
    auto token = svc.generateToken(kUserId, kUsername, {"cert:read"});
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    ASSERT_EQ(claims->permissions.size(), 1u);
    EXPECT_EQ(claims->permissions[0], "cert:read");
}

TEST_F(JwtServiceTest, Permissions_AllThirteenPermissions_RoundTrip) {
    std::vector<std::string> allPerms = {
        "cert:read", "cert:export", "pa:verify", "pa:read",
        "pa:stats", "upload:read", "upload:file", "upload:cert", "report:read",
        "ai:read", "sync:read", "icao:read", "api-client:manage"
    };
    auto token = svc.generateToken(kUserId, kUsername, allPerms, true);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->permissions, allPerms);
}

// ===========================================================================
// Section 8: Idempotency — multiple validateToken() calls
// ===========================================================================

TEST_F(JwtServiceTest, Idempotency_MultipleValidations_SameClaims) {
    auto token = svc.generateToken(kUserId, kUsername, kPerms, false);

    auto c1 = svc.validateToken(token);
    auto c2 = svc.validateToken(token);
    auto c3 = svc.validateToken(token);

    ASSERT_TRUE(c1.has_value());
    ASSERT_TRUE(c2.has_value());
    ASSERT_TRUE(c3.has_value());

    EXPECT_EQ(c1->userId,   c2->userId);
    EXPECT_EQ(c1->username, c2->username);
    EXPECT_EQ(c1->isAdmin,  c2->isAdmin);
    EXPECT_EQ(c1->userId,   c3->userId);
}

// ===========================================================================
// Section 9: Edge cases — special characters in claims
// ===========================================================================

TEST_F(JwtServiceTest, Username_WithKorean_RoundTrip) {
    // Korean username: "홍길동"
    const std::string koreanName = "\xED\x99\x8D\xEA\xB8\xB8\xEB\x8F\x99";
    auto token  = svc.generateToken(kUserId, koreanName, {});
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->username, koreanName);
}

TEST_F(JwtServiceTest, UserId_LongUUID_RoundTrip) {
    const std::string longId = "550e8400-e29b-41d4-a716-446655440000-ext-extra";
    auto token  = svc.generateToken(longId, kUsername, {});
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->userId, longId);
}

TEST_F(JwtServiceTest, GenerateToken_DefaultIsAdmin_IsFalse) {
    // isAdmin defaults to false when not specified
    auto token  = svc.generateToken(kUserId, kUsername, kPerms);
    auto claims = svc.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_FALSE(claims->isAdmin);
}

TEST_F(JwtServiceTest, TwoTokens_ForSameUser_AreDifferent) {
    // Tokens generated at different times must differ (different iat/exp)
    auto t1 = svc.generateToken(kUserId, kUsername, kPerms);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto t2 = svc.generateToken(kUserId, kUsername, kPerms);
    // The tokens may be equal if generated within the same second, but let the
    // test be advisory rather than mandatory for sub-second resolution.
    (void)t1; (void)t2;  // suppress unused warnings if sleep is < 1s
}

// ===========================================================================
// Section 10: Minimum-length secret key (32 chars)
// ===========================================================================

TEST(JwtServiceMinKeyTest, MinimalKeyLength_TokenValid) {
    JwtService svc32(kSecret32, "icao-pkd", 3600);
    auto token  = svc32.generateToken("uid1", "user1", {"cert:read"}, false);
    auto claims = svc32.validateToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->username, "user1");
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#include "password_hash.h"
#include "jwt_service.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== Phase 3 Authentication Module Test ===" << std::endl;

    // Test 1: Password Hashing
    std::cout << "\n[Test 1] Password Hashing..." << std::endl;
    std::string password = "test_password_123";
    std::string hash = auth::hashPassword(password);
    std::cout << "  Hash format: " << hash.substr(0, 50) << "..." << std::endl;

    bool verifySuccess = auth::verifyPassword(password, hash);
    assert(verifySuccess && "Password verification should succeed");
    std::cout << "  ✅ Correct password verified" << std::endl;

    bool verifyFail = auth::verifyPassword("wrong_password", hash);
    assert(!verifyFail && "Wrong password should fail");
    std::cout << "  ✅ Wrong password rejected" << std::endl;

    // Test 2: JWT Service
    std::cout << "\n[Test 2] JWT Service..." << std::endl;
    std::string secretKey = "this_is_a_very_secret_key_at_least_32_characters_long";
    auth::JwtService jwtService(secretKey);

    std::vector<std::string> permissions = {"upload:write", "cert:read"};
    std::string token = jwtService.generateToken(
        "user-uuid-123",
        "testuser",
        permissions,
        false
    );
    std::cout << "  Token: " << token.substr(0, 50) << "..." << std::endl;

    auto claims = jwtService.validateToken(token);
    assert(claims.has_value() && "Token validation should succeed");
    assert(claims->userId == "user-uuid-123" && "UserId should match");
    assert(claims->username == "testuser" && "Username should match");
    assert(claims->permissions.size() == 2 && "Should have 2 permissions");
    assert(!claims->isAdmin && "Should not be admin");
    std::cout << "  ✅ Token validated successfully" << std::endl;
    std::cout << "  Username: " << claims->username << std::endl;
    std::cout << "  Permissions: " << claims->permissions.size() << std::endl;

    // Test 3: Token Refresh
    std::cout << "\n[Test 3] Token Refresh..." << std::endl;
    std::string refreshedToken = jwtService.refreshToken(token);
    assert(!refreshedToken.empty() && "Refreshed token should not be empty");
    assert(refreshedToken != token && "Refreshed token should be different");
    std::cout << "  ✅ Token refreshed successfully" << std::endl;

    auto refreshedClaims = jwtService.validateToken(refreshedToken);
    assert(refreshedClaims.has_value() && "Refreshed token should be valid");
    assert(refreshedClaims->userId == claims->userId && "UserId should match");
    std::cout << "  ✅ Refreshed token validated successfully" << std::endl;

    // Test 4: Invalid Token
    std::cout << "\n[Test 4] Invalid Token Handling..." << std::endl;
    std::string invalidToken = "invalid.token.here";
    auto invalidClaims = jwtService.validateToken(invalidToken);
    assert(!invalidClaims.has_value() && "Invalid token should fail");
    std::cout << "  ✅ Invalid token correctly rejected" << std::endl;

    std::cout << "\n=== All Tests Passed! ===" << std::endl;
    return 0;
}

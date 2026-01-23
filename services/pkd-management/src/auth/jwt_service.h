#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace auth {

/**
 * @brief JWT Claims structure
 *
 * Contains user identity and authorization information extracted from JWT.
 */
struct JwtClaims {
    std::string userId;                    // User UUID
    std::string username;                  // Username
    std::vector<std::string> permissions;  // Permission list
    bool isAdmin;                          // Admin flag
    std::chrono::system_clock::time_point exp;  // Expiration time
    std::chrono::system_clock::time_point iat;  // Issued at time
};

/**
 * @brief JWT Service for token generation and validation
 *
 * This service handles JWT operations including:
 * - Token generation with user claims
 * - Token validation and expiration checking
 * - Token refresh (re-issue with extended expiration)
 *
 * Uses HS256 (HMAC-SHA256) algorithm for signing.
 */
class JwtService {
public:
    /**
     * @brief Construct JwtService with secret key
     *
     * @param secretKey Secret key for HMAC signing (should be at least 256 bits / 32 bytes)
     * @param issuer Token issuer identifier (default: "icao-pkd")
     * @param expirationSeconds Token validity duration in seconds (default: 3600 = 1 hour)
     */
    explicit JwtService(
        const std::string& secretKey,
        const std::string& issuer = "icao-pkd",
        int expirationSeconds = 3600);

    /**
     * @brief Generate JWT token for a user
     *
     * @param userId User UUID from database
     * @param username Username
     * @param permissions List of permissions (e.g., ["upload:write", "cert:read"])
     * @param isAdmin Admin flag
     * @return JWT token string
     */
    std::string generateToken(
        const std::string& userId,
        const std::string& username,
        const std::vector<std::string>& permissions,
        bool isAdmin = false);

    /**
     * @brief Validate JWT token and extract claims
     *
     * Checks:
     * - Signature validity
     * - Token expiration
     * - Issuer match
     *
     * @param token JWT token string
     * @return JwtClaims if valid, std::nullopt if invalid or expired
     */
    std::optional<JwtClaims> validateToken(const std::string& token);

    /**
     * @brief Refresh token (extend expiration)
     *
     * Validates the current token and issues a new one with updated expiration time.
     * All other claims remain the same.
     *
     * @param token Current JWT token
     * @return New JWT token with extended expiration, or empty string if current token is invalid
     */
    std::string refreshToken(const std::string& token);

    /**
     * @brief Check if token is expired
     *
     * @param token JWT token string
     * @return true if expired, false otherwise
     */
    bool isTokenExpired(const std::string& token);

private:
    std::string secretKey_;
    std::string issuer_;
    int expirationSeconds_;
};

} // namespace auth

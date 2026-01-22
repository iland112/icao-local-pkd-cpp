#include "jwt_service.h"
#include <jwt-cpp/jwt.h>
#include <spdlog/spdlog.h>

namespace auth {

JwtService::JwtService(
    const std::string& secretKey,
    const std::string& issuer,
    int expirationSeconds)
    : secretKey_(secretKey)
    , issuer_(issuer)
    , expirationSeconds_(expirationSeconds) {

    if (secretKey_.length() < 32) {
        throw std::runtime_error("JWT secret key must be at least 32 characters (256 bits)");
    }

    spdlog::info("[JwtService] Initialized with issuer={}, expiration={}s",
                 issuer_, expirationSeconds_);
}

std::string JwtService::generateToken(
    const std::string& userId,
    const std::string& username,
    const std::vector<std::string>& permissions,
    bool isAdmin) {

    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(expirationSeconds_);

    // Build permissions array for JWT claims
    picojson::array permsArray;
    for (const auto& perm : permissions) {
        permsArray.push_back(picojson::value(perm));
    }

    try {
        auto token = jwt::create()
            .set_issuer(issuer_)
            .set_type("JWT")
            .set_issued_at(now)
            .set_expires_at(exp)
            .set_subject(userId)
            .set_payload_claim("username", jwt::claim(username))
            .set_payload_claim("permissions", jwt::claim(permsArray))
            .set_payload_claim("isAdmin", jwt::claim(isAdmin))
            .sign(jwt::algorithm::hs256{secretKey_});

        spdlog::debug("[JwtService] Generated token for user={}, isAdmin={}, permissions={}",
                      username, isAdmin, permissions.size());

        return token;

    } catch (const std::exception& exc) {
        spdlog::error("[JwtService] Token generation failed: {}", exc.what());
        throw;
    }
}

std::optional<JwtClaims> JwtService::validateToken(const std::string& token) {
    try {
        // Create verifier
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secretKey_})
            .with_issuer(issuer_);

        // Decode and verify token
        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        // Extract claims
        JwtClaims claims;
        claims.userId = decoded.get_subject();
        claims.username = decoded.get_payload_claim("username").as_string();
        claims.isAdmin = decoded.get_payload_claim("isAdmin").as_bool();
        claims.exp = decoded.get_expires_at();
        claims.iat = decoded.get_issued_at();

        // Extract permissions array
        auto permsJson = decoded.get_payload_claim("permissions");
        if (permsJson.get_type() == jwt::json::type::array) {
            auto permsArray = permsJson.as_array();
            for (const auto& perm : permsArray) {
                if (perm.is<std::string>()) {
                    claims.permissions.push_back(perm.get<std::string>());
                }
            }
        }

        spdlog::debug("[JwtService] Token validated for user={}, permissions={}",
                      claims.username, claims.permissions.size());

        return claims;

    } catch (const jwt::token_verification_exception& exc) {
        spdlog::warn("[JwtService] Token verification failed: {}", exc.what());
        return std::nullopt;
    } catch (const std::exception& exc) {
        spdlog::error("[JwtService] Token validation error: {}", exc.what());
        return std::nullopt;
    }
}

std::string JwtService::refreshToken(const std::string& token) {
    auto claims = validateToken(token);
    if (!claims) {
        spdlog::warn("[JwtService] Cannot refresh invalid token");
        return "";
    }

    try {
        std::string newToken = generateToken(
            claims->userId,
            claims->username,
            claims->permissions,
            claims->isAdmin);

        spdlog::info("[JwtService] Token refreshed for user={}", claims->username);
        return newToken;

    } catch (const std::exception& exc) {
        spdlog::error("[JwtService] Token refresh failed: {}", exc.what());
        return "";
    }
}

bool JwtService::isTokenExpired(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        auto exp = decoded.get_expires_at();
        auto now = std::chrono::system_clock::now();

        return now >= exp;

    } catch (const std::exception& exc) {
        spdlog::error("[JwtService] Failed to check token expiration: {}", exc.what());
        return true; // Treat errors as expired
    }
}

} // namespace auth

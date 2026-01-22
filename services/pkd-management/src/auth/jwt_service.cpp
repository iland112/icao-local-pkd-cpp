#include "jwt_service.h"
#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

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

    // Build permissions array for JWT claims using nlohmann/json
    json permsArray = json::array();
    for (const auto& perm : permissions) {
        permsArray.push_back(perm);
    }

    try {
        auto token = jwt::create<jwt::traits::nlohmann_json>()
            .set_issuer(issuer_)
            .set_type("JWT")
            .set_issued_at(now)
            .set_expires_at(exp)
            .set_subject(userId)
            .set_payload_claim("username", jwt::claim(std::string(username)))
            .set_payload_claim("permissions", jwt::claim(permsArray.dump()))
            .set_payload_claim("isAdmin", jwt::claim(std::string(isAdmin ? "true" : "false")))
            .sign(jwt::algorithm::hs256{secretKey_});

        spdlog::debug("[JwtService] Generated token for user={}, isAdmin={}, permissions={}",
                      username, isAdmin, permissions.size());

        return token;

    } catch (const std::exception& e) {
        spdlog::error("[JwtService] Token generation failed: {}", e.what());
        throw;
    }
}

std::optional<JwtClaims> JwtService::validateToken(const std::string& token) {
    try {
        // Create verifier
        auto verifier = jwt::verify<jwt::default_clock, jwt::traits::nlohmann_json>()
            .allow_algorithm(jwt::algorithm::hs256{secretKey_})
            .with_issuer(issuer_);

        // Decode and verify token using nlohmann/json traits
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
        verifier.verify(decoded);

        // Extract claims
        JwtClaims claims;
        claims.userId = decoded.get_subject();
        claims.username = decoded.get_payload_claim("username").as_string();

        // Parse isAdmin (stored as string "true" or "false")
        std::string isAdminStr = decoded.get_payload_claim("isAdmin").as_string();
        claims.isAdmin = (isAdminStr == "true");

        claims.exp = decoded.get_expires_at();
        claims.iat = decoded.get_issued_at();

        // Extract permissions array (stored as JSON string)
        std::string permsJsonStr = decoded.get_payload_claim("permissions").as_string();
        try {
            auto permsJson = json::parse(permsJsonStr);
            if (permsJson.is_array()) {
                for (const auto& perm : permsJson) {
                    if (perm.is_string()) {
                        claims.permissions.push_back(perm.get<std::string>());
                    }
                }
            }
        } catch (const json::exception& e) {
            spdlog::warn("[JwtService] Failed to parse permissions JSON: {}", e.what());
        }

        spdlog::debug("[JwtService] Token validated for user={}, permissions={}",
                      claims.username, claims.permissions.size());

        return claims;

    } catch (const jwt::token_verification_exception& e) {
        spdlog::warn("[JwtService] Token verification failed: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("[JwtService] Token validation error: {}", e.what());
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

    } catch (const std::exception& e) {
        spdlog::error("[JwtService] Token refresh failed: {}", e.what());
        return "";
    }
}

bool JwtService::isTokenExpired(const std::string& token) {
    try {
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
        auto exp = decoded.get_expires_at();
        auto now = std::chrono::system_clock::now();

        return now >= exp;

    } catch (const std::exception& e) {
        spdlog::error("[JwtService] Failed to check token expiration: {}", e.what());
        return true; // Treat errors as expired
    }
}

} // namespace auth

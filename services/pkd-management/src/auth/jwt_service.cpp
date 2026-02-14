/** @file jwt_service.cpp
 *  @brief JwtService implementation (HS256 JWT generation and validation)
 */

#include "jwt_service.h"
#include <spdlog/spdlog.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace auth {

// Base64URL encoding (RFC 4648 Section 5)
static std::string base64UrlEncode(const unsigned char* data, size_t length) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string result;
    result.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        unsigned int b = (data[i] & 0xFC) >> 2;
        result += base64_chars[b];

        b = (data[i] & 0x03) << 4;
        if (i + 1 < length) {
            b |= (data[i + 1] & 0xF0) >> 4;
            result += base64_chars[b];
            b = (data[i + 1] & 0x0F) << 2;
            if (i + 2 < length) {
                b |= (data[i + 2] & 0xC0) >> 6;
                result += base64_chars[b];
                b = data[i + 2] & 0x3F;
                result += base64_chars[b];
            } else {
                result += base64_chars[b];
                result += '=';
            }
        } else {
            result += base64_chars[b];
            result += "==";
        }
    }

    // Convert to Base64URL (replace + with -, / with _, remove =)
    for (char& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    result.erase(std::remove(result.begin(), result.end(), '='), result.end());

    return result;
}

static std::string base64UrlEncode(const std::string& str) {
    return base64UrlEncode(reinterpret_cast<const unsigned char*>(str.data()), str.length());
}

// Base64URL decoding
static std::string base64UrlDecode(const std::string& input) {
    static const unsigned char base64_table[256] = {
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 62, 64, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
        64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
        64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
    };

    std::string b64 = input;
    // Convert Base64URL to Base64
    for (char& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }

    // Add padding
    while (b64.length() % 4) {
        b64 += '=';
    }

    std::string result;
    result.reserve(b64.length() * 3 / 4);

    unsigned int val = 0;
    int valb = -8;
    for (unsigned char c : b64) {
        if (base64_table[c] == 64) break;
        val = (val << 6) + base64_table[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return result;
}

// HMAC-SHA256 signing
static std::string hmacSha256(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    HMAC(EVP_sha256(),
         key.data(), key.length(),
         reinterpret_cast<const unsigned char*>(data.data()), data.length(),
         hash, &hashLen);

    return std::string(reinterpret_cast<char*>(hash), hashLen);
}

// Helper: Join permissions vector to comma-separated string
static std::string joinPermissions(const std::vector<std::string>& permissions) {
    std::ostringstream oss;
    for (size_t i = 0; i < permissions.size(); ++i) {
        oss << permissions[i];
        if (i < permissions.size() - 1) {
            oss << ",";
        }
    }
    return oss.str();
}

// Helper: Split comma-separated string to permissions vector
static std::vector<std::string> splitPermissions(const std::string& permsStr) {
    std::vector<std::string> permissions;
    if (permsStr.empty()) {
        return permissions;
    }

    std::istringstream iss(permsStr);
    std::string perm;
    while (std::getline(iss, perm, ',')) {
        if (!perm.empty()) {
            permissions.push_back(perm);
        }
    }
    return permissions;
}

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
    auto iat = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto exp = iat + expirationSeconds_;

    // Build JWT header
    std::ostringstream header;
    header << "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";

    // Build JWT payload
    std::ostringstream payload;
    payload << "{"
            << "\"iss\":\"" << issuer_ << "\","
            << "\"sub\":\"" << userId << "\","
            << "\"iat\":" << iat << ","
            << "\"exp\":" << exp << ","
            << "\"username\":\"" << username << "\","
            << "\"permissions\":\"" << joinPermissions(permissions) << "\","
            << "\"isAdmin\":" << (isAdmin ? "true" : "false")
            << "}";

    // Encode header and payload
    std::string encodedHeader = base64UrlEncode(header.str());
    std::string encodedPayload = base64UrlEncode(payload.str());

    // Create signature
    std::string message = encodedHeader + "." + encodedPayload;
    std::string signature = hmacSha256(secretKey_, message);
    std::string encodedSignature = base64UrlEncode(
        reinterpret_cast<const unsigned char*>(signature.data()), signature.length());

    // Build final JWT
    std::string token = message + "." + encodedSignature;

    spdlog::debug("[JwtService] Generated token for user={}, isAdmin={}, permissions={}",
                  username, isAdmin, permissions.size());

    return token;
}

std::optional<JwtClaims> JwtService::validateToken(const std::string& token) {
    try {
        // Split token into parts
        size_t firstDot = token.find('.');
        size_t secondDot = token.find('.', firstDot + 1);

        if (firstDot == std::string::npos || secondDot == std::string::npos) {
            spdlog::warn("[JwtService] Invalid token format");
            return std::nullopt;
        }

        std::string encodedHeader = token.substr(0, firstDot);
        std::string encodedPayload = token.substr(firstDot + 1, secondDot - firstDot - 1);
        std::string encodedSignature = token.substr(secondDot + 1);

        // Verify signature
        std::string message = encodedHeader + "." + encodedPayload;
        std::string expectedSignature = hmacSha256(secretKey_, message);
        std::string expectedEncoded = base64UrlEncode(
            reinterpret_cast<const unsigned char*>(expectedSignature.data()),
            expectedSignature.length());

        if (encodedSignature != expectedEncoded) {
            spdlog::warn("[JwtService] Invalid signature");
            return std::nullopt;
        }

        // Decode payload
        std::string payloadJson = base64UrlDecode(encodedPayload);

        // Parse JSON manually (simple parser for our specific format)
        JwtClaims claims;

        // Extract issuer
        size_t issPos = payloadJson.find("\"iss\":\"");
        if (issPos != std::string::npos) {
            size_t start = issPos + 7;
            size_t end = payloadJson.find("\"", start);
            std::string iss = payloadJson.substr(start, end - start);
            if (iss != issuer_) {
                spdlog::warn("[JwtService] Invalid issuer: {} (expected: {})", iss, issuer_);
                return std::nullopt;
            }
        }

        // Extract subject (userId)
        size_t subPos = payloadJson.find("\"sub\":\"");
        if (subPos != std::string::npos) {
            size_t start = subPos + 7;
            size_t end = payloadJson.find("\"", start);
            claims.userId = payloadJson.substr(start, end - start);
        }

        // Extract username
        size_t userPos = payloadJson.find("\"username\":\"");
        if (userPos != std::string::npos) {
            size_t start = userPos + 12;
            size_t end = payloadJson.find("\"", start);
            claims.username = payloadJson.substr(start, end - start);
        }

        // Extract permissions
        size_t permsPos = payloadJson.find("\"permissions\":\"");
        if (permsPos != std::string::npos) {
            size_t start = permsPos + 15;
            size_t end = payloadJson.find("\"", start);
            std::string permsStr = payloadJson.substr(start, end - start);
            claims.permissions = splitPermissions(permsStr);
        }

        // Extract isAdmin
        size_t adminPos = payloadJson.find("\"isAdmin\":");
        if (adminPos != std::string::npos) {
            size_t start = adminPos + 10;
            claims.isAdmin = (payloadJson.substr(start, 4) == "true");
        }

        // Extract exp
        size_t expPos = payloadJson.find("\"exp\":");
        if (expPos != std::string::npos) {
            size_t start = expPos + 6;
            size_t end = payloadJson.find_first_of(",}", start);
            long expSec = std::stol(payloadJson.substr(start, end - start));
            claims.exp = std::chrono::system_clock::from_time_t(expSec);

            // Check expiration
            auto now = std::chrono::system_clock::now();
            if (now >= claims.exp) {
                spdlog::warn("[JwtService] Token expired");
                return std::nullopt;
            }
        }

        // Extract iat
        size_t iatPos = payloadJson.find("\"iat\":");
        if (iatPos != std::string::npos) {
            size_t start = iatPos + 6;
            size_t end = payloadJson.find_first_of(",}", start);
            long iatSec = std::stol(payloadJson.substr(start, end - start));
            claims.iat = std::chrono::system_clock::from_time_t(iatSec);
        }

        spdlog::debug("[JwtService] Token validated for user={}, permissions={}",
                      claims.username, claims.permissions.size());

        return claims;

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
    auto claims = validateToken(token);
    return !claims.has_value();
}

} // namespace auth

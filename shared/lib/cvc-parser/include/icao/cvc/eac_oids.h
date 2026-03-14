#pragma once

/**
 * @file eac_oids.h
 * @brief BSI TR-03110 OID constants and algorithm mappings for EAC PKI
 *
 * Reference: BSI TR-03110 Part 3, Appendix A (OID definitions)
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace icao::cvc {

// =============================================================================
// BSI TR-03110 OID Definitions (DER-encoded byte sequences)
// =============================================================================

// BSI base OID: 0.4.0.127.0.7 → {04 00 7F 00 07}
// id-TA: 0.4.0.127.0.7.2.2.2 (Terminal Authentication)
// id-roles: 0.4.0.127.0.7.3.1.2 (Certificate holder roles)

namespace oid {

// --- Terminal Authentication Signature Algorithms ---
// id-TA: 0.4.0.127.0.7.2.2.2.{n}

inline constexpr std::string_view TA_RSA_V1_5_SHA_1     = "0.4.0.127.0.7.2.2.2.1";
inline constexpr std::string_view TA_RSA_V1_5_SHA_256   = "0.4.0.127.0.7.2.2.2.2";
inline constexpr std::string_view TA_RSA_PSS_SHA_1      = "0.4.0.127.0.7.2.2.2.3";
inline constexpr std::string_view TA_RSA_PSS_SHA_256    = "0.4.0.127.0.7.2.2.2.4";
inline constexpr std::string_view TA_ECDSA_SHA_1        = "0.4.0.127.0.7.2.2.2.5";
inline constexpr std::string_view TA_ECDSA_SHA_224      = "0.4.0.127.0.7.2.2.2.6";
inline constexpr std::string_view TA_ECDSA_SHA_256      = "0.4.0.127.0.7.2.2.2.7";
inline constexpr std::string_view TA_ECDSA_SHA_384      = "0.4.0.127.0.7.2.2.2.8";
inline constexpr std::string_view TA_ECDSA_SHA_512      = "0.4.0.127.0.7.2.2.2.9";

// --- Certificate Holder Roles ---
// id-roles: 0.4.0.127.0.7.3.1.2.{n}

inline constexpr std::string_view ROLE_IS = "0.4.0.127.0.7.3.1.2.1";  // Inspection System
inline constexpr std::string_view ROLE_AT = "0.4.0.127.0.7.3.1.2.2";  // Authentication Terminal
inline constexpr std::string_view ROLE_ST = "0.4.0.127.0.7.3.1.2.3";  // Signature Terminal

} // namespace oid

// =============================================================================
// CVC TLV Tag Constants (ISO 7816-4 / BSI TR-03110 Part 3, Appendix C)
// =============================================================================

namespace tag {

// Multi-byte tags (2 bytes)
inline constexpr uint16_t CV_CERTIFICATE             = 0x7F21;
inline constexpr uint16_t CERTIFICATE_BODY           = 0x7F4E;
inline constexpr uint16_t PUBLIC_KEY                  = 0x7F49;
inline constexpr uint16_t CHAT                       = 0x7F4C;

// Single-byte tags
inline constexpr uint16_t CERTIFICATE_PROFILE_ID     = 0x5F29;
inline constexpr uint16_t CAR                        = 0x42;    // Certification Authority Reference
inline constexpr uint16_t CHR                        = 0x5F20;  // Certificate Holder Reference
inline constexpr uint16_t EFFECTIVE_DATE             = 0x5F25;
inline constexpr uint16_t EXPIRATION_DATE            = 0x5F24;
inline constexpr uint16_t SIGNATURE                  = 0x5F37;
inline constexpr uint16_t OID                        = 0x06;
inline constexpr uint16_t DISCRETIONARY_DATA         = 0x53;

// Public Key component tags
inline constexpr uint16_t PK_MODULUS                 = 0x81;    // RSA modulus / EC prime
inline constexpr uint16_t PK_EXPONENT                = 0x82;    // RSA exponent / EC coeff A
inline constexpr uint16_t PK_COEFF_B                 = 0x83;    // EC coefficient B
inline constexpr uint16_t PK_GENERATOR               = 0x84;    // EC base point G
inline constexpr uint16_t PK_ORDER                   = 0x85;    // EC order
inline constexpr uint16_t PK_PUBLIC_POINT            = 0x86;    // EC public point
inline constexpr uint16_t PK_COFACTOR                = 0x87;    // EC cofactor

} // namespace tag

// =============================================================================
// Algorithm Name Mapping
// =============================================================================

/**
 * @brief Get human-readable algorithm name from OID string
 * @param oid OID in dotted notation (e.g., "0.4.0.127.0.7.2.2.2.7")
 * @return Algorithm name or OID string if unknown
 */
inline std::string getAlgorithmName(std::string_view oidStr) {
    static const std::unordered_map<std::string_view, std::string_view> mapping = {
        {oid::TA_RSA_V1_5_SHA_1,   "id-TA-RSA-v1-5-SHA-1"},
        {oid::TA_RSA_V1_5_SHA_256, "id-TA-RSA-v1-5-SHA-256"},
        {oid::TA_RSA_PSS_SHA_1,    "id-TA-RSA-PSS-SHA-1"},
        {oid::TA_RSA_PSS_SHA_256,  "id-TA-RSA-PSS-SHA-256"},
        {oid::TA_ECDSA_SHA_1,      "id-TA-ECDSA-SHA-1"},
        {oid::TA_ECDSA_SHA_224,    "id-TA-ECDSA-SHA-224"},
        {oid::TA_ECDSA_SHA_256,    "id-TA-ECDSA-SHA-256"},
        {oid::TA_ECDSA_SHA_384,    "id-TA-ECDSA-SHA-384"},
        {oid::TA_ECDSA_SHA_512,    "id-TA-ECDSA-SHA-512"},
    };

    auto it = mapping.find(oidStr);
    if (it != mapping.end()) {
        return std::string(it->second);
    }
    return std::string(oidStr);
}

/**
 * @brief Get role name from CHAT role OID
 * @param oid OID in dotted notation
 * @return Role name or "UNKNOWN"
 */
inline std::string getRoleName(std::string_view oidStr) {
    if (oidStr == oid::ROLE_IS) return "IS";
    if (oidStr == oid::ROLE_AT) return "AT";
    if (oidStr == oid::ROLE_ST) return "ST";
    return "UNKNOWN";
}

/**
 * @brief Check if an algorithm OID is RSA-based
 */
inline bool isRsaAlgorithm(std::string_view oidStr) {
    return oidStr == oid::TA_RSA_V1_5_SHA_1
        || oidStr == oid::TA_RSA_V1_5_SHA_256
        || oidStr == oid::TA_RSA_PSS_SHA_1
        || oidStr == oid::TA_RSA_PSS_SHA_256;
}

/**
 * @brief Check if an algorithm OID is ECDSA-based
 */
inline bool isEcdsaAlgorithm(std::string_view oidStr) {
    return oidStr == oid::TA_ECDSA_SHA_1
        || oidStr == oid::TA_ECDSA_SHA_224
        || oidStr == oid::TA_ECDSA_SHA_256
        || oidStr == oid::TA_ECDSA_SHA_384
        || oidStr == oid::TA_ECDSA_SHA_512;
}

} // namespace icao::cvc

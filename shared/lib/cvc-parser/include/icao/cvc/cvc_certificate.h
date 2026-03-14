#pragma once

/**
 * @file cvc_certificate.h
 * @brief BSI TR-03110 CVC (Card Verifiable Certificate) domain model
 *
 * Reference: BSI TR-03110 Part 3, Appendix C
 */

#include <cstdint>
#include <string>
#include <vector>

namespace icao::cvc {

// =============================================================================
// Enumerations
// =============================================================================

enum class CvcType {
    CVCA,          // Country Verifying CA (root, self-signed)
    DV_DOMESTIC,   // Document Verifier (domestic, same country as CVCA)
    DV_FOREIGN,    // Document Verifier (foreign, different country from CVCA)
    IS,            // Inspection System (terminal)
    UNKNOWN
};

enum class ChatRole {
    IS,       // Inspection System (passport readers)
    AT,       // Authentication Terminal (eID)
    ST,       // Signature Terminal (eSign)
    UNKNOWN
};

// =============================================================================
// CHAT (Certificate Holder Authorization Template)
// =============================================================================

struct ChatInfo {
    ChatRole role = ChatRole::UNKNOWN;
    std::string roleOid;                       // e.g., "0.4.0.127.0.7.3.1.2.1"
    std::vector<uint8_t> authorizationBits;    // Raw authorization bitmask
    std::vector<std::string> permissions;       // Decoded permission names
};

// =============================================================================
// CVC Public Key
// =============================================================================

struct CvcPublicKey {
    std::string algorithmOid;                  // e.g., "0.4.0.127.0.7.2.2.2.7"
    std::string algorithmName;                 // e.g., "id-TA-ECDSA-SHA-256"

    // RSA key components
    std::vector<uint8_t> modulus;               // Tag 0x81
    std::vector<uint8_t> exponent;              // Tag 0x82

    // ECDSA key components (domain parameters)
    std::vector<uint8_t> prime;                 // Tag 0x81 (prime p)
    std::vector<uint8_t> coeffA;                // Tag 0x82 (coefficient a)
    std::vector<uint8_t> coeffB;                // Tag 0x83 (coefficient b)
    std::vector<uint8_t> generator;             // Tag 0x84 (base point G)
    std::vector<uint8_t> order;                 // Tag 0x85 (order n)
    std::vector<uint8_t> publicPoint;           // Tag 0x86 (public key point Q)
    std::vector<uint8_t> cofactor;              // Tag 0x87 (cofactor h)
};

// =============================================================================
// CVC Certificate
// =============================================================================

struct CvcCertificate {
    // Certificate Profile Identifier (Tag 0x5F29, always 0x00)
    uint8_t profileIdentifier = 0x00;

    // Certificate Authority Reference (Tag 0x42)
    // Format: "{country}{holder_mnemonic}{sequence}" (e.g., "DECVCA00001")
    std::string car;

    // Certificate Holder Reference (Tag 0x5F20)
    // Format: "{country}{holder_mnemonic}{sequence}" (e.g., "DEDV000001")
    std::string chr;

    // Derived from CAR/CHR patterns
    CvcType type = CvcType::UNKNOWN;
    std::string countryCode;                   // First 2 chars of CHR

    // CHAT
    ChatInfo chat;

    // Public Key
    CvcPublicKey publicKey;

    // Validity dates (YYMMDD → "YYYY-MM-DD")
    std::string effectiveDate;
    std::string expirationDate;

    // Signature (Tag 0x5F37)
    std::vector<uint8_t> signature;

    // Raw data for verification
    std::vector<uint8_t> bodyRaw;              // Certificate Body [0x7F4E] content
    std::vector<uint8_t> rawBinary;            // Complete CVC binary

    // Computed
    std::string fingerprintSha256;             // SHA-256 of rawBinary (hex)
};

// =============================================================================
// Utility: CvcType to string conversion
// =============================================================================

inline std::string cvcTypeToString(CvcType type) {
    switch (type) {
        case CvcType::CVCA:        return "CVCA";
        case CvcType::DV_DOMESTIC: return "DV_DOMESTIC";
        case CvcType::DV_FOREIGN:  return "DV_FOREIGN";
        case CvcType::IS:          return "IS";
        default:                   return "UNKNOWN";
    }
}

inline CvcType stringToCvcType(const std::string& str) {
    if (str == "CVCA")        return CvcType::CVCA;
    if (str == "DV_DOMESTIC") return CvcType::DV_DOMESTIC;
    if (str == "DV_FOREIGN")  return CvcType::DV_FOREIGN;
    if (str == "IS")          return CvcType::IS;
    return CvcType::UNKNOWN;
}

inline std::string chatRoleToString(ChatRole role) {
    switch (role) {
        case ChatRole::IS: return "IS";
        case ChatRole::AT: return "AT";
        case ChatRole::ST: return "ST";
        default:           return "UNKNOWN";
    }
}

} // namespace icao::cvc

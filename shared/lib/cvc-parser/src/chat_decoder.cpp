/**
 * @file chat_decoder.cpp
 * @brief CHAT bitmask decoder implementation
 */

#include "icao/cvc/chat_decoder.h"
#include "icao/cvc/eac_oids.h"

namespace icao::cvc {

ChatRole ChatDecoder::decodeRole(const std::string& roleOid) {
    if (roleOid == oid::ROLE_IS) return ChatRole::IS;
    if (roleOid == oid::ROLE_AT) return ChatRole::AT;
    if (roleOid == oid::ROLE_ST) return ChatRole::ST;
    return ChatRole::UNKNOWN;
}

std::vector<std::string> ChatDecoder::decodePermissions(ChatRole role,
                                                         const std::vector<uint8_t>& authBits) {
    switch (role) {
        case ChatRole::IS: return decodeIsPermissions(authBits);
        case ChatRole::AT: return decodeAtPermissions(authBits);
        case ChatRole::ST: return decodeStPermissions(authBits);
        default:           return {};
    }
}

std::vector<std::string> ChatDecoder::decodeIsPermissions(const std::vector<uint8_t>& authBits) {
    std::vector<std::string> perms;
    if (authBits.empty()) return perms;

    // IS CHAT: 1 byte, bits in the last byte
    uint8_t b = authBits.back();

    if (b & 0x01) perms.emplace_back("Read DG3 (Fingerprint)");
    if (b & 0x02) perms.emplace_back("Read DG4 (Iris)");

    return perms;
}

std::vector<std::string> ChatDecoder::decodeAtPermissions(const std::vector<uint8_t>& authBits) {
    std::vector<std::string> perms;
    if (authBits.empty()) return perms;

    // AT CHAT: up to 5 bytes (40 bits), interpret as big-endian bitmask
    // We extract individual bits from the authorization bytes
    // The bits are numbered from the rightmost bit of the last byte

    // Combine bytes into a single value (big-endian)
    uint64_t bits = 0;
    for (auto b : authBits) {
        bits = (bits << 8) | b;
    }

    // Number of total bits
    size_t totalBits = authBits.size() * 8;

    // Bit definitions (from LSB of the combined value)
    // Note: The role bits (bits at position totalBits-1 and totalBits-2) are not permissions

    // Right-aligned bit positions for AT (5-byte = 40 bits):
    // Bit 0: Age Verification
    // Bit 1: Community ID Verification
    // Bit 2: Restricted Identification
    // Bit 3: Privileged Terminal
    // Bit 4: CAN allowed
    // Bit 5: PIN Management
    // Bit 6: Install Certificate
    // Bit 7: Install Qualified Certificate

    struct BitDef {
        int bit;
        const char* name;
    };

    // Right-aligned access rights (from BSI TR-03110 Part 3, Table C.4)
    static const BitDef atBits[] = {
        {0,  "Age Verification"},
        {1,  "Community ID Verification"},
        {2,  "Restricted Identification"},
        {3,  "Privileged Terminal"},
        {4,  "CAN allowed"},
        {5,  "PIN Management"},
        {6,  "Install Certificate"},
        {7,  "Install Qualified Certificate"},
        // DG read access (bits 8-28)
        {8,  "Read DG21"},
        {9,  "Read DG20"},
        {10, "Read DG19"},
        {11, "Read DG18"},
        {12, "Read DG17"},
        {13, "Read DG16"},
        {14, "Read DG15"},
        {15, "Read DG14"},
        {16, "Read DG13"},
        {17, "Read DG12"},
        {18, "Read DG11"},
        {19, "Read DG10"},
        {20, "Read DG9"},
        {21, "Read DG8"},
        {22, "Read DG7"},
        {23, "Read DG6"},
        {24, "Read DG5"},
        {25, "Read DG4"},
        {26, "Read DG3"},
        {27, "Read DG2"},
        {28, "Read DG1"},
        // Write access (bits 29-37 if present)
        {29, "Write DG21"},
        {30, "Write DG20"},
        {31, "Write DG19"},
        {32, "Write DG18"},
        {33, "Write DG17"},
    };

    for (const auto& def : atBits) {
        if (def.bit >= static_cast<int>(totalBits)) continue;
        if (bits & (1ULL << def.bit)) {
            perms.emplace_back(def.name);
        }
    }

    return perms;
}

std::vector<std::string> ChatDecoder::decodeStPermissions(const std::vector<uint8_t>& authBits) {
    std::vector<std::string> perms;
    if (authBits.empty()) return perms;

    uint8_t b = authBits.back();

    if (b & 0x01) perms.emplace_back("Generate Electronic Signature");
    if (b & 0x02) perms.emplace_back("Generate Qualified Electronic Signature");

    return perms;
}

} // namespace icao::cvc

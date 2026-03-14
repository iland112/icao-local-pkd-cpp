#pragma once

/**
 * @file chat_decoder.h
 * @brief CHAT (Certificate Holder Authorization Template) bitmask decoder
 *
 * Decodes CHAT role OID and authorization bitmask into human-readable
 * permission lists per BSI TR-03110 Part 3.
 *
 * Reference: BSI TR-03110 Part 3, Section C.4
 */

#include "icao/cvc/cvc_certificate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace icao::cvc {

class ChatDecoder {
public:
    /**
     * @brief Decode role from CHAT OID
     * @param roleOid OID in dotted notation
     * @return ChatRole enum value
     */
    static ChatRole decodeRole(const std::string& roleOid);

    /**
     * @brief Decode authorization bitmask into permission name list
     * @param role ChatRole to determine which permission table to use
     * @param authBits Raw authorization bytes from CHAT Discretionary Data
     * @return List of human-readable permission names
     */
    static std::vector<std::string> decodePermissions(ChatRole role,
                                                       const std::vector<uint8_t>& authBits);

    /**
     * @brief Decode IS (Inspection System) permissions
     *
     * IS CHAT authorization is 1 byte:
     *   Bit 0: Read DG3 (Fingerprint)
     *   Bit 1: Read DG4 (Iris)
     */
    static std::vector<std::string> decodeIsPermissions(const std::vector<uint8_t>& authBits);

    /**
     * @brief Decode AT (Authentication Terminal) permissions
     *
     * AT CHAT authorization is 5 bytes (40 bits):
     *   Bits 0-7:  Read access DG1-DG8
     *   Bits 8-15: Read access DG9-DG16
     *   Bits 16-20: Read access DG17-DG21
     *   Bit 21:    Install Qualified Certificate
     *   Bit 22:    Install Certificate
     *   Bit 23:    PIN Management
     *   Bit 24:    CAN allowed
     *   Bit 25:    Privileged Terminal
     *   Bit 26:    Restricted Identification
     *   Bit 27:    Community ID Verification
     *   Bit 28:    Age Verification
     *   (Bits 29-31: role encoding, 2 bits)
     *   (Bits 32-39: write access, optional)
     */
    static std::vector<std::string> decodeAtPermissions(const std::vector<uint8_t>& authBits);

    /**
     * @brief Decode ST (Signature Terminal) permissions
     *
     * ST CHAT authorization is 1 byte:
     *   Bit 0: Generate Electronic Signature
     *   Bit 1: Generate Qualified Electronic Signature
     */
    static std::vector<std::string> decodeStPermissions(const std::vector<uint8_t>& authBits);
};

} // namespace icao::cvc

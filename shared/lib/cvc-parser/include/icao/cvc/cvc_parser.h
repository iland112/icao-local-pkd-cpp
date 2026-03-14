#pragma once

/**
 * @file cvc_parser.h
 * @brief BSI TR-03110 CVC Certificate parser
 *
 * Parses CVC (Card Verifiable Certificate) binary data into a CvcCertificate model.
 * Supports CVCA, DV, and IS certificate types with RSA and ECDSA public keys.
 *
 * Reference: BSI TR-03110 Part 3, Appendix C
 */

#include "icao/cvc/cvc_certificate.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace icao::cvc {

/**
 * @brief CVC Certificate parser
 */
class CvcParser {
public:
    /**
     * @brief Parse a CVC certificate from binary data
     * @param data Raw CVC binary (starting with tag 0x7F21)
     * @param dataLen Length of data
     * @return Parsed CvcCertificate, or nullopt on parse failure
     */
    static std::optional<CvcCertificate> parse(const uint8_t* data, size_t dataLen);

    /**
     * @brief Parse a CVC certificate from a byte vector
     */
    static std::optional<CvcCertificate> parse(const std::vector<uint8_t>& data);

    /**
     * @brief Compute SHA-256 fingerprint of binary data
     * @param data Raw binary data
     * @param dataLen Length of data
     * @return Hex-encoded SHA-256 hash (64 chars lowercase), or empty on error
     */
    static std::string computeSha256(const uint8_t* data, size_t dataLen);

private:
    /**
     * @brief Parse certificate body fields from body TLV value
     */
    static bool parseBody(const uint8_t* data, size_t dataLen, CvcCertificate& cert);

    /**
     * @brief Parse public key from Public Key TLV (0x7F49)
     */
    static bool parsePublicKey(const uint8_t* data, size_t dataLen, CvcPublicKey& pk);

    /**
     * @brief Parse CHAT from CHAT TLV (0x7F4C)
     */
    static bool parseChat(const uint8_t* data, size_t dataLen, ChatInfo& chat);

    /**
     * @brief Infer CVC type from CAR and CHR patterns
     *
     * Rules:
     * - CAR == CHR → CVCA (self-signed root)
     * - CAR country == CHR country, CHR contains "DV" → DV_DOMESTIC
     * - CAR country != CHR country, CHR contains "DV" → DV_FOREIGN
     * - Otherwise → IS
     */
    static CvcType inferCvcType(const std::string& car, const std::string& chr);
};

} // namespace icao::cvc

#pragma once

/**
 * @file tlv.h
 * @brief Generic TLV (Tag-Length-Value) parser for ISO 7816-4 / BSI TR-03110
 *
 * Parses DER-encoded TLV structures used in CVC certificates.
 * Supports both single-byte and multi-byte tags (up to 2 bytes).
 *
 * Reference: ISO 7816-4, BSI TR-03110 Part 3
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace icao::cvc {

/**
 * @brief A parsed TLV element
 */
struct TlvElement {
    uint16_t tag;                              // Tag (1 or 2 bytes)
    std::vector<uint8_t> value;                // Value bytes
    const uint8_t* valuePtr = nullptr;         // Pointer into original buffer (for nested parsing)
    size_t valueLength = 0;                    // Length of value
    size_t totalLength = 0;                    // Total bytes consumed (tag + length + value)
};

/**
 * @brief Generic TLV parser with boundary safety
 */
class TlvParser {
public:
    /**
     * @brief Parse a single TLV element from a byte buffer
     * @param data Pointer to start of TLV data
     * @param dataLen Available bytes in buffer
     * @return Parsed TLV element, or nullopt on error
     */
    static std::optional<TlvElement> parse(const uint8_t* data, size_t dataLen);

    /**
     * @brief Parse all TLV elements (children) within a constructed TLV value
     * @param data Pointer to the value portion of a constructed TLV
     * @param dataLen Length of the value portion
     * @return Vector of parsed child TLV elements
     */
    static std::vector<TlvElement> parseChildren(const uint8_t* data, size_t dataLen);

    /**
     * @brief Find a child TLV element by tag within a constructed TLV value
     * @param data Pointer to the value portion
     * @param dataLen Length of the value portion
     * @param tag Tag to search for
     * @return Found TLV element, or nullopt if not found
     */
    static std::optional<TlvElement> findTag(const uint8_t* data, size_t dataLen, uint16_t tag);

    /**
     * @brief Decode a DER-encoded OID from TLV value bytes to dotted notation
     * @param oidBytes Raw OID value (without tag and length)
     * @return OID in dotted notation (e.g., "0.4.0.127.0.7.2.2.2.7")
     */
    static std::string decodeOid(const std::vector<uint8_t>& oidBytes);

    /**
     * @brief Decode BCD date bytes (6 bytes: YYMMDD) to ISO date string
     * @param dateBytes 6 bytes of BCD-encoded date
     * @return Date string "YYYY-MM-DD", or empty string on error
     */
    static std::string decodeBcdDate(const std::vector<uint8_t>& dateBytes);

private:
    /**
     * @brief Parse tag field (1 or 2 bytes)
     * @param p Current position pointer (advanced on success)
     * @param end End of buffer
     * @param outTag Parsed tag value
     * @return true on success
     */
    static bool parseTag(const uint8_t*& p, const uint8_t* end, uint16_t& outTag);

    /**
     * @brief Parse length field (DER short/long form)
     * @param p Current position pointer (advanced on success)
     * @param end End of buffer
     * @param outLen Parsed length value
     * @return true on success
     */
    static bool parseLength(const uint8_t*& p, const uint8_t* end, size_t& outLen);
};

} // namespace icao::cvc

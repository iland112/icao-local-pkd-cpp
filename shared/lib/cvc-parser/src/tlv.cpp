/**
 * @file tlv.cpp
 * @brief Generic TLV parser implementation
 */

#include "icao/cvc/tlv.h"

#include <sstream>

namespace icao::cvc {

bool TlvParser::parseTag(const uint8_t*& p, const uint8_t* end, uint16_t& outTag) {
    if (p >= end) return false;

    uint8_t first = *p++;

    // Check if this is a multi-byte tag
    // ISO 7816-4: if lower 5 bits are all 1 (0x1F), the tag continues
    if ((first & 0x1F) == 0x1F) {
        // Two-byte tag
        if (p >= end) return false;
        uint8_t second = *p++;
        outTag = (static_cast<uint16_t>(first) << 8) | second;
    } else {
        outTag = first;
    }
    return true;
}

bool TlvParser::parseLength(const uint8_t*& p, const uint8_t* end, size_t& outLen) {
    if (p >= end) return false;

    uint8_t first = *p++;

    if (!(first & 0x80)) {
        // Short form: single byte (0-127)
        outLen = first;
        return true;
    }

    // Long form: first byte = 0x80 | numBytes
    int numBytes = first & 0x7F;
    if (numBytes == 0 || numBytes > 4) return false;  // Safety: max 4 bytes
    if (p + numBytes > end) return false;

    outLen = 0;
    for (int i = 0; i < numBytes; i++) {
        outLen = (outLen << 8) | *p++;
    }
    return true;
}

std::optional<TlvElement> TlvParser::parse(const uint8_t* data, size_t dataLen) {
    if (!data || dataLen == 0) return std::nullopt;

    const uint8_t* p = data;
    const uint8_t* end = data + dataLen;

    TlvElement elem;

    // Parse tag
    if (!parseTag(p, end, elem.tag)) return std::nullopt;

    // Parse length
    size_t valueLen = 0;
    if (!parseLength(p, end, valueLen)) return std::nullopt;

    // Validate value fits in buffer
    if (p + valueLen > end) return std::nullopt;

    // Store value
    elem.valuePtr = p;
    elem.valueLength = valueLen;
    elem.value.assign(p, p + valueLen);
    elem.totalLength = static_cast<size_t>(p - data) + valueLen;

    return elem;
}

std::vector<TlvElement> TlvParser::parseChildren(const uint8_t* data, size_t dataLen) {
    std::vector<TlvElement> children;
    if (!data || dataLen == 0) return children;

    const uint8_t* p = data;
    const uint8_t* end = data + dataLen;

    while (p < end) {
        size_t remaining = static_cast<size_t>(end - p);
        auto elem = parse(p, remaining);
        if (!elem) break;

        p += elem->totalLength;
        children.push_back(std::move(*elem));
    }

    return children;
}

std::optional<TlvElement> TlvParser::findTag(const uint8_t* data, size_t dataLen, uint16_t tag) {
    auto children = parseChildren(data, dataLen);
    for (auto& child : children) {
        if (child.tag == tag) return child;
    }
    return std::nullopt;
}

std::string TlvParser::decodeOid(const std::vector<uint8_t>& oidBytes) {
    if (oidBytes.empty()) return "";

    std::ostringstream oss;

    // First byte encodes two components: first = byte/40, second = byte%40
    uint8_t first = oidBytes[0] / 40;
    uint8_t second = oidBytes[0] % 40;
    oss << static_cast<int>(first) << "." << static_cast<int>(second);

    // Remaining bytes: base-128 encoding (high bit = continuation)
    size_t i = 1;
    while (i < oidBytes.size()) {
        uint32_t component = 0;
        int byteCount = 0;
        while (i < oidBytes.size()) {
            uint8_t b = oidBytes[i++];
            component = (component << 7) | (b & 0x7F);
            byteCount++;
            if (byteCount > 5) return "";  // Overflow protection
            if (!(b & 0x80)) break;        // Last byte of component
        }
        oss << "." << component;
    }

    return oss.str();
}

std::string TlvParser::decodeBcdDate(const std::vector<uint8_t>& dateBytes) {
    // CVC date: 6 bytes, each byte is a BCD digit (0x00-0x09)
    // Format: YY MM DD
    if (dateBytes.size() != 6) return "";

    // Validate all bytes are valid BCD digits
    for (auto b : dateBytes) {
        if (b > 9) return "";
    }

    int year = dateBytes[0] * 10 + dateBytes[1];
    int month = dateBytes[2] * 10 + dateBytes[3];
    int day = dateBytes[4] * 10 + dateBytes[5];

    // Basic validation
    if (month < 1 || month > 12 || day < 1 || day > 31) return "";

    // Convert 2-digit year: 00-69 → 2000-2069, 70-99 → 1970-1999
    int fullYear = (year < 70) ? 2000 + year : 1900 + year;

    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", fullYear, month, day);
    return std::string(buf);
}

} // namespace icao::cvc

#include "der_parser.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <cstring>

namespace icao {
namespace certificate_parser {

DerParseResult DerParser::parse(const std::vector<uint8_t>& data) {
    DerParseResult result;
    result.fileSize = data.size();

    if (data.empty()) {
        result.errorMessage = "Empty data";
        return result;
    }

    // Validate DER structure
    result.isValidDer = validateDerStructure(data);
    if (!result.isValidDer) {
        result.errorMessage = "Invalid DER structure";
        return result;
    }

    // Parse certificate using OpenSSL
    const unsigned char* dataPtr = data.data();
    X509* cert = d2i_X509(nullptr, &dataPtr, static_cast<long>(data.size()));

    if (!cert) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        result.errorMessage = std::string("Failed to parse DER certificate: ") + errBuf;
        return result;
    }

    result.certificate = cert;
    result.success = true;
    return result;
}

bool DerParser::isDerFormat(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }

    // Check for SEQUENCE tag (0x30)
    if (data[0] != 0x30) {
        return false;
    }

    // Check length encoding
    uint8_t lengthByte = data[1];

    // Short form: 0x00-0x7F
    if (lengthByte <= 0x7F) {
        return data.size() >= static_cast<size_t>(2 + lengthByte);
    }

    // Long form: 0x81-0x84 (1-4 length bytes)
    if (lengthByte >= 0x81 && lengthByte <= 0x84) {
        size_t lengthBytes = lengthByte & 0x7F;
        return data.size() >= (2 + lengthBytes);
    }

    return false;
}

bool DerParser::validateDerStructure(const std::vector<uint8_t>& data) {
    if (!isDerFormat(data)) {
        return false;
    }

    // Get expected certificate size from DER encoding
    size_t certSize = getDerCertificateSize(data);
    if (certSize == 0) {
        return false;
    }

    // Verify file size matches encoded size
    // Allow exact match or slightly larger (trailing data)
    if (data.size() < certSize) {
        return false;
    }

    return true;
}

size_t DerParser::getDerCertificateSize(const std::vector<uint8_t>& data) {
    if (data.size() < 2) {
        return 0;
    }

    // Skip SEQUENCE tag
    if (data[0] != 0x30) {
        return 0;
    }

    size_t lengthFieldSize = 0;
    size_t contentLength = parseDerLength(
        std::vector<uint8_t>(data.begin() + 1, data.end()),
        lengthFieldSize
    );

    if (contentLength == 0) {
        return 0;
    }

    // Total size = tag (1 byte) + length field + content
    return 1 + lengthFieldSize + contentLength;
}

std::vector<uint8_t> DerParser::toDer(X509* cert) {
    std::vector<uint8_t> derData;

    if (!cert) {
        return derData;
    }

    // Get DER encoding length
    int derLength = i2d_X509(cert, nullptr);
    if (derLength <= 0) {
        return derData;
    }

    // Allocate buffer
    derData.resize(derLength);
    unsigned char* dataPtr = derData.data();

    // Encode to DER
    int result = i2d_X509(cert, &dataPtr);
    if (result <= 0) {
        derData.clear();
        return derData;
    }

    return derData;
}

size_t DerParser::parseDerLength(const std::vector<uint8_t>& data, size_t& lengthFieldSize) {
    if (data.empty()) {
        lengthFieldSize = 0;
        return 0;
    }

    uint8_t firstByte = data[0];

    // Short form: 0x00-0x7F (length ≤ 127)
    if (firstByte <= 0x7F) {
        lengthFieldSize = 1;
        return firstByte;
    }

    // Long form: 0x81-0x84 (1-4 length bytes follow)
    if (firstByte < 0x80 || firstByte > 0x84) {
        lengthFieldSize = 0;
        return 0;  // Invalid encoding
    }

    size_t numLengthBytes = firstByte & 0x7F;
    if (data.size() < 1 + numLengthBytes) {
        lengthFieldSize = 0;
        return 0;  // Not enough data
    }

    // Parse length bytes (big-endian)
    size_t length = 0;
    for (size_t i = 0; i < numLengthBytes; i++) {
        length = (length << 8) | data[1 + i];
    }

    lengthFieldSize = 1 + numLengthBytes;
    return length;
}

} // namespace certificate_parser
} // namespace icao

/**
 * @file der_parser.h
 * @brief DER (Distinguished Encoding Rules) format parser for X.509 certificates
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <openssl/x509.h>

namespace icao {
namespace certificate_parser {

/**
 * @brief DER parsing result
 *
 * Contains extracted certificate and metadata from a DER file
 */
struct DerParseResult {
    bool success;                           ///< Whether parsing succeeded
    std::string errorMessage;               ///< Error message if failed

    // Extracted certificate
    X509* certificate;                      ///< Extracted X.509 certificate (ownership transferred)

    // File metadata
    size_t fileSize;                        ///< Original file size in bytes
    bool isValidDer;                        ///< Whether file has valid DER structure

    DerParseResult()
        : success(false), certificate(nullptr), fileSize(0), isValidDer(false) {}

    ~DerParseResult() {
        // Free OpenSSL resources
        if (certificate) {
            X509_free(certificate);
        }
    }

    // Move constructor
    DerParseResult(DerParseResult&& other) noexcept
        : success(other.success)
        , errorMessage(std::move(other.errorMessage))
        , certificate(other.certificate)
        , fileSize(other.fileSize)
        , isValidDer(other.isValidDer)
    {
        other.certificate = nullptr;
    }

    // Delete copy constructor
    DerParseResult(const DerParseResult&) = delete;
    DerParseResult& operator=(const DerParseResult&) = delete;
};

/**
 * @brief DER (Distinguished Encoding Rules) Format Parser
 *
 * Parses DER-encoded certificate files according to ITU-T X.690.
 *
 * DER Format:
 * - Binary format (ASN.1 binary encoding)
 * - Deterministic encoding (unique representation)
 * - Used for digital signatures and certificates
 * - File extensions: .der, .cer (Windows), .bin
 * - No delimiters (entire file is one certificate)
 *
 * DER Structure:
 * - Starts with SEQUENCE tag (0x30)
 * - Followed by length encoding
 * - Then certificate content (TBSCertificate, signatureAlgorithm, signature)
 *
 * Usage Example:
 * @code
 * std::vector<uint8_t> derData = readFile("certificate.der");
 * DerParseResult result = DerParser::parse(derData);
 *
 * if (result.success) {
 *     std::cout << "Parsed certificate from " << result.fileSize << " bytes" << std::endl;
 *     X509* cert = result.certificate;
 *     // Process certificate (do not free - ownership transferred)
 * }
 * @endcode
 */
class DerParser {
public:
    /**
     * @brief Parse DER file content
     *
     * @param data DER file content (binary format)
     * @return DerParseResult with extracted certificate
     *
     * The function:
     * 1. Verifies DER structure (SEQUENCE tag and length)
     * 2. Parses X.509 certificate
     * 3. Returns certificate with metadata
     */
    static DerParseResult parse(const std::vector<uint8_t>& data);

    /**
     * @brief Check if data is DER format
     *
     * @param data File content
     * @return true if DER format detected
     *
     * Checks for:
     * - SEQUENCE tag (0x30)
     * - Valid length encoding (0x81, 0x82, 0x83, 0x84)
     * - Minimum size requirements
     */
    static bool isDerFormat(const std::vector<uint8_t>& data);

    /**
     * @brief Validate DER structure
     *
     * @param data DER-encoded data
     * @return true if valid DER structure
     *
     * Performs structural validation without full parsing:
     * - Tag-Length-Value (TLV) structure
     * - Length encoding consistency
     * - File size matches encoded length
     */
    static bool validateDerStructure(const std::vector<uint8_t>& data);

    /**
     * @brief Get DER certificate size from header
     *
     * @param data DER file content (at least 4 bytes)
     * @return Certificate size in bytes (including header) or 0 on error
     *
     * Extracts size from DER length encoding
     */
    static size_t getDerCertificateSize(const std::vector<uint8_t>& data);

    /**
     * @brief Convert certificate to DER format
     *
     * @param cert X.509 certificate
     * @return DER-encoded bytes
     *
     * Utility function for certificate export
     */
    static std::vector<uint8_t> toDer(X509* cert);

private:
    /**
     * @brief Parse DER length encoding
     *
     * @param data Data starting at length field
     * @param lengthFieldSize Output: number of bytes used for length encoding
     * @return Decoded length value or 0 on error
     *
     * DER length encoding:
     * - Short form: 0x00-0x7F (length ≤ 127)
     * - Long form: 0x81-0x84 (1-4 length bytes follow)
     */
    static size_t parseDerLength(const std::vector<uint8_t>& data, size_t& lengthFieldSize);
};

} // namespace certificate_parser
} // namespace icao

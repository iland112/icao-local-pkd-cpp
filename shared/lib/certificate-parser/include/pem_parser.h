#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <openssl/x509.h>

namespace icao {
namespace certificate_parser {

/**
 * @brief PEM parsing result
 *
 * Contains extracted certificates and metadata from a PEM file
 */
struct PemParseResult {
    bool success;                           ///< Whether parsing succeeded
    std::string errorMessage;               ///< Error message if failed

    // Extracted certificates
    std::vector<X509*> certificates;        ///< Extracted X.509 certificates (ownership transferred)

    // Parsing statistics
    int certificateCount;                   ///< Number of certificates found
    int parseErrors;                        ///< Number of parsing errors

    PemParseResult()
        : success(false), certificateCount(0), parseErrors(0) {}

    ~PemParseResult() {
        // Free OpenSSL resources
        for (auto* cert : certificates) {
            if (cert) {
                X509_free(cert);
            }
        }
    }

    // Move constructor
    PemParseResult(PemParseResult&& other) noexcept
        : success(other.success)
        , errorMessage(std::move(other.errorMessage))
        , certificates(std::move(other.certificates))
        , certificateCount(other.certificateCount)
        , parseErrors(other.parseErrors)
    {
        other.certificates.clear();
    }

    // Delete copy constructor
    PemParseResult(const PemParseResult&) = delete;
    PemParseResult& operator=(const PemParseResult&) = delete;
};

/**
 * @brief PEM (Privacy Enhanced Mail) Format Parser
 *
 * Parses PEM-encoded certificate files according to RFC 7468.
 *
 * PEM Format:
 * - Text-based format with Base64 encoding
 * - Enclosed in "-----BEGIN CERTIFICATE-----" and "-----END CERTIFICATE-----"
 * - Can contain multiple certificates in a single file
 * - May include additional PEM blocks (private keys, CSRs, etc.)
 *
 * Supported PEM Types:
 * - X.509 Certificates (CERTIFICATE)
 * - X.509 Certificate Requests (CERTIFICATE REQUEST)
 * - PKCS#7 Certificates (PKCS7)
 *
 * Usage Example:
 * @code
 * std::vector<uint8_t> pemData = readFile("certificates.pem");
 * PemParseResult result = PemParser::parse(pemData);
 *
 * if (result.success) {
 *     std::cout << "Found " << result.certificateCount << " certificates" << std::endl;
 *     for (X509* cert : result.certificates) {
 *         // Process certificate
 *     }
 * }
 * @endcode
 */
class PemParser {
public:
    /**
     * @brief Parse PEM file content
     *
     * @param data PEM file content (text format)
     * @return PemParseResult with extracted certificates
     *
     * The function:
     * 1. Identifies all PEM blocks (BEGIN/END markers)
     * 2. Extracts certificate blocks only
     * 3. Decodes Base64 content
     * 4. Parses DER-encoded certificates
     * 5. Returns all valid certificates
     */
    static PemParseResult parse(const std::vector<uint8_t>& data);

    /**
     * @brief Parse PEM file content from string
     *
     * @param pemString PEM content as string
     * @return PemParseResult with extracted certificates
     */
    static PemParseResult parse(const std::string& pemString);

    /**
     * @brief Parse single PEM certificate
     *
     * @param data PEM file content (single certificate)
     * @return X509* certificate (caller must free) or nullptr on error
     *
     * For files with multiple certificates, use parse() instead
     */
    static X509* parseSingle(const std::vector<uint8_t>& data);

    /**
     * @brief Check if data is PEM format
     *
     * @param data File content
     * @return true if PEM format detected
     *
     * Looks for "-----BEGIN CERTIFICATE-----" marker
     */
    static bool isPemFormat(const std::vector<uint8_t>& data);

    /**
     * @brief Extract all PEM blocks from content
     *
     * @param content PEM file content
     * @return Vector of PEM blocks (each block is a string)
     *
     * Returns all blocks including non-certificate blocks
     */
    static std::vector<std::string> extractPemBlocks(const std::string& content);

    /**
     * @brief Convert certificate to PEM format
     *
     * @param cert X.509 certificate
     * @return PEM-encoded string
     *
     * Utility function for certificate export
     */
    static std::string toPem(X509* cert);

private:
    /**
     * @brief Parse single PEM block to certificate
     *
     * @param pemBlock PEM block string (with BEGIN/END markers)
     * @return X509* certificate or nullptr on error
     */
    static X509* parsePemBlock(const std::string& pemBlock);

    /**
     * @brief Check if PEM block is a certificate
     *
     * @param block PEM block string
     * @return true if block contains "BEGIN CERTIFICATE"
     */
    static bool isCertificateBlock(const std::string& block);
};

} // namespace certificate_parser
} // namespace icao

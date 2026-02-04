#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace icao {
namespace certificate_parser {

/**
 * @brief Supported file formats for certificate parsing
 */
enum class FileFormat {
    UNKNOWN,     ///< Unknown or unsupported format
    PEM,         ///< PEM format (text-based, Base64 encoded)
    DER,         ///< DER format (binary ASN.1 encoding)
    CER,         ///< CER format (Windows convention for DER)
    BIN,         ///< Generic binary format
    DVL,         ///< Deviation List (CMS SignedData)
    LDIF,        ///< LDAP Data Interchange Format
    ML           ///< Master List (CMS SignedData)
};

/**
 * @brief File format detector using extension and content analysis
 *
 * This class provides static methods to automatically detect certificate
 * file formats based on filename extensions and file content headers.
 *
 * Detection Strategy:
 * 1. Extension-based detection (fast, first priority)
 * 2. Content-based detection (fallback, more accurate)
 *
 * Usage Example:
 * @code
 * std::vector<uint8_t> content = readFile("cert.pem");
 * FileFormat format = FileDetector::detectFormat("cert.pem", content);
 * if (format == FileFormat::PEM) {
 *     // Parse as PEM
 * }
 * @endcode
 */
class FileDetector {
public:
    /**
     * @brief Detect file format from filename and content
     *
     * @param filename Original filename (used for extension detection)
     * @param content File content (first 512 bytes sufficient)
     * @return Detected file format
     *
     * Detection order:
     * 1. Check filename extension
     * 2. If extension unknown, check content headers
     * 3. Return UNKNOWN if both fail
     */
    static FileFormat detectFormat(
        const std::string& filename,
        const std::vector<uint8_t>& content
    );

    /**
     * @brief Convert FileFormat enum to string for database storage
     *
     * @param format File format enum
     * @return String representation (e.g., "PEM", "DER", "DVL")
     */
    static std::string formatToString(FileFormat format);

    /**
     * @brief Convert string to FileFormat enum
     *
     * @param str String representation (e.g., "PEM", "DER")
     * @return FileFormat enum
     */
    static FileFormat stringToFormat(const std::string& str);

private:
    /**
     * @brief Detect format by filename extension
     *
     * Extensions:
     * - .pem, .crt → PEM
     * - .der → DER
     * - .cer → CER
     * - .bin → BIN
     * - .dvl → DVL
     * - .ldif → LDIF
     * - .ml → ML
     */
    static FileFormat detectByExtension(const std::string& filename);

    /**
     * @brief Detect format by content headers
     *
     * Headers:
     * - "-----BEGIN" → PEM
     * - 0x30 0x82 or 0x30 0x81 → DER/CER/BIN
     * - PKCS#7 with OID 2.23.136.1.1.7 → DVL
     * - PKCS#7 with OID 2.23.136.1.1.2 → ML
     * - "dn:" or "version:" → LDIF
     */
    static FileFormat detectByContent(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content starts with PEM header
     *
     * Looks for "-----BEGIN CERTIFICATE-----" or similar
     */
    static bool isPEM(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content is DER-encoded ASN.1
     *
     * DER format starts with:
     * - 0x30 (SEQUENCE tag)
     * - Length encoding (0x81, 0x82, 0x83, 0x84)
     */
    static bool isDER(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content is Deviation List (CMS SignedData)
     *
     * DVL contains:
     * - PKCS#7 SignedData structure
     * - OID 2.23.136.1.1.7 (ICAO deviationList)
     */
    static bool isDVL(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content is Master List (CMS SignedData)
     *
     * ML contains:
     * - PKCS#7 SignedData structure
     * - OID 2.23.136.1.1.2 (ICAO cscaMasterList)
     */
    static bool isMasterList(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content is LDIF format
     *
     * LDIF starts with:
     * - "dn:" or "version:"
     */
    static bool isLDIF(const std::vector<uint8_t>& content);

    /**
     * @brief Convert filename to lowercase for case-insensitive comparison
     */
    static std::string toLower(const std::string& str);

    /**
     * @brief Extract file extension from filename
     *
     * @return Extension including dot (e.g., ".pem")
     */
    static std::string getExtension(const std::string& filename);
};

} // namespace certificate_parser
} // namespace icao

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
    DL,          ///< Document List / Deviation List (CMS SignedData)
    LDIF,        ///< LDAP Data Interchange Format
    ML,          ///< Master List (CMS SignedData)
    P7B,         ///< PKCS#7 certificate bundle (CMS SignedData without ICAO OID)
    CRL          ///< Certificate Revocation List (DER or PEM encoded)
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
     * @return String representation (e.g., "PEM", "DER", "DL")
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
     * - .dvl, .dl → DL
     * - .ldif → LDIF
     * - .ml → ML
     * - .p7b, .p7c → P7B
     * - .crl → CRL
     */
    static FileFormat detectByExtension(const std::string& filename);

    /**
     * @brief Detect format by content headers
     *
     * Headers:
     * - "-----BEGIN" → PEM
     * - 0x30 0x82 or 0x30 0x81 → DER/CER/BIN
     * - PKCS#7 with OID 2.23.136.1.1.7 → DL
     * - PKCS#7 with OID 2.23.136.1.1.2 → ML
     * - PKCS#7 SignedData (no ICAO OID) → P7B
     * - "-----BEGIN X509 CRL-----" → CRL (PEM)
     * - DER with CRL structure → CRL (DER)
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
     * @brief Check if content is Document List / Deviation List (CMS SignedData)
     *
     * DL contains:
     * - PKCS#7 SignedData structure
     * - OID 2.23.136.1.1.7 (ICAO deviationList)
     */
    static bool isDL(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content is Master List (CMS SignedData)
     *
     * ML contains:
     * - PKCS#7 SignedData structure
     * - OID 2.23.136.1.1.2 (ICAO cscaMasterList)
     */
    static bool isMasterList(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content is PKCS#7 bundle (without ICAO-specific OID)
     *
     * P7B contains PKCS#7 SignedData structure but NOT DL or ML OIDs
     */
    static bool isP7B(const std::vector<uint8_t>& content);

    /**
     * @brief Check if content is Certificate Revocation List
     *
     * CRL formats:
     * - PEM: "-----BEGIN X509 CRL-----"
     * - DER: ASN.1 SEQUENCE with CRL-specific structure
     */
    static bool isCRL(const std::vector<uint8_t>& content);

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

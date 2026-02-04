#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <openssl/x509.h>
#include <openssl/pkcs7.h>

namespace icao {
namespace certificate_parser {

/**
 * @brief Deviation List entry structure
 *
 * Represents a single deviation entry from an ICAO Deviation List
 */
struct DeviationEntry {
    std::string issuerCountry;           ///< Country code of certificate issuer
    std::string serialNumber;            ///< Certificate serial number
    std::string deviationReason;         ///< Reason for deviation
    std::optional<std::string> details;  ///< Additional details (optional)
};

/**
 * @brief DVL (Deviation List) parsing result
 *
 * Contains extracted certificates and deviation information from a DVL file
 */
struct DvlParseResult {
    bool success;                           ///< Whether parsing succeeded
    std::string errorMessage;               ///< Error message if failed

    // DVL metadata
    std::string version;                    ///< DVL version
    std::string issuerCountry;              ///< Country issuing the DVL
    std::optional<std::string> issuerOrg;   ///< Issuing organization

    // Signer certificate
    X509* signerCertificate;                ///< DVL signer certificate (ownership transferred)
    bool signatureVerified;                 ///< Whether DVL signature was verified

    // Deviation entries
    std::vector<DeviationEntry> deviations; ///< List of deviation entries

    // Extracted certificates (if any)
    std::vector<X509*> certificates;        ///< Extracted X.509 certificates (ownership transferred)

    DvlParseResult()
        : success(false), signerCertificate(nullptr), signatureVerified(false) {}

    ~DvlParseResult() {
        // Free OpenSSL resources
        if (signerCertificate) {
            X509_free(signerCertificate);
        }
        for (auto* cert : certificates) {
            if (cert) {
                X509_free(cert);
            }
        }
    }

    // Move constructor
    DvlParseResult(DvlParseResult&& other) noexcept
        : success(other.success)
        , errorMessage(std::move(other.errorMessage))
        , version(std::move(other.version))
        , issuerCountry(std::move(other.issuerCountry))
        , issuerOrg(std::move(other.issuerOrg))
        , signerCertificate(other.signerCertificate)
        , signatureVerified(other.signatureVerified)
        , deviations(std::move(other.deviations))
        , certificates(std::move(other.certificates))
    {
        other.signerCertificate = nullptr;
        other.certificates.clear();
    }

    // Delete copy constructor
    DvlParseResult(const DvlParseResult&) = delete;
    DvlParseResult& operator=(const DvlParseResult&) = delete;
};

/**
 * @brief DVL (Deviation List) Parser
 *
 * Parses ICAO Deviation List files (PKCS#7/CMS SignedData format)
 * according to ICAO Doc 9303 Part 12.
 *
 * DVL Structure:
 * - PKCS#7 SignedData with OID 2.23.136.1.1.7 (deviationList)
 * - Signer certificate (DVL signer)
 * - Deviation entries with certificate references
 * - Optional embedded certificates
 *
 * Usage Example:
 * @code
 * std::vector<uint8_t> dvlData = readFile("germany_dvl.bin");
 * DvlParseResult result = DvlParser::parse(dvlData);
 *
 * if (result.success) {
 *     std::cout << "DVL from " << result.issuerCountry << std::endl;
 *     std::cout << "Deviations: " << result.deviations.size() << std::endl;
 *     std::cout << "Certificates: " << result.certificates.size() << std::endl;
 * }
 * @endcode
 */
class DvlParser {
public:
    /**
     * @brief Parse DVL file content
     *
     * @param data DVL file content (DER-encoded PKCS#7/CMS SignedData)
     * @return DvlParseResult with extracted data
     *
     * The function:
     * 1. Verifies PKCS#7 SignedData structure
     * 2. Checks for ICAO DVL OID (2.23.136.1.1.7)
     * 3. Extracts signer certificate
     * 4. Verifies signature (if possible)
     * 5. Parses deviation entries
     * 6. Extracts embedded certificates (if any)
     */
    static DvlParseResult parse(const std::vector<uint8_t>& data);

    /**
     * @brief Verify DVL signature
     *
     * @param p7 PKCS#7 structure
     * @param signerCert Signer certificate
     * @return true if signature is valid
     */
    static bool verifySignature(PKCS7* p7, X509* signerCert);

    /**
     * @brief Extract deviation entries from DVL content
     *
     * @param p7 PKCS#7 structure
     * @return Vector of deviation entries
     *
     * Parses the SignedData content to extract deviation information
     */
    static std::vector<DeviationEntry> extractDeviations(PKCS7* p7);

    /**
     * @brief Check if data contains DVL OID
     *
     * @param data File content
     * @return true if DVL OID (2.23.136.1.1.7) is present
     */
    static bool containsDvlOid(const std::vector<uint8_t>& data);

private:
    /**
     * @brief Extract signer certificate from PKCS#7
     *
     * @param p7 PKCS#7 structure
     * @return Signer certificate (caller must free)
     */
    static X509* extractSignerCertificate(PKCS7* p7);

    /**
     * @brief Extract all embedded certificates from PKCS#7
     *
     * @param p7 PKCS#7 structure
     * @return Vector of certificates (caller must free)
     */
    static std::vector<X509*> extractCertificates(PKCS7* p7);

    /**
     * @brief Extract country code from certificate subject DN
     *
     * @param cert X.509 certificate
     * @return Country code (e.g., "DE" for Germany)
     */
    static std::string getCountryFromCert(X509* cert);

    /**
     * @brief Extract organization from certificate subject DN
     *
     * @param cert X.509 certificate
     * @return Organization name
     */
    static std::string getOrganizationFromCert(X509* cert);
};

} // namespace certificate_parser
} // namespace icao

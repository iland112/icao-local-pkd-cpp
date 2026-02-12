#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <openssl/x509.h>
#include <openssl/cms.h>

namespace icao {
namespace certificate_parser {

/**
 * @brief Deviation entry from an ICAO Deviation List (Doc 9303 Part 12)
 *
 * Each entry identifies a certificate with known defects and describes
 * the specific deviation from the ICAO 9303 standard.
 *
 * ASN.1 structure (simplified):
 *   SignerDeviation ::= SEQUENCE {
 *       signerIdentifier  CertificateIdentifier,
 *       defects           SET OF Defect
 *   }
 *   Defect ::= SEQUENCE {
 *       description    PrintableString OPTIONAL,
 *       defectType     OBJECT IDENTIFIER,
 *       parameters     [0] ANY DEFINED BY defectType OPTIONAL
 *   }
 */
struct DeviationEntry {
    // Target certificate identification (from IssuerAndSerialNumber)
    std::string certificateIssuerDn;        ///< Issuer DN (RFC 2253 format)
    std::string certificateSerialNumber;    ///< Certificate serial number (hex)

    // Defect information
    std::string defectDescription;          ///< Human-readable description
    std::string defectTypeOid;              ///< OID in dotted notation (e.g., "2.23.136.1.1.7.1.2")
    std::string defectCategory;             ///< Category: "CertOrKey", "LDS", "MRZ", "Chip"
    std::vector<uint8_t> defectParameters;  ///< Raw ASN.1 encoded parameters (optional)
};

/**
 * @brief DL (Deviation List) parsing result
 *
 * Contains all extracted data from a DL file:
 * - DL metadata (version, hash algorithm, signing time)
 * - Signer certificate and signature verification
 * - Deviation entries with certificate references
 * - All embedded certificates from CMS wrapper
 */
struct DlParseResult {
    bool success = false;
    std::string errorMessage;

    // DL metadata
    int version = 0;                        ///< DL version (0 = v1)
    std::string hashAlgorithm;              ///< e.g., "sha1", "sha256"
    std::string signingTime;                ///< ISO 8601 timestamp
    std::string issuerCountry;              ///< Country code from signer cert
    std::optional<std::string> issuerOrg;   ///< Issuing organization

    // CMS-level metadata
    int cmsVersion = 0;                     ///< CMS SignedData version
    std::string cmsDigestAlgorithm;         ///< CMS digest algorithm (e.g., "SHA-1")
    std::string cmsSignatureAlgorithm;      ///< Signer signature algorithm
    std::string eContentType;               ///< eContentType OID (should be 2.23.136.1.1.7)

    // Signer certificate
    X509* signerCertificate = nullptr;      ///< DL signer cert (ownership transferred)
    bool signatureVerified = false;         ///< Whether DL signature was verified

    // Deviation entries
    std::vector<DeviationEntry> deviations;

    // All embedded certificates from CMS wrapper
    std::vector<X509*> certificates;        ///< Ownership transferred to caller

    ~DlParseResult() {
        if (signerCertificate) {
            X509_free(signerCertificate);
        }
        for (auto* cert : certificates) {
            if (cert) X509_free(cert);
        }
    }

    // Move constructor
    DlParseResult(DlParseResult&& other) noexcept
        : success(other.success)
        , errorMessage(std::move(other.errorMessage))
        , version(other.version)
        , hashAlgorithm(std::move(other.hashAlgorithm))
        , signingTime(std::move(other.signingTime))
        , issuerCountry(std::move(other.issuerCountry))
        , issuerOrg(std::move(other.issuerOrg))
        , cmsVersion(other.cmsVersion)
        , cmsDigestAlgorithm(std::move(other.cmsDigestAlgorithm))
        , cmsSignatureAlgorithm(std::move(other.cmsSignatureAlgorithm))
        , eContentType(std::move(other.eContentType))
        , signerCertificate(other.signerCertificate)
        , signatureVerified(other.signatureVerified)
        , deviations(std::move(other.deviations))
        , certificates(std::move(other.certificates))
    {
        other.signerCertificate = nullptr;
        other.certificates.clear();
    }

    DlParseResult() = default;
    DlParseResult(const DlParseResult&) = delete;
    DlParseResult& operator=(const DlParseResult&) = delete;
};

/**
 * @brief ICAO Deviation List parser (Doc 9303 Part 12)
 *
 * Parses DL files (CMS SignedData with OID 2.23.136.1.1.7).
 * Uses CMS API (with PKCS7 fallback) to handle CMS v3 structures.
 *
 * Usage:
 * @code
 * std::vector<uint8_t> dlData = readFile("germany_dl.dvl");
 * DlParseResult result = DlParser::parse(dlData);
 * if (result.success) {
 *     for (const auto& dev : result.deviations) {
 *         std::cout << dev.defectDescription << std::endl;
 *     }
 * }
 * @endcode
 */
class DlParser {
public:
    static DlParseResult parse(const std::vector<uint8_t>& data);
    static bool containsDlOid(const std::vector<uint8_t>& data);

private:
    // CMS-based extraction
    static X509* extractSignerCertificateFromCms(CMS_ContentInfo* cms);
    static std::vector<X509*> extractCertificatesFromCms(CMS_ContentInfo* cms);
    static std::vector<DeviationEntry> extractDeviationsFromCms(CMS_ContentInfo* cms);
    static std::string extractSigningTime(CMS_ContentInfo* cms);
    static bool verifyCmsSignature(CMS_ContentInfo* cms, X509* signerCert);

    // eContent metadata extraction
    struct ContentMetadata {
        int version = 0;
        std::string hashAlgorithm;
    };
    static ContentMetadata extractContentMetadata(CMS_ContentInfo* cms);

    // ASN.1 parsing helpers
    static std::string getCountryFromCert(X509* cert);
    static std::string getOrganizationFromCert(X509* cert);
    static std::string classifyDeviationOid(const std::string& oid);
    static std::string oidToAlgorithmName(const std::string& oid);
    static std::string x509NameToString(X509_NAME* name);
};

} // namespace certificate_parser
} // namespace icao

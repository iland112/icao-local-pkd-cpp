#pragma once

#include <string>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace icao {
namespace certificate_parser {

/**
 * @brief Certificate types supported by ICAO PKD
 */
enum class CertificateType {
    UNKNOWN,        ///< Unknown certificate type
    CSCA,           ///< Country Signing CA (Root CA)
    DSC,            ///< Document Signer Certificate
    DSC_NC,         ///< Non-Conformant Document Signer Certificate
    MLSC,           ///< Master List Signer Certificate
    LINK_CERT,      ///< Link Certificate (Intermediate CSCA)
    DL_SIGNER       ///< Document List / Deviation List Signer Certificate
};

/**
 * @brief Comprehensive certificate information
 */
struct CertificateInfo {
    CertificateType type;           ///< Detected certificate type
    std::string country;            ///< ISO 3166-1 alpha-2 country code
    std::string fingerprint;        ///< SHA-256 fingerprint (hex)
    std::string subject_dn;         ///< Subject Distinguished Name
    std::string issuer_dn;          ///< Issuer Distinguished Name
    bool is_self_signed;            ///< True if issuer == subject
    bool is_ca;                     ///< True if Basic Constraints CA=TRUE
    bool has_key_cert_sign;         ///< True if Key Usage has keyCertSign
    std::string error_message;      ///< Error message if detection failed
};

/**
 * @brief Certificate type detector using X.509 attributes
 *
 * This class analyzes X.509 certificate extensions and attributes
 * to automatically determine the certificate type according to
 * ICAO Doc 9303 Part 12 specifications.
 *
 * Detection Algorithm:
 * 1. Check Extended Key Usage for MLSC/DL Signer OIDs
 * 2. Check Basic Constraints (CA flag)
 * 3. Check Key Usage (keyCertSign)
 * 4. Check if self-signed (Issuer DN == Subject DN)
 * 5. Default to DSC if non-CA
 *
 * Usage Example:
 * @code
 * X509* cert = d2i_X509_bio(bio, nullptr);
 * CertificateInfo info = CertTypeDetector::detectType(cert);
 * std::cout << "Type: " << CertTypeDetector::typeToString(info.type) << std::endl;
 * X509_free(cert);
 * @endcode
 */
class CertTypeDetector {
public:
    /**
     * @brief Detect certificate type from X.509 certificate
     *
     * @param cert OpenSSL X509 certificate pointer
     * @return Complete certificate information
     *
     * Note: Caller is responsible for X509_free(cert)
     */
    static CertificateInfo detectType(X509* cert);

    /**
     * @brief Convert CertificateType enum to string
     *
     * @param type Certificate type enum
     * @return String representation (e.g., "CSCA", "DSC", "MLSC")
     */
    static std::string typeToString(CertificateType type);

    /**
     * @brief Convert string to CertificateType enum
     *
     * @param str String representation
     * @return CertificateType enum
     */
    static CertificateType stringToType(const std::string& str);

    /**
     * @brief Check if certificate is Master List Signer Certificate
     *
     * Checks for Extended Key Usage OID:
     * 2.23.136.1.1.9 (id-icao-mrtd-security-masterListSigner)
     *
     * @param cert X509 certificate
     * @return True if MLSC
     */
    static bool isMasterListSigner(X509* cert);

    /**
     * @brief Check if certificate is Deviation List Signer
     *
     * Checks for Extended Key Usage OID:
     * 2.23.136.1.1.10 (id-icao-mrtd-security-deviationListSigner)
     *
     * @param cert X509 certificate
     * @return True if DL Signer
     */
    static bool isDeviationListSigner(X509* cert);

    /**
     * @brief Check if certificate has Document Signer extended key usage
     *
     * Checks for Extended Key Usage OID:
     * 2.23.136.1.1.6 (id-icao-mrtd-security-aaProtocol)
     *
     * @param cert X509 certificate
     * @return True if has document signer EKU
     */
    static bool isDocumentSigner(X509* cert);

private:
    /**
     * @brief Check if certificate is a CA
     *
     * Checks Basic Constraints extension: CA=TRUE
     */
    static bool isCA(X509* cert);

    /**
     * @brief Check if certificate is self-signed
     *
     * Compares issuer DN with subject DN
     */
    static bool isSelfSigned(X509* cert);

    /**
     * @brief Extract country code from Subject DN
     *
     * Looks for C= component in DN
     *
     * @return ISO 3166-1 alpha-2 country code or empty string
     */
    static std::string extractCountry(X509* cert);

    /**
     * @brief Check if certificate has keyCertSign key usage
     *
     * Checks Key Usage extension for keyCertSign bit (bit 5)
     */
    static bool hasKeyCertSign(X509* cert);

    /**
     * @brief Calculate SHA-256 fingerprint of certificate
     *
     * @return Hex-encoded fingerprint (64 characters)
     */
    static std::string calculateFingerprint(X509* cert);

    /**
     * @brief Convert X509_NAME to string (RFC2253 format)
     *
     * @param name X509_NAME pointer
     * @return DN string (e.g., "CN=Test,O=Org,C=US")
     */
    static std::string nameToString(X509_NAME* name);

    /**
     * @brief Check if certificate has specific Extended Key Usage OID
     *
     * @param cert X509 certificate
     * @param oid_str OID string (e.g., "2.23.136.1.1.9")
     * @return True if OID present in EKU extension
     */
    static bool hasExtendedKeyUsage(X509* cert, const std::string& oid_str);
};

} // namespace certificate_parser
} // namespace icao

/**
 * @file certificate_parser.h
 * @brief Certificate format detection and parsing
 *
 * Handles multiple certificate formats: PEM, DER, CER, BIN, CMS SignedData.
 * Provides unified interface for certificate loading.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <openssl/x509.h>

namespace icao {
namespace x509 {

/**
 * @brief Certificate format enumeration
 */
enum class CertificateFormat {
    UNKNOWN,        ///< Format not recognized
    PEM,            ///< Base64-encoded with BEGIN/END markers
    DER,            ///< Binary DER encoding
    CER,            ///< Same as DER (different extension)
    BIN,            ///< Raw binary certificate
    CMS,            ///< CMS/PKCS7 SignedData container
    LDIF            ///< LDIF format (for Master Lists)
};

/**
 * @brief Certificate format detection result
 */
struct FormatDetectionResult {
    CertificateFormat format;
    std::string formatName;      ///< Human-readable format name
    bool isBinary;               ///< true for DER/CER/BIN/CMS
    size_t dataSize;             ///< Size of certificate data in bytes
    std::optional<std::string> error;  ///< Error message if detection failed
};

/**
 * @brief Detect certificate format from data
 *
 * Analyzes first bytes of data to determine format.
 *
 * @param data Certificate data (raw bytes)
 * @return FormatDetectionResult with detected format
 *
 * @example
 * std::vector<uint8_t> data = readFile("cert.pem");
 * auto result = detectCertificateFormat(data);
 * if (result.format == CertificateFormat::PEM) {
 *     // Handle PEM
 * }
 */
FormatDetectionResult detectCertificateFormat(const std::vector<uint8_t>& data);

/**
 * @brief Parse certificate from binary data
 *
 * Automatically detects format and parses certificate.
 * Handles PEM, DER, CER, BIN formats.
 *
 * @param data Certificate data
 * @return X509 certificate (caller must free with X509_free),
 *         or nullptr on error
 *
 * @warning Caller is responsible for freeing returned X509
 */
X509* parseCertificate(const std::vector<uint8_t>& data);

/**
 * @brief Parse certificate from PEM string
 *
 * @param pem PEM-encoded certificate
 * @return X509 certificate, or nullptr on error
 */
X509* parseCertificateFromPem(const std::string& pem);

/**
 * @brief Parse certificate from DER/BIN data
 *
 * @param der DER-encoded certificate
 * @return X509 certificate, or nullptr on error
 */
X509* parseCertificateFromDer(const std::vector<uint8_t>& der);

/**
 * @brief Extract certificates from CMS/PKCS7 SignedData
 *
 * Used for Master List processing where certificates are embedded
 * in CMS SignedData structure.
 *
 * @param cmsData CMS/PKCS7 SignedData
 * @return Vector of X509 certificates (caller must free each with X509_free)
 *
 * @warning Caller is responsible for freeing all returned X509 certificates
 */
std::vector<X509*> extractCertificatesFromCms(const std::vector<uint8_t>& cmsData);

/**
 * @brief Serialize certificate to PEM format
 *
 * @param cert X509 certificate
 * @return PEM string, or std::nullopt on error
 */
std::optional<std::string> certificateToPem(X509* cert);

/**
 * @brief Serialize certificate to DER format
 *
 * @param cert X509 certificate
 * @return DER bytes, or empty vector on error
 */
std::vector<uint8_t> certificateToDer(X509* cert);

/**
 * @brief Compute SHA-256 fingerprint of certificate
 *
 * @param cert X509 certificate
 * @return Hex-encoded SHA-256 fingerprint (64 chars lowercase),
 *         or std::nullopt on error
 *
 * @example
 * auto fingerprint = computeFingerprint(cert);
 * // Returns: "a1b2c3d4e5f6..."
 */
std::optional<std::string> computeFingerprint(X509* cert);

/**
 * @brief Validate certificate structure
 *
 * Performs basic sanity checks on certificate:
 * - Valid X509 structure
 * - Has subject DN
 * - Has issuer DN
 * - Has serial number
 * - Has validity period
 *
 * @param cert X509 certificate
 * @return true if certificate passes basic validation
 */
bool validateCertificateStructure(X509* cert);

/**
 * @brief RAII wrapper for X509 certificate
 *
 * Automatically frees X509 structure when going out of scope.
 */
class CertificatePtr {
public:
    explicit CertificatePtr(X509* cert = nullptr) : cert_(cert) {}

    ~CertificatePtr() {
        if (cert_) {
            X509_free(cert_);
        }
    }

    // Move semantics
    CertificatePtr(CertificatePtr&& other) noexcept : cert_(other.cert_) {
        other.cert_ = nullptr;
    }

    CertificatePtr& operator=(CertificatePtr&& other) noexcept {
        if (this != &other) {
            if (cert_) {
                X509_free(cert_);
            }
            cert_ = other.cert_;
            other.cert_ = nullptr;
        }
        return *this;
    }

    // Delete copy semantics
    CertificatePtr(const CertificatePtr&) = delete;
    CertificatePtr& operator=(const CertificatePtr&) = delete;

    // Access
    X509* get() const { return cert_; }
    X509* release() {
        X509* tmp = cert_;
        cert_ = nullptr;
        return tmp;
    }

    explicit operator bool() const { return cert_ != nullptr; }
    X509* operator->() const { return cert_; }

private:
    X509* cert_;
};

} // namespace x509
} // namespace icao

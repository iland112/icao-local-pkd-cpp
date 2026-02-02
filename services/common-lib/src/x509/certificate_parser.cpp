/**
 * @file certificate_parser.cpp
 * @brief Certificate format detection and parsing implementation
 *
 * Implements multi-format certificate parsing using OpenSSL.
 * Supports PEM, DER, CER, BIN, and CMS/PKCS7 formats.
 */

#include "icao/x509/certificate_parser.h"
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace icao {
namespace x509 {

namespace {
    /**
     * @brief Check if data starts with PEM marker
     */
    bool isPemFormat(const std::vector<uint8_t>& data) {
        if (data.size() < 27) {
            return false;
        }
        const char* pem_marker = "-----BEGIN CERTIFICATE-----";
        return std::memcmp(data.data(), pem_marker, 27) == 0;
    }

    /**
     * @brief Check if data starts with DER SEQUENCE tag
     */
    bool isDerFormat(const std::vector<uint8_t>& data) {
        if (data.size() < 2) {
            return false;
        }
        // DER certificates start with SEQUENCE tag (0x30)
        return data[0] == 0x30;
    }

    /**
     * @brief Check if data is PKCS7/CMS SignedData
     */
    bool isCmsFormat(const std::vector<uint8_t>& data) {
        if (data.size() < 10) {
            return false;
        }

        // Try to parse as PKCS7
        const unsigned char* p = data.data();
        PKCS7* pkcs7 = d2i_PKCS7(nullptr, &p, data.size());
        if (pkcs7) {
            bool is_signed = PKCS7_type_is_signed(pkcs7);
            PKCS7_free(pkcs7);
            return is_signed;
        }

        return false;
    }

    /**
     * @brief Convert bytes to hex string (lowercase)
     */
    std::string bytesToHex(const unsigned char* bytes, size_t length) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length; ++i) {
            oss << std::setw(2) << static_cast<int>(bytes[i]);
        }
        return oss.str();
    }
}

FormatDetectionResult detectCertificateFormat(const std::vector<uint8_t>& data) {
    FormatDetectionResult result;
    result.dataSize = data.size();

    if (data.empty()) {
        result.format = CertificateFormat::UNKNOWN;
        result.formatName = "Unknown";
        result.isBinary = false;
        result.error = "Empty data";
        return result;
    }

    // Check PEM format first (text-based)
    if (isPemFormat(data)) {
        result.format = CertificateFormat::PEM;
        result.formatName = "PEM";
        result.isBinary = false;
        return result;
    }

    // Check CMS/PKCS7 format (must check before DER)
    if (isCmsFormat(data)) {
        result.format = CertificateFormat::CMS;
        result.formatName = "CMS/PKCS7";
        result.isBinary = true;
        return result;
    }

    // Check DER format
    if (isDerFormat(data)) {
        result.format = CertificateFormat::DER;
        result.formatName = "DER";
        result.isBinary = true;
        return result;
    }

    // Unknown format
    result.format = CertificateFormat::UNKNOWN;
    result.formatName = "Unknown";
    result.isBinary = false;
    result.error = "Format not recognized";
    return result;
}

X509* parseCertificate(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return nullptr;
    }

    // Detect format
    auto formatResult = detectCertificateFormat(data);

    // Route to appropriate parser
    switch (formatResult.format) {
        case CertificateFormat::PEM: {
            std::string pem(reinterpret_cast<const char*>(data.data()), data.size());
            return parseCertificateFromPem(pem);
        }
        case CertificateFormat::DER:
        case CertificateFormat::CER:
        case CertificateFormat::BIN:
            return parseCertificateFromDer(data);
        case CertificateFormat::CMS: {
            // CMS may contain multiple certificates, return first one
            auto certs = extractCertificatesFromCms(data);
            if (!certs.empty()) {
                X509* first_cert = certs[0];
                // Free remaining certificates
                for (size_t i = 1; i < certs.size(); ++i) {
                    X509_free(certs[i]);
                }
                return first_cert;
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

X509* parseCertificateFromPem(const std::string& pem) {
    if (pem.empty()) {
        return nullptr;
    }

    // Create BIO from PEM string
    BIO* bio = BIO_new_mem_buf(pem.data(), pem.size());
    if (!bio) {
        return nullptr;
    }

    // Read certificate from BIO
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    // Cleanup BIO
    BIO_free(bio);

    return cert;
}

X509* parseCertificateFromDer(const std::vector<uint8_t>& der) {
    if (der.empty()) {
        return nullptr;
    }

    // d2i_X509 expects pointer to pointer that it will advance
    const unsigned char* p = der.data();
    X509* cert = d2i_X509(nullptr, &p, der.size());

    return cert;
}

std::vector<X509*> extractCertificatesFromCms(const std::vector<uint8_t>& cmsData) {
    std::vector<X509*> certificates;

    if (cmsData.empty()) {
        return certificates;
    }

    // Parse PKCS7 structure
    const unsigned char* p = cmsData.data();
    PKCS7* pkcs7 = d2i_PKCS7(nullptr, &p, cmsData.size());
    if (!pkcs7) {
        return certificates;
    }

    // Check if it's SignedData
    if (!PKCS7_type_is_signed(pkcs7)) {
        PKCS7_free(pkcs7);
        return certificates;
    }

    // Get certificates from SignedData
    STACK_OF(X509)* certs_stack = nullptr;
    int type = OBJ_obj2nid(pkcs7->type);

    if (type == NID_pkcs7_signed) {
        certs_stack = pkcs7->d.sign->cert;
    }

    // Extract certificates
    if (certs_stack) {
        int num_certs = sk_X509_num(certs_stack);
        for (int i = 0; i < num_certs; ++i) {
            X509* cert = sk_X509_value(certs_stack, i);
            if (cert) {
                // Duplicate certificate (increase reference count)
                X509* cert_copy = X509_dup(cert);
                if (cert_copy) {
                    certificates.push_back(cert_copy);
                }
            }
        }
    }

    // Cleanup PKCS7 structure
    PKCS7_free(pkcs7);

    return certificates;
}

std::optional<std::string> certificateToPem(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    // Create memory BIO
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return std::nullopt;
    }

    // Write certificate to BIO in PEM format
    if (PEM_write_bio_X509(bio, cert) != 1) {
        BIO_free(bio);
        return std::nullopt;
    }

    // Extract string from BIO
    char* pem_data = nullptr;
    long pem_len = BIO_get_mem_data(bio, &pem_data);
    if (pem_len <= 0 || !pem_data) {
        BIO_free(bio);
        return std::nullopt;
    }

    std::string pem_str(pem_data, pem_len);

    // Cleanup
    BIO_free(bio);

    return pem_str;
}

std::vector<uint8_t> certificateToDer(X509* cert) {
    std::vector<uint8_t> der;

    if (!cert) {
        return der;
    }

    // Get required buffer size
    int der_len = i2d_X509(cert, nullptr);
    if (der_len <= 0) {
        return der;
    }

    // Allocate buffer
    der.resize(der_len);

    // Encode certificate to DER
    unsigned char* p = der.data();
    int encoded_len = i2d_X509(cert, &p);
    if (encoded_len <= 0) {
        der.clear();
        return der;
    }

    // Resize to actual length (should match)
    der.resize(encoded_len);

    return der;
}

std::optional<std::string> computeFingerprint(X509* cert) {
    if (!cert) {
        return std::nullopt;
    }

    // Compute SHA-256 digest
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    if (X509_digest(cert, EVP_sha256(), digest, &digest_len) != 1) {
        return std::nullopt;
    }

    // Convert to hex string (lowercase)
    return bytesToHex(digest, digest_len);
}

bool validateCertificateStructure(X509* cert) {
    if (!cert) {
        return false;
    }

    // Check subject DN
    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject || X509_NAME_entry_count(subject) == 0) {
        return false;
    }

    // Check issuer DN
    X509_NAME* issuer = X509_get_issuer_name(cert);
    if (!issuer || X509_NAME_entry_count(issuer) == 0) {
        return false;
    }

    // Check serial number
    const ASN1_INTEGER* serial = X509_get0_serialNumber(cert);
    if (!serial) {
        return false;
    }

    // Check validity period
    const ASN1_TIME* not_before = X509_get0_notBefore(cert);
    const ASN1_TIME* not_after = X509_get0_notAfter(cert);
    if (!not_before || !not_after) {
        return false;
    }

    return true;
}

} // namespace x509
} // namespace icao

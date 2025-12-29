/**
 * @file OpenSslValidationAdapter.hpp
 * @brief OpenSSL-based certificate validation adapter
 */

#pragma once

#include "certificatevalidation/domain/port/ICertificateValidationPort.hpp"
#include "certificatevalidation/domain/repository/ICertificateRepository.hpp"
#include "certificatevalidation/domain/repository/ICrlRepository.hpp"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <memory>
#include <regex>
#include <spdlog/spdlog.h>

namespace certificatevalidation::infrastructure::adapter {

using namespace certificatevalidation::domain::model;
using namespace certificatevalidation::domain::port;
using namespace certificatevalidation::domain::repository;

/**
 * @brief OpenSSL-based X.509 certificate validation adapter
 *
 * Implements ICertificateValidationPort using OpenSSL 3.x library.
 *
 * Validation capabilities:
 * - Signature verification (RSA, ECDSA)
 * - Validity period checking
 * - Basic Constraints validation
 * - Key Usage validation
 * - CRL-based revocation checking
 * - Trust chain building
 */
class OpenSslValidationAdapter : public ICertificateValidationPort {
private:
    std::shared_ptr<ICertificateRepository> certificateRepository_;
    std::shared_ptr<ICrlRepository> crlRepository_;

    // RAII wrapper for X509
    struct X509Deleter {
        void operator()(X509* x) { X509_free(x); }
    };
    using X509Ptr = std::unique_ptr<X509, X509Deleter>;

    // RAII wrapper for X509_CRL
    struct X509CrlDeleter {
        void operator()(X509_CRL* crl) { X509_CRL_free(crl); }
    };
    using X509CrlPtr = std::unique_ptr<X509_CRL, X509CrlDeleter>;

    // RAII wrapper for EVP_PKEY
    struct EVP_PKEY_Deleter {
        void operator()(EVP_PKEY* key) { EVP_PKEY_free(key); }
    };
    using EVP_PKEY_Ptr = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;

    /**
     * @brief Convert certificate binary to OpenSSL X509
     */
    X509Ptr toX509(const std::vector<uint8_t>& binary) {
        const unsigned char* data = binary.data();
        X509* cert = d2i_X509(nullptr, &data, static_cast<long>(binary.size()));
        if (!cert) {
            spdlog::error("Failed to parse X509 certificate: {}",
                ERR_error_string(ERR_get_error(), nullptr));
            return nullptr;
        }
        return X509Ptr(cert);
    }

    /**
     * @brief Convert CRL binary to OpenSSL X509_CRL
     */
    X509CrlPtr toX509Crl(const std::vector<uint8_t>& binary) {
        const unsigned char* data = binary.data();
        X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(binary.size()));
        if (!crl) {
            spdlog::error("Failed to parse X509 CRL: {}",
                ERR_error_string(ERR_get_error(), nullptr));
            return nullptr;
        }
        return X509CrlPtr(crl);
    }

    /**
     * @brief Extract country code from issuer DN
     */
    std::string extractCountryCode(const std::string& issuerDn) {
        std::regex pattern("C=([A-Z]{2})");
        std::smatch match;
        if (std::regex_search(issuerDn, match, pattern)) {
            return match[1].str();
        }
        return "";
    }

    /**
     * @brief Extract issuer name (CN) from DN
     */
    std::string extractIssuerName(const std::string& issuerDn) {
        std::regex pattern("CN=([^,]+)");
        std::smatch match;
        if (std::regex_search(issuerDn, match, pattern)) {
            return match[1].str();
        }
        return "";
    }

public:
    OpenSslValidationAdapter(
        std::shared_ptr<ICertificateRepository> certificateRepository,
        std::shared_ptr<ICrlRepository> crlRepository
    ) : certificateRepository_(std::move(certificateRepository)),
        crlRepository_(std::move(crlRepository)) {
        // Initialize OpenSSL
        OpenSSL_add_all_algorithms();
        ERR_load_crypto_strings();
    }

    ~OpenSslValidationAdapter() {
        // Cleanup OpenSSL
        EVP_cleanup();
        ERR_free_strings();
    }

    /**
     * @brief Validate certificate signature
     */
    bool validateSignature(
        const Certificate& certificate,
        const std::optional<Certificate>& issuerCertificate
    ) override {
        spdlog::debug("validateSignature: certificate={}",
            certificate.getId().getValue());

        auto certX509 = toX509(certificate.getX509Data().getCertificateBinary());
        if (!certX509) {
            spdlog::error("Failed to parse certificate");
            return false;
        }

        X509Ptr issuerX509;
        if (issuerCertificate) {
            issuerX509 = toX509(issuerCertificate->getX509Data().getCertificateBinary());
            if (!issuerX509) {
                spdlog::error("Failed to parse issuer certificate");
                return false;
            }
        } else {
            // Self-signed: use the same certificate
            issuerX509 = toX509(certificate.getX509Data().getCertificateBinary());
        }

        // Get issuer public key
        EVP_PKEY* issuerKey = X509_get_pubkey(issuerX509.get());
        if (!issuerKey) {
            spdlog::error("Failed to get issuer public key");
            return false;
        }
        EVP_PKEY_Ptr issuerKeyPtr(issuerKey);

        // Verify signature
        int result = X509_verify(certX509.get(), issuerKey);
        if (result == 1) {
            spdlog::debug("Signature verification succeeded");
            return true;
        } else {
            spdlog::error("Signature verification failed: {}",
                ERR_error_string(ERR_get_error(), nullptr));
            return false;
        }
    }

    /**
     * @brief Validate certificate validity period
     */
    bool validateValidity(const Certificate& certificate) override {
        spdlog::debug("validateValidity: certificate={}",
            certificate.getId().getValue());

        return certificate.isCurrentlyValid();
    }

    /**
     * @brief Validate Basic Constraints extension
     */
    bool validateBasicConstraints(const Certificate& certificate) override {
        spdlog::debug("validateBasicConstraints: certificate={}, type={}",
            certificate.getId().getValue(),
            toString(certificate.getCertificateType()));

        auto certX509 = toX509(certificate.getX509Data().getCertificateBinary());
        if (!certX509) {
            return false;
        }

        // Get Basic Constraints extension
        BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
            X509_get_ext_d2i(certX509.get(), NID_basic_constraints, nullptr, nullptr)
        );

        if (!bc) {
            // No Basic Constraints - OK for non-CA certificates
            spdlog::debug("No Basic Constraints extension found");
            return !isCA(certificate.getCertificateType());
        }

        bool isCAFlag = bc->ca != 0;
        BASIC_CONSTRAINTS_free(bc);

        bool expectedCA = isCA(certificate.getCertificateType());
        if (isCAFlag != expectedCA) {
            spdlog::warn("Basic Constraints CA flag mismatch: isCA={}, expected={}",
                isCAFlag, expectedCA);
            return false;
        }

        spdlog::debug("Basic Constraints validation passed: isCA={}", isCAFlag);
        return true;
    }

    /**
     * @brief Validate Key Usage extension
     */
    bool validateKeyUsage(const Certificate& certificate) override {
        spdlog::debug("validateKeyUsage: certificate={}, type={}",
            certificate.getId().getValue(),
            toString(certificate.getCertificateType()));

        auto certX509 = toX509(certificate.getX509Data().getCertificateBinary());
        if (!certX509) {
            return false;
        }

        // Get Key Usage extension
        ASN1_BIT_STRING* keyUsage = static_cast<ASN1_BIT_STRING*>(
            X509_get_ext_d2i(certX509.get(), NID_key_usage, nullptr, nullptr)
        );

        if (!keyUsage) {
            // No Key Usage extension - warn but pass
            spdlog::warn("No Key Usage extension found");
            return true;
        }

        bool hasDigitalSignature = ASN1_BIT_STRING_get_bit(keyUsage, 0);  // digitalSignature
        bool hasKeyCertSign = ASN1_BIT_STRING_get_bit(keyUsage, 5);       // keyCertSign
        bool hasCrlSign = ASN1_BIT_STRING_get_bit(keyUsage, 6);           // cRLSign

        ASN1_BIT_STRING_free(keyUsage);

        CertificateType type = certificate.getCertificateType();
        bool valid = true;

        switch (type) {
            case CertificateType::CSCA:
                // CSCA requires keyCertSign and cRLSign
                valid = hasKeyCertSign && hasCrlSign;
                if (!valid) {
                    spdlog::warn("CSCA missing required key usage bits");
                }
                break;

            case CertificateType::DSC:
            case CertificateType::DSC_NC:
            case CertificateType::DS:
                // DSC/DS requires digitalSignature
                valid = hasDigitalSignature;
                if (!valid) {
                    spdlog::warn("DSC missing digitalSignature key usage bit");
                }
                break;

            default:
                valid = true;
                break;
        }

        spdlog::debug("Key Usage validation {}", valid ? "passed" : "failed");
        return valid;
    }

    /**
     * @brief Check certificate revocation status
     */
    bool checkRevocation(const Certificate& certificate) override {
        spdlog::debug("checkRevocation: certificate={}",
            certificate.getId().getValue());

        std::string issuerDn = certificate.getIssuerInfo().getDistinguishedName();
        std::string issuerName = extractIssuerName(issuerDn);
        std::string countryCode = extractCountryCode(issuerDn);

        if (issuerName.empty() || countryCode.empty()) {
            spdlog::warn("Could not extract issuer info for CRL check");
            return true;  // Assume not revoked
        }

        auto crlOpt = crlRepository_->findByIssuerNameAndCountry(issuerName, countryCode);
        if (!crlOpt) {
            spdlog::warn("No CRL found for issuer={}, country={}", issuerName, countryCode);
            return true;  // Assume not revoked
        }

        const auto& crl = *crlOpt;
        if (!crl.isValid()) {
            spdlog::warn("CRL is not valid (expired or not yet valid)");
            return true;  // Assume not revoked
        }

        std::string serialNumber = certificate.getX509Data().getSerialNumber();
        bool isRevoked = crl.isRevoked(serialNumber);

        if (isRevoked) {
            spdlog::error("Certificate is revoked: serialNumber={}", serialNumber);
            return false;
        }

        spdlog::debug("Certificate is not revoked");
        return true;
    }

    /**
     * @brief Check if certificate is revoked using specific CRL
     */
    bool isRevoked(
        const Certificate& certificate,
        const CertificateRevocationList& crl
    ) override {
        if (!crl.isValid()) {
            spdlog::warn("CRL is not valid");
            return false;
        }

        std::string serialNumber = certificate.getX509Data().getSerialNumber();
        return crl.isRevoked(serialNumber);
    }

    /**
     * @brief Build trust chain from certificate to trust anchor
     */
    std::vector<Certificate> buildTrustChain(
        const Certificate& certificate,
        const std::optional<Certificate>& trustAnchor,
        int maxDepth
    ) override {
        spdlog::info("Building trust chain: certificate={}",
            certificate.getId().getValue());

        std::vector<Certificate> chain;
        chain.push_back(certificate);

        Certificate current = certificate;
        int depth = 0;

        while (!current.isSelfSigned() && depth < maxDepth) {
            std::string issuerDn = current.getIssuerInfo().getDistinguishedName();
            auto issuerOpt = certificateRepository_->findBySubjectDn(issuerDn);

            if (!issuerOpt) {
                spdlog::warn("Issuer certificate not found: {}", issuerDn);
                break;
            }

            chain.push_back(*issuerOpt);
            spdlog::debug("Added to chain: issuer={}, depth={}",
                issuerOpt->getId().getValue(), depth + 1);

            current = *issuerOpt;
            depth++;
        }

        spdlog::info("Trust chain built: depth={}, count={}", depth, chain.size());
        return chain;
    }

    /**
     * @brief Perform full certificate validation
     */
    std::vector<ValidationError> performFullValidation(
        const Certificate& certificate,
        const std::optional<Certificate>& trustAnchor,
        bool checkRevocationFlag
    ) override {
        spdlog::info("performFullValidation: certificate={}",
            certificate.getId().getValue());

        std::vector<ValidationError> errors;

        // 1. Signature validation
        if (!validateSignature(certificate, trustAnchor)) {
            errors.push_back(ValidationError::signatureInvalid());
        }

        // 2. Validity period validation
        if (!validateValidity(certificate)) {
            if (certificate.isExpired()) {
                errors.push_back(ValidationError::certificateExpired());
            } else if (certificate.isNotYetValid()) {
                errors.push_back(ValidationError::certificateNotYetValid());
            }
        }

        // 3. Basic Constraints validation
        if (!validateBasicConstraints(certificate)) {
            errors.push_back(ValidationError::basicConstraintsInvalid());
        }

        // 4. Key Usage validation
        if (!validateKeyUsage(certificate)) {
            errors.push_back(ValidationError::keyUsageInvalid());
        }

        // 5. Revocation check
        if (checkRevocationFlag) {
            if (!checkRevocation(certificate)) {
                errors.push_back(ValidationError::certificateRevoked());
            }
        }

        if (errors.empty()) {
            spdlog::info("Full validation passed: certificate={}",
                certificate.getId().getValue());
        } else {
            spdlog::warn("Full validation failed with {} errors: certificate={}",
                errors.size(), certificate.getId().getValue());
        }

        return errors;
    }

    /**
     * @brief Validate trust chain (DSC -> CSCA)
     */
    void validateTrustChain(
        const Certificate& dsc,
        const Certificate& csca
    ) override {
        spdlog::debug("Validating trust chain: DSC -> CSCA");

        // 1. Verify DSC issuer DN matches CSCA subject DN
        std::string dscIssuerDn = dsc.getIssuerInfo().getDistinguishedName();
        std::string cscaSubjectDn = csca.getSubjectInfo().getDistinguishedName();

        if (dscIssuerDn != cscaSubjectDn) {
            throw std::runtime_error(
                "DSC issuer DN does not match CSCA subject DN: " +
                dscIssuerDn + " != " + cscaSubjectDn
            );
        }

        // 2. Verify DSC signature with CSCA public key
        if (!validateSignature(dsc, csca)) {
            throw std::runtime_error("DSC signature validation failed with CSCA public key");
        }

        // 3. Verify CSCA is self-signed
        if (!validateSignature(csca, std::nullopt)) {
            throw std::runtime_error("CSCA self-signed signature validation failed");
        }

        spdlog::debug("Trust chain validation passed");
    }
};

} // namespace certificatevalidation::infrastructure::adapter

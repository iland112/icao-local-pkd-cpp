/**
 * @file certificate.h
 * @brief Domain Model - Certificate Entity
 *
 * Domain-Driven Design: Entity representing a PKI Certificate
 */

#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <vector>

namespace domain {
namespace models {

/**
 * @brief Certificate type enumeration
 */
enum class CertificateType {
    CSCA,      // Country Signing Certificate Authority
    MLSC,      // Master List Signer Certificate
    DSC,       // Document Signer Certificate
    DSC_NC,    // Non-Conformant DSC
    CRL,       // Certificate Revocation List
    ML         // Master List
};

/**
 * @brief Certificate validity status
 */
enum class ValidityStatus {
    VALID,
    EXPIRED,
    NOT_YET_VALID,
    UNKNOWN
};

/**
 * @brief Certificate Entity (Domain Model)
 *
 * Represents a PKI certificate with its attributes and business rules.
 * Immutable value object for certificate data.
 */
class Certificate {
public:
    /**
     * @brief Constructor for Certificate entity
     */
    Certificate(
        std::string dn,
        std::string cn,
        std::string sn,
        std::string country,
        CertificateType certType,
        std::string subjectDn,
        std::string issuerDn,
        std::string fingerprint,
        std::chrono::system_clock::time_point validFrom,
        std::chrono::system_clock::time_point validTo,
        std::optional<std::string> pkdConformanceCode = std::nullopt,
        std::optional<std::string> pkdConformanceText = std::nullopt,
        std::optional<std::string> pkdVersion = std::nullopt,
        // X.509 Metadata Fields (v2.3.0)
        int version = 2,
        std::optional<std::string> signatureAlgorithm = std::nullopt,
        std::optional<std::string> signatureHashAlgorithm = std::nullopt,
        std::optional<std::string> publicKeyAlgorithm = std::nullopt,
        std::optional<int> publicKeySize = std::nullopt,
        std::optional<std::string> publicKeyCurve = std::nullopt,
        std::vector<std::string> keyUsage = {},
        std::vector<std::string> extendedKeyUsage = {},
        std::optional<bool> isCA = std::nullopt,
        std::optional<int> pathLenConstraint = std::nullopt,
        std::optional<std::string> subjectKeyIdentifier = std::nullopt,
        std::optional<std::string> authorityKeyIdentifier = std::nullopt,
        std::vector<std::string> crlDistributionPoints = {},
        std::optional<std::string> ocspResponderUrl = std::nullopt,
        std::optional<bool> isCertSelfSigned = std::nullopt
    ) : dn_(std::move(dn)),
        cn_(std::move(cn)),
        sn_(std::move(sn)),
        country_(std::move(country)),
        certType_(certType),
        subjectDn_(std::move(subjectDn)),
        issuerDn_(std::move(issuerDn)),
        fingerprint_(std::move(fingerprint)),
        validFrom_(validFrom),
        validTo_(validTo),
        pkdConformanceCode_(std::move(pkdConformanceCode)),
        pkdConformanceText_(std::move(pkdConformanceText)),
        pkdVersion_(std::move(pkdVersion)),
        // X.509 Metadata initialization
        version_(version),
        signatureAlgorithm_(std::move(signatureAlgorithm)),
        signatureHashAlgorithm_(std::move(signatureHashAlgorithm)),
        publicKeyAlgorithm_(std::move(publicKeyAlgorithm)),
        publicKeySize_(publicKeySize),
        publicKeyCurve_(std::move(publicKeyCurve)),
        keyUsage_(std::move(keyUsage)),
        extendedKeyUsage_(std::move(extendedKeyUsage)),
        isCA_(isCA),
        pathLenConstraint_(pathLenConstraint),
        subjectKeyIdentifier_(std::move(subjectKeyIdentifier)),
        authorityKeyIdentifier_(std::move(authorityKeyIdentifier)),
        crlDistributionPoints_(std::move(crlDistributionPoints)),
        ocspResponderUrl_(std::move(ocspResponderUrl)),
        isCertSelfSigned_(isCertSelfSigned) {}

    // Getters (immutable)
    const std::string& getDn() const { return dn_; }
    const std::string& getCn() const { return cn_; }
    const std::string& getSn() const { return sn_; }
    const std::string& getCountry() const { return country_; }
    CertificateType getCertType() const { return certType_; }
    const std::string& getSubjectDn() const { return subjectDn_; }
    const std::string& getIssuerDn() const { return issuerDn_; }
    const std::string& getFingerprint() const { return fingerprint_; }
    std::chrono::system_clock::time_point getValidFrom() const { return validFrom_; }
    std::chrono::system_clock::time_point getValidTo() const { return validTo_; }
    const std::optional<std::string>& getPkdConformanceCode() const { return pkdConformanceCode_; }
    const std::optional<std::string>& getPkdConformanceText() const { return pkdConformanceText_; }
    const std::optional<std::string>& getPkdVersion() const { return pkdVersion_; }

    // X.509 Metadata Getters (v2.3.0)
    int getVersion() const { return version_; }
    const std::optional<std::string>& getSignatureAlgorithm() const { return signatureAlgorithm_; }
    const std::optional<std::string>& getSignatureHashAlgorithm() const { return signatureHashAlgorithm_; }
    const std::optional<std::string>& getPublicKeyAlgorithm() const { return publicKeyAlgorithm_; }
    const std::optional<int>& getPublicKeySize() const { return publicKeySize_; }
    const std::optional<std::string>& getPublicKeyCurve() const { return publicKeyCurve_; }
    const std::vector<std::string>& getKeyUsage() const { return keyUsage_; }
    const std::vector<std::string>& getExtendedKeyUsage() const { return extendedKeyUsage_; }
    const std::optional<bool>& getIsCA() const { return isCA_; }
    const std::optional<int>& getPathLenConstraint() const { return pathLenConstraint_; }
    const std::optional<std::string>& getSubjectKeyIdentifier() const { return subjectKeyIdentifier_; }
    const std::optional<std::string>& getAuthorityKeyIdentifier() const { return authorityKeyIdentifier_; }
    const std::vector<std::string>& getCrlDistributionPoints() const { return crlDistributionPoints_; }
    const std::optional<std::string>& getOcspResponderUrl() const { return ocspResponderUrl_; }
    const std::optional<bool>& getIsCertSelfSigned() const { return isCertSelfSigned_; }

    /**
     * @brief Business logic: Check if certificate is currently valid
     * @return ValidityStatus
     */
    ValidityStatus getValidityStatus() const {
        auto now = std::chrono::system_clock::now();

        if (now < validFrom_) {
            return ValidityStatus::NOT_YET_VALID;
        } else if (now > validTo_) {
            return ValidityStatus::EXPIRED;
        } else {
            return ValidityStatus::VALID;
        }
    }

    /**
     * @brief Check if certificate is self-signed
     */
    bool isSelfSigned() const {
        return subjectDn_ == issuerDn_;
    }

    /**
     * @brief Get certificate type as string
     */
    std::string getCertTypeString() const {
        switch (certType_) {
            case CertificateType::CSCA: return "CSCA";
            case CertificateType::MLSC: return "MLSC";
            case CertificateType::DSC: return "DSC";
            case CertificateType::DSC_NC: return "DSC_NC";
            case CertificateType::CRL: return "CRL";
            case CertificateType::ML: return "ML";
            default: return "UNKNOWN";
        }
    }

private:
    std::string dn_;              // LDAP Distinguished Name
    std::string cn_;              // Common Name
    std::string sn_;              // Serial Number
    std::string country_;         // ISO 3166-1 alpha-2 code
    CertificateType certType_;    // Certificate type
    std::string subjectDn_;       // X.509 Subject DN
    std::string issuerDn_;        // X.509 Issuer DN
    std::string fingerprint_;     // SHA-256 fingerprint
    std::chrono::system_clock::time_point validFrom_;  // Not Before
    std::chrono::system_clock::time_point validTo_;    // Not After
    // DSC_NC specific attributes (optional)
    std::optional<std::string> pkdConformanceCode_;  // PKD conformance code
    std::optional<std::string> pkdConformanceText_;  // PKD conformance text
    std::optional<std::string> pkdVersion_;          // PKD version

    // X.509 Metadata Fields (v2.3.0) - 15 fields
    int version_;                                           // 0=v1, 1=v2, 2=v3
    std::optional<std::string> signatureAlgorithm_;         // "sha256WithRSAEncryption"
    std::optional<std::string> signatureHashAlgorithm_;     // "SHA-256"
    std::optional<std::string> publicKeyAlgorithm_;         // "RSA", "ECDSA"
    std::optional<int> publicKeySize_;                      // 2048, 4096 (bits)
    std::optional<std::string> publicKeyCurve_;             // "prime256v1" (ECDSA)
    std::vector<std::string> keyUsage_;                     // {"digitalSignature", "keyCertSign"}
    std::vector<std::string> extendedKeyUsage_;             // {"serverAuth", "clientAuth"}
    std::optional<bool> isCA_;                              // TRUE if CA certificate
    std::optional<int> pathLenConstraint_;                  // Path length constraint
    std::optional<std::string> subjectKeyIdentifier_;       // SKI (hex)
    std::optional<std::string> authorityKeyIdentifier_;     // AKI (hex)
    std::vector<std::string> crlDistributionPoints_;        // CRL URLs
    std::optional<std::string> ocspResponderUrl_;           // OCSP URL
    std::optional<bool> isCertSelfSigned_;                  // Self-signed flag
};

/**
 * @brief Search criteria for certificates (Value Object)
 */
struct CertificateSearchCriteria {
    std::optional<std::string> country;
    std::optional<CertificateType> certType;
    std::optional<ValidityStatus> validity;
    std::optional<std::string> searchTerm;
    int limit = 50;
    int offset = 0;

    /**
     * @brief Validate search criteria
     */
    bool isValid() const {
        return limit > 0 && limit <= 200 && offset >= 0;
    }
};

/**
 * @brief Certificate statistics by validity status (Value Object)
 */
struct CertificateStatistics {
    int total = 0;
    int valid = 0;
    int expired = 0;
    int notYetValid = 0;
    int unknown = 0;
};

/**
 * @brief Search result with pagination info and statistics (Value Object)
 */
struct CertificateSearchResult {
    std::vector<Certificate> certificates;
    int total;
    int limit;
    int offset;
    CertificateStatistics stats;  // Aggregated statistics for all matching certificates
};

} // namespace models
} // namespace domain

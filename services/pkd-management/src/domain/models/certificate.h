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
        std::optional<std::string> pkdVersion = std::nullopt
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
        pkdVersion_(std::move(pkdVersion)) {}

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
 * @brief Search result with pagination info (Value Object)
 */
struct CertificateSearchResult {
    std::vector<Certificate> certificates;
    int total;
    int limit;
    int offset;
};

} // namespace models
} // namespace domain

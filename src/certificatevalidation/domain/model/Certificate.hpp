/**
 * @file Certificate.hpp
 * @brief Certificate Aggregate Root
 */

#pragma once

#include "shared/domain/AggregateRoot.hpp"
#include "CertificateId.hpp"
#include "CertificateType.hpp"
#include "CertificateStatus.hpp"
#include "X509Data.hpp"
#include "SubjectInfo.hpp"
#include "IssuerInfo.hpp"
#include "ValidityPeriod.hpp"
#include "ValidationResult.hpp"
#include "ValidationError.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <stdexcept>

namespace certificatevalidation::domain::model {

/**
 * @brief Source type for certificate origin
 */
enum class CertificateSourceType {
    LDIF_DSC,       ///< DSC from LDIF file
    LDIF_CSCA,      ///< CSCA from LDIF file
    MASTER_LIST     ///< CSCA from Master List
};

inline std::string toString(CertificateSourceType type) {
    switch (type) {
        case CertificateSourceType::LDIF_DSC:    return "LDIF_DSC";
        case CertificateSourceType::LDIF_CSCA:   return "LDIF_CSCA";
        case CertificateSourceType::MASTER_LIST: return "MASTER_LIST";
        default:                                  return "UNKNOWN";
    }
}

/**
 * @brief Certificate Aggregate Root
 *
 * X.509 certificate aggregate root that manages certificate lifecycle,
 * validation status, and domain events.
 *
 * DDD Aggregate Root Pattern:
 * - Boundary: Certificate entity consistency boundary
 * - Identity: CertificateId provides unique identification
 * - Lifecycle: Manages from creation to revocation
 * - Invariants: Enforces all business rules
 */
class Certificate : public shared::domain::AggregateRoot<CertificateId> {
private:
    CertificateId id_;
    std::string uploadId_;  // Cross-context reference to FileUpload
    X509Data x509Data_;
    SubjectInfo subjectInfo_;
    IssuerInfo issuerInfo_;
    ValidityPeriod validity_;
    CertificateType certificateType_;
    CertificateStatus status_;
    std::string signatureAlgorithm_;
    CertificateSourceType sourceType_;
    std::optional<std::string> masterListId_;
    std::optional<ValidationResult> validationResult_;
    std::vector<ValidationError> validationErrors_;
    std::map<std::string, std::vector<std::string>> allAttributes_;
    bool uploadedToLdap_;
    std::optional<std::chrono::system_clock::time_point> uploadedToLdapAt_;
    std::chrono::system_clock::time_point createdAt_;
    std::optional<std::chrono::system_clock::time_point> updatedAt_;

    // Private constructor for static factory
    Certificate(
        CertificateId id,
        std::string uploadId,
        X509Data x509Data,
        SubjectInfo subjectInfo,
        IssuerInfo issuerInfo,
        ValidityPeriod validity,
        CertificateType certificateType,
        std::string signatureAlgorithm,
        CertificateSourceType sourceType,
        std::optional<std::string> masterListId
    ) : id_(std::move(id)),
        uploadId_(std::move(uploadId)),
        x509Data_(std::move(x509Data)),
        subjectInfo_(std::move(subjectInfo)),
        issuerInfo_(std::move(issuerInfo)),
        validity_(std::move(validity)),
        certificateType_(certificateType),
        status_(CertificateStatus::UNKNOWN),
        signatureAlgorithm_(std::move(signatureAlgorithm)),
        sourceType_(sourceType),
        masterListId_(std::move(masterListId)),
        uploadedToLdap_(false),
        createdAt_(std::chrono::system_clock::now()) {}

public:
    /**
     * @brief Create new Certificate from LDIF file
     */
    static Certificate create(
        const std::string& uploadId,
        X509Data x509Data,
        SubjectInfo subjectInfo,
        IssuerInfo issuerInfo,
        ValidityPeriod validity,
        CertificateType certificateType,
        const std::string& signatureAlgorithm,
        std::map<std::string, std::vector<std::string>> allAttributes = {}
    ) {
        if (uploadId.empty()) {
            throw std::invalid_argument("uploadId cannot be null");
        }
        if (signatureAlgorithm.empty()) {
            throw std::invalid_argument("signatureAlgorithm cannot be null or blank");
        }

        // Auto-determine sourceType from certificateType
        CertificateSourceType sourceType = (certificateType == CertificateType::CSCA)
            ? CertificateSourceType::LDIF_CSCA
            : CertificateSourceType::LDIF_DSC;

        Certificate cert(
            CertificateId::newId(),
            uploadId,
            std::move(x509Data),
            std::move(subjectInfo),
            std::move(issuerInfo),
            std::move(validity),
            certificateType,
            signatureAlgorithm,
            sourceType,
            std::nullopt
        );
        cert.allAttributes_ = std::move(allAttributes);

        // TODO: Publish CertificateCreatedEvent

        return cert;
    }

    /**
     * @brief Create Certificate from Master List
     */
    static Certificate createFromMasterList(
        const std::string& uploadId,
        const std::optional<std::string>& masterListId,
        X509Data x509Data,
        SubjectInfo subjectInfo,
        IssuerInfo issuerInfo,
        ValidityPeriod validity,
        const std::string& signatureAlgorithm
    ) {
        if (uploadId.empty()) {
            throw std::invalid_argument("uploadId cannot be null");
        }

        return Certificate(
            CertificateId::newId(),
            uploadId,
            std::move(x509Data),
            std::move(subjectInfo),
            std::move(issuerInfo),
            std::move(validity),
            CertificateType::CSCA,  // Always CSCA for Master List
            signatureAlgorithm,
            CertificateSourceType::MASTER_LIST,
            masterListId
        );
    }

    /**
     * @brief Reconstruct Certificate from database
     */
    static Certificate reconstitute(
        const CertificateId& id,
        const std::string& uploadId,
        X509Data x509Data,
        SubjectInfo subjectInfo,
        IssuerInfo issuerInfo,
        ValidityPeriod validity,
        CertificateType certificateType,
        CertificateStatus status,
        const std::string& signatureAlgorithm,
        CertificateSourceType sourceType,
        const std::optional<std::string>& masterListId,
        bool uploadedToLdap,
        std::chrono::system_clock::time_point createdAt
    ) {
        Certificate cert(
            id, uploadId, std::move(x509Data), std::move(subjectInfo),
            std::move(issuerInfo), std::move(validity), certificateType,
            signatureAlgorithm, sourceType, masterListId
        );
        cert.status_ = status;
        cert.uploadedToLdap_ = uploadedToLdap;
        cert.createdAt_ = createdAt;
        return cert;
    }

    // ========== Getters ==========
    [[nodiscard]] const CertificateId& getId() const noexcept override { return id_; }
    [[nodiscard]] const std::string& getUploadId() const noexcept { return uploadId_; }
    [[nodiscard]] const X509Data& getX509Data() const noexcept { return x509Data_; }
    [[nodiscard]] const SubjectInfo& getSubjectInfo() const noexcept { return subjectInfo_; }
    [[nodiscard]] const IssuerInfo& getIssuerInfo() const noexcept { return issuerInfo_; }
    [[nodiscard]] const ValidityPeriod& getValidity() const noexcept { return validity_; }
    [[nodiscard]] CertificateType getCertificateType() const noexcept { return certificateType_; }
    [[nodiscard]] CertificateStatus getStatus() const noexcept { return status_; }
    [[nodiscard]] const std::string& getSignatureAlgorithm() const noexcept { return signatureAlgorithm_; }
    [[nodiscard]] CertificateSourceType getSourceType() const noexcept { return sourceType_; }
    [[nodiscard]] const std::optional<std::string>& getMasterListId() const noexcept { return masterListId_; }
    [[nodiscard]] const std::optional<ValidationResult>& getValidationResult() const noexcept { return validationResult_; }
    [[nodiscard]] const std::vector<ValidationError>& getValidationErrors() const noexcept { return validationErrors_; }
    [[nodiscard]] const std::map<std::string, std::vector<std::string>>& getAllAttributes() const noexcept { return allAttributes_; }
    [[nodiscard]] bool isUploadedToLdap() const noexcept { return uploadedToLdap_; }
    [[nodiscard]] std::chrono::system_clock::time_point getCreatedAt() const noexcept { return createdAt_; }
    [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>& getUpdatedAt() const noexcept { return updatedAt_; }

    // ========== Business Logic ==========

    /**
     * @brief Record validation result
     */
    void recordValidation(const ValidationResult& result) {
        validationResult_ = result;
        status_ = result.getOverallStatus();
        updatedAt_ = std::chrono::system_clock::now();
        // TODO: Publish CertificateValidatedEvent
    }

    /**
     * @brief Add validation error
     */
    void addValidationError(const ValidationError& error) {
        validationErrors_.push_back(error);
        updatedAt_ = std::chrono::system_clock::now();
    }

    /**
     * @brief Clear validation errors
     */
    void clearValidationErrors() {
        validationErrors_.clear();
        updatedAt_ = std::chrono::system_clock::now();
    }

    /**
     * @brief Mark as uploaded to LDAP
     */
    void markAsUploadedToLdap() {
        uploadedToLdap_ = true;
        uploadedToLdapAt_ = std::chrono::system_clock::now();
        updatedAt_ = std::chrono::system_clock::now();
    }

    // ========== Status Checks ==========

    [[nodiscard]] bool isValid() const noexcept {
        return status_ == CertificateStatus::VALID;
    }

    [[nodiscard]] bool isExpired() const noexcept {
        return status_ == CertificateStatus::EXPIRED || validity_.isExpired();
    }

    [[nodiscard]] bool isNotYetValid() const noexcept {
        return status_ == CertificateStatus::NOT_YET_VALID || validity_.isNotYetValid();
    }

    [[nodiscard]] bool isRevoked() const noexcept {
        return status_ == CertificateStatus::REVOKED;
    }

    [[nodiscard]] bool isCurrentlyValid() const noexcept {
        return validity_.isCurrentlyValid();
    }

    [[nodiscard]] bool isCA() const noexcept {
        return issuerInfo_.isCA();
    }

    [[nodiscard]] bool isSelfSigned() const noexcept {
        return subjectInfo_.getDistinguishedName() == issuerInfo_.getDistinguishedName();
    }

    [[nodiscard]] bool isCsca() const noexcept {
        return certificateType_ == CertificateType::CSCA;
    }

    [[nodiscard]] bool isDsc() const noexcept {
        return certificateType_ == CertificateType::DSC ||
               certificateType_ == CertificateType::DSC_NC;
    }

    [[nodiscard]] bool isFromMasterList() const noexcept {
        return sourceType_ == CertificateSourceType::MASTER_LIST;
    }

    [[nodiscard]] bool isFromLdif() const noexcept {
        return sourceType_ == CertificateSourceType::LDIF_DSC ||
               sourceType_ == CertificateSourceType::LDIF_CSCA;
    }

    [[nodiscard]] long daysUntilExpiration() const noexcept {
        return validity_.daysUntilExpiration();
    }

    [[nodiscard]] bool isExpiringSoon() const noexcept {
        return validity_.isExpiringSoon();
    }

    [[nodiscard]] bool hasCriticalErrors() const noexcept {
        for (const auto& error : validationErrors_) {
            if (error.isCritical()) return true;
        }
        return false;
    }

    [[nodiscard]] bool isComplete() const noexcept {
        return x509Data_.isComplete() &&
               subjectInfo_.isComplete() &&
               issuerInfo_.isComplete() &&
               !signatureAlgorithm_.empty();
    }

    [[nodiscard]] std::string toString() const {
        return "Certificate[id=" + id_.getValue() +
               ", subject=" + subjectInfo_.getCommonNameOrDefault() +
               ", issuer=" + issuerInfo_.getCommonNameOrDefault() +
               ", type=" + certificatevalidation::domain::model::toString(certificateType_) +
               ", status=" + certificatevalidation::domain::model::toString(status_) + "]";
    }
};

} // namespace certificatevalidation::domain::model

/**
 * @file ParsedFile.hpp
 * @brief Aggregate Root for parsed file results
 */

#pragma once

#include "CertificateData.hpp"
#include "CrlData.hpp"
#include "shared/domain/AggregateRoot.hpp"
#include "fileupload/domain/model/UploadId.hpp"
#include <vector>
#include <string>

namespace fileparsing::domain::model {

/**
 * @brief Parsing error info
 */
struct ParsingError {
    std::string entryDn;
    std::string errorCode;
    std::string errorMessage;
};

/**
 * @brief Aggregate Root for parsed file content
 */
class ParsedFile : public shared::domain::AggregateRoot<fileupload::domain::model::UploadId> {
private:
    std::vector<CertificateData> certificates_;
    std::vector<CrlData> crls_;
    std::vector<ParsingError> errors_;
    int totalEntries_ = 0;
    int processedEntries_ = 0;

public:
    explicit ParsedFile(fileupload::domain::model::UploadId uploadId)
        : AggregateRoot<fileupload::domain::model::UploadId>(std::move(uploadId)) {}

    // Mutators
    void addCertificate(CertificateData cert) {
        certificates_.push_back(std::move(cert));
    }

    void addCrl(CrlData crl) {
        crls_.push_back(std::move(crl));
    }

    void addError(ParsingError error) {
        errors_.push_back(std::move(error));
    }

    void setTotalEntries(int total) {
        totalEntries_ = total;
    }

    void incrementProcessedEntries() {
        processedEntries_++;
    }

    // Getters
    [[nodiscard]] const std::vector<CertificateData>& getCertificates() const noexcept {
        return certificates_;
    }

    [[nodiscard]] const std::vector<CrlData>& getCrls() const noexcept {
        return crls_;
    }

    [[nodiscard]] const std::vector<ParsingError>& getErrors() const noexcept {
        return errors_;
    }

    [[nodiscard]] int getTotalEntries() const noexcept {
        return totalEntries_;
    }

    [[nodiscard]] int getProcessedEntries() const noexcept {
        return processedEntries_;
    }

    // Statistics
    [[nodiscard]] int getCscaCount() const noexcept {
        int count = 0;
        for (const auto& cert : certificates_) {
            if (cert.getCertificateType() == CertificateType::CSCA) count++;
        }
        return count;
    }

    [[nodiscard]] int getDscCount() const noexcept {
        int count = 0;
        for (const auto& cert : certificates_) {
            if (cert.getCertificateType() == CertificateType::DSC) count++;
        }
        return count;
    }

    [[nodiscard]] int getDscNcCount() const noexcept {
        int count = 0;
        for (const auto& cert : certificates_) {
            if (cert.getCertificateType() == CertificateType::DSC_NC) count++;
        }
        return count;
    }

    [[nodiscard]] int getCrlCount() const noexcept {
        return static_cast<int>(crls_.size());
    }

    [[nodiscard]] int getErrorCount() const noexcept {
        return static_cast<int>(errors_.size());
    }

    [[nodiscard]] double getProgressPercent() const noexcept {
        if (totalEntries_ == 0) return 0.0;
        return (static_cast<double>(processedEntries_) / totalEntries_) * 100.0;
    }
};

} // namespace fileparsing::domain::model

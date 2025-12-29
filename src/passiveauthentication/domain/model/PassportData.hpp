#pragma once

#include "shared/domain/AggregateRoot.hpp"
#include "PassportDataId.hpp"
#include "SecurityObjectDocument.hpp"
#include "DataGroup.hpp"
#include "PassiveAuthenticationResult.hpp"
#include "PassiveAuthenticationStatus.hpp"
#include "RequestMetadata.hpp"
#include "shared/exception/DomainException.hpp"
#include <vector>
#include <optional>
#include <string>
#include <chrono>
#include <algorithm>
#include <set>

namespace pa::domain::model {

/**
 * PassportData Aggregate Root.
 *
 * Represents ePassport data submitted for Passive Authentication verification.
 * Contains:
 * - Security Object Document (SOD) - PKCS#7 SignedData
 * - Data Groups (DG1-DG16) - ePassport data groups
 * - Verification result - overall PA result
 * - Request metadata - audit information
 * - Timing information - for performance tracking
 */
class PassportData : public shared::domain::AggregateRoot<PassportDataId> {
private:
    PassportDataId id_;
    SecurityObjectDocument sod_;
    std::vector<DataGroup> dataGroups_;
    std::optional<PassiveAuthenticationResult> result_;
    RequestMetadata requestMetadata_;

    std::chrono::system_clock::time_point startedAt_;
    std::optional<std::chrono::system_clock::time_point> completedAt_;
    std::optional<long> processingDurationMs_;

    PassiveAuthenticationStatus verificationStatus_;
    std::string issuingCountry_;
    std::string documentNumber_;
    std::string rawRequestData_;

    static void validateCreationParameters(
        const SecurityObjectDocument& sod,
        const std::vector<DataGroup>& dataGroups
    ) {
        if (dataGroups.empty()) {
            throw shared::exception::DomainException(
                "EMPTY_DATA_GROUPS",
                "At least one data group is required"
            );
        }

        // Check for duplicate data group numbers
        std::set<int> uniqueNumbers;
        for (const auto& dg : dataGroups) {
            int num = toInt(dg.getNumber());
            if (uniqueNumbers.count(num) > 0) {
                throw shared::exception::DomainException(
                    "DUPLICATE_DATA_GROUP",
                    "Duplicate data group number: DG" + std::to_string(num)
                );
            }
            uniqueNumbers.insert(num);
        }
    }

    PassportData() = default;

public:
    /**
     * Create new PassportData for verification.
     */
    static PassportData create(
        const SecurityObjectDocument& sod,
        const std::vector<DataGroup>& dataGroups,
        const RequestMetadata& requestMetadata,
        const std::string& rawRequestData,
        const std::string& issuingCountry,
        const std::string& documentNumber
    ) {
        validateCreationParameters(sod, dataGroups);

        PassportData passportData;
        passportData.id_ = PassportDataId::newId();
        passportData.sod_ = sod;
        passportData.dataGroups_ = dataGroups;
        passportData.requestMetadata_ = requestMetadata;
        passportData.rawRequestData_ = rawRequestData;
        passportData.startedAt_ = std::chrono::system_clock::now();
        passportData.verificationStatus_ = PassiveAuthenticationStatus::VALID;  // Initial optimistic status
        passportData.issuingCountry_ = issuingCountry;
        passportData.documentNumber_ = documentNumber;

        return passportData;
    }

    /**
     * Record verification result.
     */
    void recordResult(const PassiveAuthenticationResult& res) {
        result_ = res;
        verificationStatus_ = res.getStatus();
        completedAt_ = std::chrono::system_clock::now();
        processingDurationMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            completedAt_.value() - startedAt_
        ).count();
    }

    /**
     * Mark verification as started.
     */
    void markVerificationStarted() {
        startedAt_ = std::chrono::system_clock::now();
    }

    /**
     * Mark verification as completed.
     */
    void markVerificationCompleted(PassiveAuthenticationStatus status) {
        verificationStatus_ = status;
        completedAt_ = std::chrono::system_clock::now();
        processingDurationMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            completedAt_.value() - startedAt_
        ).count();
    }

    /**
     * Add data group to passport data.
     */
    void addDataGroup(const DataGroup& dataGroup) {
        // Check for duplicate data group numbers
        for (const auto& dg : dataGroups_) {
            if (dg.getNumber() == dataGroup.getNumber()) {
                throw shared::exception::DomainException(
                    "DUPLICATE_DATA_GROUP",
                    "Data group " + toString(dataGroup.getNumber()) + " already exists"
                );
            }
        }

        dataGroups_.push_back(dataGroup);
    }

    /**
     * Get data group by number.
     */
    std::optional<DataGroup> getDataGroup(DataGroupNumber number) const {
        for (const auto& dg : dataGroups_) {
            if (dg.getNumber() == number) {
                return dg;
            }
        }
        return std::nullopt;
    }

    // Getters
    const PassportDataId& getId() const override { return id_; }
    const SecurityObjectDocument& getSod() const { return sod_; }
    const std::vector<DataGroup>& getDataGroups() const { return dataGroups_; }
    const std::optional<PassiveAuthenticationResult>& getResult() const { return result_; }
    const RequestMetadata& getRequestMetadata() const { return requestMetadata_; }
    const std::chrono::system_clock::time_point& getStartedAt() const { return startedAt_; }
    const std::optional<std::chrono::system_clock::time_point>& getCompletedAt() const { return completedAt_; }
    const std::optional<long>& getProcessingDurationMs() const { return processingDurationMs_; }
    PassiveAuthenticationStatus getVerificationStatus() const { return verificationStatus_; }
    const std::string& getIssuingCountry() const { return issuingCountry_; }
    const std::string& getDocumentNumber() const { return documentNumber_; }
    const std::string& getRawRequestData() const { return rawRequestData_; }

    int getDataGroupCount() const { return static_cast<int>(dataGroups_.size()); }

    int getValidDataGroupCount() const {
        return static_cast<int>(std::count_if(
            dataGroups_.begin(), dataGroups_.end(),
            [](const DataGroup& dg) { return dg.isValid(); }
        ));
    }

    int getInvalidDataGroupCount() const {
        return static_cast<int>(std::count_if(
            dataGroups_.begin(), dataGroups_.end(),
            [](const DataGroup& dg) { return !dg.isValid(); }
        ));
    }

    bool allDataGroupsValid() const {
        if (dataGroups_.empty()) return false;
        return std::all_of(
            dataGroups_.begin(), dataGroups_.end(),
            [](const DataGroup& dg) { return dg.isValid(); }
        );
    }

    bool isCompleted() const { return completedAt_.has_value(); }
    bool isInProgress() const { return !completedAt_.has_value(); }
    bool isValid() const { return verificationStatus_ == PassiveAuthenticationStatus::VALID; }
    bool isInvalid() const { return verificationStatus_ == PassiveAuthenticationStatus::INVALID; }
    bool isError() const { return verificationStatus_ == PassiveAuthenticationStatus::ERROR; }

    std::optional<double> getProcessingDurationInSeconds() const {
        if (!processingDurationMs_.has_value()) {
            return std::nullopt;
        }
        return processingDurationMs_.value() / 1000.0;
    }

    std::vector<PassiveAuthenticationError> getVerificationErrors() const {
        if (!result_.has_value()) {
            return {};
        }
        return result_->getErrors();
    }

    std::vector<PassiveAuthenticationError> getCriticalErrors() const {
        std::vector<PassiveAuthenticationError> criticalErrors;
        for (const auto& error : getVerificationErrors()) {
            if (error.isCritical()) {
                criticalErrors.push_back(error);
            }
        }
        return criticalErrors;
    }
};

} // namespace pa::domain::model

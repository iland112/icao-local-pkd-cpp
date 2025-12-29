#pragma once

#include "PassiveAuthenticationStatus.hpp"
#include "PassiveAuthenticationError.hpp"
#include <vector>
#include <string>

namespace pa::domain::model {

/**
 * Result of Passive Authentication verification.
 *
 * Aggregates the results of all verification steps:
 * - Overall verification status (VALID/INVALID/ERROR)
 * - Certificate chain validation result
 * - SOD signature validation result
 * - Data group hash verification statistics
 * - Detailed error list
 */
class PassiveAuthenticationResult {
private:
    PassiveAuthenticationStatus status_;
    bool certificateChainValid_;
    bool sodSignatureValid_;
    int totalDataGroups_;
    int validDataGroups_;
    int invalidDataGroups_;
    std::vector<PassiveAuthenticationError> errors_;

    PassiveAuthenticationResult()
        : status_(PassiveAuthenticationStatus::ERROR),
          certificateChainValid_(false),
          sodSignatureValid_(false),
          totalDataGroups_(0),
          validDataGroups_(0),
          invalidDataGroups_(0) {}

public:
    /**
     * Create a VALID result (all verifications passed).
     */
    static PassiveAuthenticationResult valid(int totalDataGroups) {
        PassiveAuthenticationResult result;
        result.status_ = PassiveAuthenticationStatus::VALID;
        result.certificateChainValid_ = true;
        result.sodSignatureValid_ = true;
        result.totalDataGroups_ = totalDataGroups;
        result.validDataGroups_ = totalDataGroups;
        result.invalidDataGroups_ = 0;
        return result;
    }

    /**
     * Create an INVALID result (one or more verifications failed).
     */
    static PassiveAuthenticationResult invalid(
        bool certificateChainValid,
        bool sodSignatureValid,
        int totalDataGroups,
        int validDataGroups,
        int invalidDataGroups,
        const std::vector<PassiveAuthenticationError>& errors
    ) {
        PassiveAuthenticationResult result;
        result.status_ = PassiveAuthenticationStatus::INVALID;
        result.certificateChainValid_ = certificateChainValid;
        result.sodSignatureValid_ = sodSignatureValid;
        result.totalDataGroups_ = totalDataGroups;
        result.validDataGroups_ = validDataGroups;
        result.invalidDataGroups_ = invalidDataGroups;
        result.errors_ = errors;
        return result;
    }

    /**
     * Create an ERROR result (unexpected error occurred).
     */
    static PassiveAuthenticationResult error(const PassiveAuthenticationError& err) {
        PassiveAuthenticationResult result;
        result.status_ = PassiveAuthenticationStatus::ERROR;
        result.errors_.push_back(err);
        return result;
    }

    /**
     * Create result with detailed verification statistics.
     */
    static PassiveAuthenticationResult withStatistics(
        bool certificateChainValid,
        bool sodSignatureValid,
        int totalDataGroups,
        int validDataGroups,
        const std::vector<PassiveAuthenticationError>& errors
    ) {
        int invalidDataGroups = totalDataGroups - validDataGroups;

        // Determine overall status
        PassiveAuthenticationStatus overallStatus;
        if (certificateChainValid && sodSignatureValid && invalidDataGroups == 0) {
            overallStatus = PassiveAuthenticationStatus::VALID;
        } else {
            overallStatus = PassiveAuthenticationStatus::INVALID;
        }

        PassiveAuthenticationResult result;
        result.status_ = overallStatus;
        result.certificateChainValid_ = certificateChainValid;
        result.sodSignatureValid_ = sodSignatureValid;
        result.totalDataGroups_ = totalDataGroups;
        result.validDataGroups_ = validDataGroups;
        result.invalidDataGroups_ = invalidDataGroups;
        result.errors_ = errors;
        return result;
    }

    PassiveAuthenticationStatus getStatus() const { return status_; }
    bool isCertificateChainValid() const { return certificateChainValid_; }
    bool isSodSignatureValid() const { return sodSignatureValid_; }
    int getTotalDataGroups() const { return totalDataGroups_; }
    int getValidDataGroups() const { return validDataGroups_; }
    int getInvalidDataGroups() const { return invalidDataGroups_; }
    const std::vector<PassiveAuthenticationError>& getErrors() const { return errors_; }

    bool isValid() const { return status_ == PassiveAuthenticationStatus::VALID; }
    bool isInvalid() const { return status_ == PassiveAuthenticationStatus::INVALID; }
    bool isError() const { return status_ == PassiveAuthenticationStatus::ERROR; }

    /**
     * Get hash verification success rate.
     */
    double getHashVerificationSuccessRate() const {
        if (totalDataGroups_ == 0) {
            return 0.0;
        }
        return static_cast<double>(validDataGroups_) / totalDataGroups_ * 100.0;
    }

    /**
     * Check if all verification components passed.
     */
    bool allComponentsValid() const {
        return certificateChainValid_ &&
               sodSignatureValid_ &&
               invalidDataGroups_ == 0 &&
               totalDataGroups_ > 0;
    }
};

} // namespace pa::domain::model

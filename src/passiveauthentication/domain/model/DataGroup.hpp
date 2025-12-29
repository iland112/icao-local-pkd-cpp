#pragma once

#include "DataGroupNumber.hpp"
#include "DataGroupHash.hpp"
#include "shared/exception/DomainException.hpp"
#include <vector>
#include <optional>
#include <cstdint>

namespace pa::domain::model {

/**
 * Data Group in ePassport LDS (Logical Data Structure).
 *
 * Represents a single data group (DG1-DG16) with its content and hash values.
 * Used for Passive Authentication hash verification.
 */
class DataGroup {
private:
    DataGroupNumber number_;
    std::vector<uint8_t> content_;
    std::optional<DataGroupHash> expectedHash_;  // From SOD
    std::optional<DataGroupHash> actualHash_;    // Calculated
    bool valid_ = false;
    bool hashMismatchDetected_ = false;

    void validate(const std::vector<uint8_t>& content) {
        if (content.empty()) {
            throw shared::exception::DomainException(
                "INVALID_DG_CONTENT",
                "Data Group content cannot be null or empty"
            );
        }
    }

    DataGroup(DataGroupNumber number, std::vector<uint8_t> content)
        : number_(number), content_(std::move(content)) {
        validate(content_);
    }

public:
    DataGroup() = default;

    /**
     * Create DataGroup with content only (hash will be calculated later).
     */
    static DataGroup of(DataGroupNumber number, const std::vector<uint8_t>& content) {
        return DataGroup(number, content);
    }

    /**
     * Create DataGroup with expected hash from SOD.
     */
    static DataGroup withExpectedHash(
        DataGroupNumber number,
        const std::vector<uint8_t>& content,
        const DataGroupHash& expectedHash
    ) {
        DataGroup dg(number, content);
        dg.expectedHash_ = expectedHash;
        return dg;
    }

    /**
     * Get data group number.
     */
    DataGroupNumber getNumber() const { return number_; }

    /**
     * Get data group content.
     */
    const std::vector<uint8_t>& getContent() const { return content_; }

    /**
     * Get expected hash from SOD.
     */
    const std::optional<DataGroupHash>& getExpectedHash() const { return expectedHash_; }

    /**
     * Get actual calculated hash.
     */
    const std::optional<DataGroupHash>& getActualHash() const { return actualHash_; }

    /**
     * Set expected hash from SOD.
     */
    void setExpectedHash(const DataGroupHash& hash) {
        expectedHash_ = hash;
    }

    /**
     * Calculate actual hash from content using specified algorithm.
     */
    void calculateActualHash(const std::string& algorithm) {
        actualHash_ = DataGroupHash::calculate(content_, algorithm);
    }

    /**
     * Verify hash by comparing expected and actual hashes.
     *
     * @return true if hashes match
     */
    bool verifyHash() {
        if (!expectedHash_.has_value() || !actualHash_.has_value()) {
            throw shared::exception::DomainException(
                "HASH_NOT_READY",
                "Both expected and actual hashes must be set before verification"
            );
        }

        bool matches = (expectedHash_.value() == actualHash_.value());
        valid_ = matches;
        hashMismatchDetected_ = !matches;
        return matches;
    }

    /**
     * Check if this data group is valid (hash matches).
     */
    bool isValid() const { return valid_; }

    /**
     * Check if hash mismatch was detected.
     */
    bool isHashMismatchDetected() const { return hashMismatchDetected_; }

    /**
     * Get data group number as integer.
     */
    int getNumberValue() const { return toInt(number_); }
};

} // namespace pa::domain::model

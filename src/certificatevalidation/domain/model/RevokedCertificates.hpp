/**
 * @file RevokedCertificates.hpp
 * @brief Value Object for revoked certificates collection
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <unordered_set>
#include <string>
#include <algorithm>

namespace certificatevalidation::domain::model {

/**
 * @brief Revoked certificates collection Value Object
 *
 * Contains a set of revoked certificate serial numbers for efficient lookup.
 */
class RevokedCertificates : public shared::domain::ValueObject {
private:
    std::unordered_set<std::string> serialNumbers_;

    explicit RevokedCertificates(std::unordered_set<std::string> serialNumbers)
        : serialNumbers_(std::move(serialNumbers)) {}

public:
    /**
     * @brief Create RevokedCertificates from set of serial numbers
     */
    static RevokedCertificates of(std::unordered_set<std::string> serialNumbers) {
        return RevokedCertificates(std::move(serialNumbers));
    }

    /**
     * @brief Create empty RevokedCertificates
     */
    static RevokedCertificates empty() {
        return RevokedCertificates(std::unordered_set<std::string>{});
    }

    /**
     * @brief Check if a certificate serial number is revoked
     * @param serialNumber Certificate serial number (hex string)
     * @return true if revoked
     */
    [[nodiscard]] bool contains(const std::string& serialNumber) const {
        // Normalize serial number to uppercase for comparison
        std::string normalized = serialNumber;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);

        // Also try to match with lowercase
        if (serialNumbers_.find(normalized) != serialNumbers_.end()) {
            return true;
        }

        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
        return serialNumbers_.find(normalized) != serialNumbers_.end() ||
               serialNumbers_.find(serialNumber) != serialNumbers_.end();
    }

    /**
     * @brief Get count of revoked certificates
     */
    [[nodiscard]] size_t calculateCount() const noexcept {
        return serialNumbers_.size();
    }

    /**
     * @brief Get all revoked serial numbers
     */
    [[nodiscard]] const std::unordered_set<std::string>& getSerialNumbers() const noexcept {
        return serialNumbers_;
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        return serialNumbers_.empty();
    }
};

} // namespace certificatevalidation::domain::model

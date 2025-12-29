/**
 * @file CertificateId.hpp
 * @brief Value Object for Certificate identifier
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <uuid/uuid.h>
#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace certificatevalidation::domain::model {

/**
 * @brief Certificate identifier Value Object
 *
 * Provides unique identification for Certificate aggregate root.
 * Uses UUID v4 for global uniqueness.
 */
class CertificateId : public shared::domain::ValueObject {
private:
    std::string value_;

    explicit CertificateId(std::string value) : value_(std::move(value)) {
        validate();
    }

    void validate() const {
        if (value_.empty()) {
            throw std::invalid_argument("CertificateId cannot be empty");
        }
        // Basic UUID format validation (36 characters with hyphens)
        if (value_.length() != 36) {
            throw std::invalid_argument("CertificateId must be a valid UUID format");
        }
    }

public:
    /**
     * @brief Create CertificateId from existing UUID string
     */
    static CertificateId of(const std::string& value) {
        return CertificateId(value);
    }

    /**
     * @brief Generate new unique CertificateId
     */
    static CertificateId newId() {
        uuid_t uuid;
        uuid_generate_random(uuid);

        char str[37];
        uuid_unparse_lower(uuid, str);

        return CertificateId(std::string(str));
    }

    [[nodiscard]] const std::string& getValue() const noexcept {
        return value_;
    }

    bool operator==(const CertificateId& other) const noexcept {
        return value_ == other.value_;
    }

    bool operator!=(const CertificateId& other) const noexcept {
        return !(*this == other);
    }

    bool operator<(const CertificateId& other) const noexcept {
        return value_ < other.value_;
    }

    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace certificatevalidation::domain::model

// Hash specialization for CertificateId
namespace std {
    template<>
    struct hash<certificatevalidation::domain::model::CertificateId> {
        size_t operator()(const certificatevalidation::domain::model::CertificateId& id) const {
            return hash<string>()(id.getValue());
        }
    };
}

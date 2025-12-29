/**
 * @file CrlId.hpp
 * @brief Value Object for CRL identifier
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <uuid/uuid.h>
#include <string>
#include <stdexcept>

namespace certificatevalidation::domain::model {

/**
 * @brief CRL (Certificate Revocation List) identifier Value Object
 */
class CrlId : public shared::domain::ValueObject {
private:
    std::string value_;

    explicit CrlId(std::string value) : value_(std::move(value)) {
        validate();
    }

    void validate() const {
        if (value_.empty()) {
            throw std::invalid_argument("CrlId cannot be empty");
        }
        if (value_.length() != 36) {
            throw std::invalid_argument("CrlId must be a valid UUID format");
        }
    }

public:
    static CrlId of(const std::string& value) {
        return CrlId(value);
    }

    static CrlId newId() {
        uuid_t uuid;
        uuid_generate_random(uuid);

        char str[37];
        uuid_unparse_lower(uuid, str);

        return CrlId(std::string(str));
    }

    [[nodiscard]] const std::string& getValue() const noexcept {
        return value_;
    }

    bool operator==(const CrlId& other) const noexcept {
        return value_ == other.value_;
    }

    bool operator!=(const CrlId& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace certificatevalidation::domain::model

namespace std {
    template<>
    struct hash<certificatevalidation::domain::model::CrlId> {
        size_t operator()(const certificatevalidation::domain::model::CrlId& id) const {
            return hash<string>()(id.getValue());
        }
    };
}

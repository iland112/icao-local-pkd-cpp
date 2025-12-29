/**
 * @file UploadId.hpp
 * @brief Value Object for Upload ID (UUID)
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include "shared/exception/DomainException.hpp"
#include <string>
#include <regex>
#include <random>
#include <sstream>
#include <iomanip>

namespace fileupload::domain::model {

/**
 * @brief Upload ID Value Object (UUID v4)
 */
class UploadId : public shared::domain::StringValueObject {
private:
    explicit UploadId(std::string value) : StringValueObject(std::move(value)) {
        validate();
    }

    void validate() const override {
        static const std::regex uuidRegex(
            "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
            std::regex::icase
        );

        if (!std::regex_match(value_, uuidRegex)) {
            throw shared::exception::DomainException(
                "INVALID_UPLOAD_ID",
                "Upload ID must be a valid UUID v4: " + value_
            );
        }
    }

public:
    /**
     * @brief Create from existing UUID string
     */
    static UploadId of(const std::string& value) {
        return UploadId(value);
    }

    /**
     * @brief Generate a new UUID v4
     */
    static UploadId generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;

        uint64_t ab = dis(gen);
        uint64_t cd = dis(gen);

        // Set version (4) and variant (RFC 4122)
        ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        ss << std::setw(8) << (ab >> 32) << '-';
        ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << '-';
        ss << std::setw(4) << (ab & 0xFFFF) << '-';
        ss << std::setw(4) << (cd >> 48) << '-';
        ss << std::setw(12) << (cd & 0x0000FFFFFFFFFFFFULL);

        return UploadId(ss.str());
    }

    /**
     * @brief Get string representation
     */
    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace fileupload::domain::model

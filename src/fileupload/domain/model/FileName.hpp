/**
 * @file FileName.hpp
 * @brief Value Object for file name
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include "shared/exception/DomainException.hpp"
#include <string>
#include <algorithm>

namespace fileupload::domain::model {

/**
 * @brief File Name Value Object
 */
class FileName : public shared::domain::StringValueObject {
private:
    static constexpr size_t MAX_LENGTH = 255;

    explicit FileName(std::string value) : StringValueObject(std::move(value)) {
        validate();
    }

    void validate() const override {
        if (value_.empty()) {
            throw shared::exception::DomainException(
                "INVALID_FILE_NAME",
                "File name cannot be empty"
            );
        }

        if (value_.length() > MAX_LENGTH) {
            throw shared::exception::DomainException(
                "INVALID_FILE_NAME",
                "File name exceeds maximum length of " + std::to_string(MAX_LENGTH)
            );
        }

        // Check for invalid characters
        static const std::string invalidChars = "<>:\"|?*\\/";
        for (char c : value_) {
            if (invalidChars.find(c) != std::string::npos || c < 32) {
                throw shared::exception::DomainException(
                    "INVALID_FILE_NAME",
                    "File name contains invalid characters"
                );
            }
        }
    }

public:
    /**
     * @brief Create from string
     */
    static FileName of(const std::string& value) {
        return FileName(value);
    }

    /**
     * @brief Get file extension (without dot)
     */
    [[nodiscard]] std::string getExtension() const {
        size_t dotPos = value_.rfind('.');
        if (dotPos == std::string::npos || dotPos == value_.length() - 1) {
            return "";
        }
        return value_.substr(dotPos + 1);
    }

    /**
     * @brief Get base name (without extension)
     */
    [[nodiscard]] std::string getBaseName() const {
        size_t dotPos = value_.rfind('.');
        if (dotPos == std::string::npos) {
            return value_;
        }
        return value_.substr(0, dotPos);
    }

    /**
     * @brief Get string representation
     */
    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace fileupload::domain::model

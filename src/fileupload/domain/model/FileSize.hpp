/**
 * @file FileSize.hpp
 * @brief Value Object for file size
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include "shared/exception/DomainException.hpp"
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>

namespace fileupload::domain::model {

/**
 * @brief File Size Value Object
 */
class FileSize : public shared::domain::ValueObject<int64_t> {
private:
    static constexpr int64_t MAX_FILE_SIZE = 100 * 1024 * 1024;  // 100 MB

    explicit FileSize(int64_t value) : ValueObject<int64_t>(value) {
        validate();
    }

    void validate() const override {
        if (value_ < 0) {
            throw shared::exception::DomainException(
                "INVALID_FILE_SIZE",
                "File size cannot be negative"
            );
        }

        if (value_ > MAX_FILE_SIZE) {
            throw shared::exception::DomainException(
                "FILE_TOO_LARGE",
                "File size exceeds maximum allowed size of 100 MB"
            );
        }
    }

public:
    /**
     * @brief Create from bytes
     */
    static FileSize ofBytes(int64_t bytes) {
        return FileSize(bytes);
    }

    /**
     * @brief Create from kilobytes
     */
    static FileSize ofKilobytes(int64_t kb) {
        return FileSize(kb * 1024);
    }

    /**
     * @brief Create from megabytes
     */
    static FileSize ofMegabytes(int64_t mb) {
        return FileSize(mb * 1024 * 1024);
    }

    /**
     * @brief Get size in bytes
     */
    [[nodiscard]] int64_t toBytes() const noexcept {
        return value_;
    }

    /**
     * @brief Get size in kilobytes
     */
    [[nodiscard]] double toKilobytes() const noexcept {
        return static_cast<double>(value_) / 1024.0;
    }

    /**
     * @brief Get size in megabytes
     */
    [[nodiscard]] double toMegabytes() const noexcept {
        return static_cast<double>(value_) / (1024.0 * 1024.0);
    }

    /**
     * @brief Get human-readable string representation
     */
    [[nodiscard]] std::string toHumanReadable() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);

        if (value_ < 1024) {
            ss << value_ << " B";
        } else if (value_ < 1024 * 1024) {
            ss << toKilobytes() << " KB";
        } else {
            ss << toMegabytes() << " MB";
        }

        return ss.str();
    }

    /**
     * @brief Check if file is empty
     */
    [[nodiscard]] bool isEmpty() const noexcept {
        return value_ == 0;
    }
};

} // namespace fileupload::domain::model

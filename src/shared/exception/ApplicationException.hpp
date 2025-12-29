/**
 * @file ApplicationException.hpp
 * @brief Application layer exception class
 */

#pragma once

#include <stdexcept>
#include <string>

namespace shared::exception {

/**
 * @brief Exception for application layer errors
 *
 * Used for use case execution errors, business rule violations at application level.
 */
class ApplicationException : public std::runtime_error {
private:
    std::string code_;
    std::string message_;

public:
    /**
     * @brief Construct a new Application Exception
     * @param code Error code (e.g., "CSCA_NOT_FOUND")
     * @param message Human-readable error message
     */
    ApplicationException(std::string code, std::string message)
        : std::runtime_error(message),
          code_(std::move(code)),
          message_(std::move(message)) {}

    /**
     * @brief Get the error code
     */
    [[nodiscard]] const std::string& getCode() const noexcept {
        return code_;
    }

    /**
     * @brief Get the error message
     */
    [[nodiscard]] const std::string& getMessage() const noexcept {
        return message_;
    }
};

} // namespace shared::exception

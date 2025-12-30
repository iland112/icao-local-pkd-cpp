/**
 * @file DomainException.hpp
 * @brief Domain layer exception class
 */

#pragma once

#include <stdexcept>
#include <string>

namespace shared::exception {

/**
 * @brief Exception for domain layer errors
 *
 * Used when business rules are violated or domain invariants are broken.
 */
class DomainException : public std::runtime_error {
private:
    std::string code_;
    std::string message_;

public:
    /**
     * @brief Construct a new Domain Exception
     * @param code Error code (e.g., "INVALID_SOD")
     * @param message Human-readable error message
     */
    DomainException(std::string code, std::string message)
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

/**
 * @file InfrastructureException.hpp
 * @brief Infrastructure layer exception class
 */

#pragma once

#include <stdexcept>
#include <string>

namespace shared::exception {

/**
 * @brief Exception for infrastructure layer errors
 *
 * Used for database, LDAP, file system, and other infrastructure errors.
 */
class InfrastructureException : public std::runtime_error {
private:
    std::string code_;
    std::string message_;

public:
    /**
     * @brief Construct a new Infrastructure Exception
     * @param code Error code (e.g., "LDAP_CONNECTION_ERROR")
     * @param message Human-readable error message
     */
    InfrastructureException(std::string code, std::string message)
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

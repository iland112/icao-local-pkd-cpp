/**
 * @file exceptions.h
 * @brief Standard Exception Hierarchy
 *
 * Provides consistent exception types across all services
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include <stdexcept>
#include <string>

namespace common {

/**
 * @brief Base exception for all ICAO PKD exceptions
 */
class IcaoException : public std::runtime_error {
public:
    explicit IcaoException(const std::string& message)
        : std::runtime_error(message) {}
};

/**
 * @brief Database operation failed
 */
class DatabaseException : public IcaoException {
public:
    explicit DatabaseException(const std::string& message)
        : IcaoException("Database error: " + message) {}
};

/**
 * @brief LDAP operation failed
 */
class LdapException : public IcaoException {
public:
    explicit LdapException(const std::string& message)
        : IcaoException("LDAP error: " + message) {}
};

/**
 * @brief Certificate validation failed
 */
class ValidationException : public IcaoException {
public:
    explicit ValidationException(const std::string& message)
        : IcaoException("Validation error: " + message) {}
};

/**
 * @brief Configuration error
 */
class ConfigException : public IcaoException {
public:
    explicit ConfigException(const std::string& message)
        : IcaoException("Configuration error: " + message) {}
};

/**
 * @brief Parsing error (SOD, DG, MRZ)
 */
class ParsingException : public IcaoException {
public:
    explicit ParsingException(const std::string& message)
        : IcaoException("Parsing error: " + message) {}
};

/**
 * @brief Connection pool exhausted
 */
class PoolExhaustedException : public IcaoException {
public:
    explicit PoolExhaustedException(const std::string& poolType)
        : IcaoException(poolType + " connection pool exhausted") {}
};

} // namespace common

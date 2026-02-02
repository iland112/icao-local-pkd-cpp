/**
 * @file exceptions.h
 * @brief Exception hierarchy for PA Service
 *
 * Provides typed exceptions with error codes for better error handling
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#pragma once

#include <stdexcept>
#include <string>
#include "error_codes.h"

namespace common {

/**
 * @brief Base exception for all PA Service errors
 */
class PaServiceException : public std::runtime_error {
private:
    ErrorCode code_;
    std::string details_;

public:
    explicit PaServiceException(
        ErrorCode code,
        const std::string& message,
        const std::string& details = "")
        : std::runtime_error(message)
        , code_(code)
        , details_(details) {}

    /**
     * @brief Get error code
     */
    ErrorCode getCode() const {
        return code_;
    }

    /**
     * @brief Get error details
     */
    const std::string& getDetails() const {
        return details_;
    }

    /**
     * @brief Convert to ErrorResponse
     */
    ErrorResponse toErrorResponse() const {
        return ErrorResponse(code_, what(), details_);
    }
};

// =============================================================================
// Database Exceptions
// =============================================================================

class DatabaseException : public PaServiceException {
public:
    explicit DatabaseException(
        ErrorCode code,
        const std::string& message,
        const std::string& details = "")
        : PaServiceException(code, message, details) {}
};

class DbConnectionException : public DatabaseException {
public:
    explicit DbConnectionException(const std::string& details = "")
        : DatabaseException(
            ErrorCode::DB_CONNECTION_FAILED,
            "Failed to connect to database",
            details) {}
};

class DbQueryException : public DatabaseException {
public:
    explicit DbQueryException(const std::string& query, const std::string& error)
        : DatabaseException(
            ErrorCode::DB_QUERY_FAILED,
            "Database query failed",
            "Query: " + query + ", Error: " + error) {}
};

class DbNoDataException : public DatabaseException {
public:
    explicit DbNoDataException(const std::string& details = "")
        : DatabaseException(
            ErrorCode::DB_NO_DATA_FOUND,
            "No data found in database",
            details) {}
};

class DbTimeoutException : public DatabaseException {
public:
    explicit DbTimeoutException(const std::string& details = "")
        : DatabaseException(
            ErrorCode::DB_TIMEOUT,
            "Database operation timed out",
            details) {}
};

class DbPoolExhaustedException : public DatabaseException {
public:
    explicit DbPoolExhaustedException(const std::string& details = "")
        : DatabaseException(
            ErrorCode::DB_POOL_EXHAUSTED,
            "Database connection pool exhausted",
            details) {}
};

// =============================================================================
// LDAP Exceptions
// =============================================================================

class LdapException : public PaServiceException {
public:
    explicit LdapException(
        ErrorCode code,
        const std::string& message,
        const std::string& details = "")
        : PaServiceException(code, message, details) {}
};

class LdapConnectionException : public LdapException {
public:
    explicit LdapConnectionException(const std::string& ldapUrl, const std::string& error)
        : LdapException(
            ErrorCode::LDAP_CONNECTION_FAILED,
            "Failed to connect to LDAP server",
            "URL: " + ldapUrl + ", Error: " + error) {}
};

class LdapBindException : public LdapException {
public:
    explicit LdapBindException(const std::string& bindDn, const std::string& error)
        : LdapException(
            ErrorCode::LDAP_BIND_FAILED,
            "Failed to bind to LDAP server",
            "Bind DN: " + bindDn + ", Error: " + error) {}
};

class LdapSearchException : public LdapException {
public:
    explicit LdapSearchException(const std::string& baseDn, const std::string& filter, const std::string& error)
        : LdapException(
            ErrorCode::LDAP_SEARCH_FAILED,
            "LDAP search failed",
            "Base DN: " + baseDn + ", Filter: " + filter + ", Error: " + error) {}
};

class LdapNoSuchObjectException : public LdapException {
public:
    explicit LdapNoSuchObjectException(const std::string& dn)
        : LdapException(
            ErrorCode::LDAP_NO_SUCH_OBJECT,
            "LDAP object not found",
            "DN: " + dn) {}
};

class LdapTimeoutException : public LdapException {
public:
    explicit LdapTimeoutException(const std::string& details = "")
        : LdapException(
            ErrorCode::LDAP_TIMEOUT,
            "LDAP operation timed out",
            details) {}
};

// =============================================================================
// Repository Exceptions
// =============================================================================

class RepositoryException : public PaServiceException {
public:
    explicit RepositoryException(
        ErrorCode code,
        const std::string& message,
        const std::string& details = "")
        : PaServiceException(code, message, details) {}
};

class InvalidInputException : public RepositoryException {
public:
    explicit InvalidInputException(const std::string& fieldName, const std::string& reason)
        : RepositoryException(
            ErrorCode::REPO_INVALID_INPUT,
            "Invalid input",
            "Field: " + fieldName + ", Reason: " + reason) {}
};

class EntityNotFoundException : public RepositoryException {
public:
    explicit EntityNotFoundException(const std::string& entityType, const std::string& identifier)
        : RepositoryException(
            ErrorCode::REPO_ENTITY_NOT_FOUND,
            "Entity not found",
            "Type: " + entityType + ", ID: " + identifier) {}
};

class DuplicateEntityException : public RepositoryException {
public:
    explicit DuplicateEntityException(const std::string& entityType, const std::string& identifier)
        : RepositoryException(
            ErrorCode::REPO_DUPLICATE_ENTITY,
            "Duplicate entity",
            "Type: " + entityType + ", ID: " + identifier) {}
};

// =============================================================================
// Service Exceptions
// =============================================================================

class ServiceException : public PaServiceException {
public:
    explicit ServiceException(
        ErrorCode code,
        const std::string& message,
        const std::string& details = "")
        : PaServiceException(code, message, details) {}
};

class ServiceInvalidInputException : public ServiceException {
public:
    explicit ServiceInvalidInputException(const std::string& details)
        : ServiceException(
            ErrorCode::SERVICE_INVALID_INPUT,
            "Invalid service input",
            details) {}
};

class ServiceProcessingException : public ServiceException {
public:
    explicit ServiceProcessingException(const std::string& operation, const std::string& error)
        : ServiceException(
            ErrorCode::SERVICE_PROCESSING_FAILED,
            "Service processing failed",
            "Operation: " + operation + ", Error: " + error) {}
};

// =============================================================================
// Validation Exceptions
// =============================================================================

class ValidationException : public PaServiceException {
public:
    explicit ValidationException(
        ErrorCode code,
        const std::string& message,
        const std::string& details = "")
        : PaServiceException(code, message, details) {}
};

class InvalidMrzException : public ValidationException {
public:
    explicit InvalidMrzException(const std::string& reason)
        : ValidationException(
            ErrorCode::VALIDATION_INVALID_MRZ,
            "Invalid MRZ data",
            reason) {}
};

class InvalidSodException : public ValidationException {
public:
    explicit InvalidSodException(const std::string& reason)
        : ValidationException(
            ErrorCode::VALIDATION_INVALID_SOD,
            "Invalid SOD data",
            reason) {}
};

class HashMismatchException : public ValidationException {
public:
    explicit HashMismatchException(const std::string& dgNumber, const std::string& expected, const std::string& actual)
        : ValidationException(
            ErrorCode::VALIDATION_HASH_MISMATCH,
            "Data group hash mismatch",
            "DG: " + dgNumber + ", Expected: " + expected + ", Actual: " + actual) {}
};

class SignatureValidationException : public ValidationException {
public:
    explicit SignatureValidationException(const std::string& reason)
        : ValidationException(
            ErrorCode::VALIDATION_SIGNATURE_FAILED,
            "Signature validation failed",
            reason) {}
};

class CscaNotFoundException : public ValidationException {
public:
    explicit CscaNotFoundException(const std::string& issuerDn, const std::string& country)
        : ValidationException(
            ErrorCode::VALIDATION_CSCA_NOT_FOUND,
            "CSCA certificate not found",
            "Issuer: " + issuerDn + ", Country: " + country) {}
};

// =============================================================================
// Parsing Exceptions
// =============================================================================

class ParsingException : public PaServiceException {
public:
    explicit ParsingException(
        ErrorCode code,
        const std::string& message,
        const std::string& details = "")
        : PaServiceException(code, message, details) {}
};

class Asn1ParseException : public ParsingException {
public:
    explicit Asn1ParseException(const std::string& details)
        : ParsingException(
            ErrorCode::PARSE_ASN1_ERROR,
            "ASN.1 parsing error",
            details) {}
};

class DerParseException : public ParsingException {
public:
    explicit DerParseException(const std::string& details)
        : ParsingException(
            ErrorCode::PARSE_DER_ERROR,
            "DER parsing error",
            details) {}
};

class InvalidFormatException : public ParsingException {
public:
    explicit InvalidFormatException(const std::string& expectedFormat, const std::string& details = "")
        : ParsingException(
            ErrorCode::PARSE_INVALID_FORMAT,
            "Invalid format",
            "Expected: " + expectedFormat + (details.empty() ? "" : ", Details: " + details)) {}
};

} // namespace common

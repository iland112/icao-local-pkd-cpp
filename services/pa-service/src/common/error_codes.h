/**
 * @file error_codes.h
 * @brief Standardized error codes for PA Service
 *
 * Provides consistent error codes across all components
 * Format: COMPONENT_ERROR_TYPE_DETAIL
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <json/json.h>

namespace common {

/**
 * @brief Error code enumeration
 */
enum class ErrorCode {
    // Success
    SUCCESS = 0,

    // Database Errors (1000-1999)
    DB_CONNECTION_FAILED = 1001,
    DB_QUERY_FAILED = 1002,
    DB_NO_DATA_FOUND = 1003,
    DB_CONSTRAINT_VIOLATION = 1004,
    DB_TIMEOUT = 1005,
    DB_POOL_EXHAUSTED = 1006,

    // LDAP Errors (2000-2999)
    LDAP_CONNECTION_FAILED = 2001,
    LDAP_BIND_FAILED = 2002,
    LDAP_SEARCH_FAILED = 2003,
    LDAP_NO_SUCH_OBJECT = 2004,
    LDAP_TIMEOUT = 2005,
    LDAP_POOL_EXHAUSTED = 2006,

    // Repository Errors (3000-3999)
    REPO_INVALID_INPUT = 3001,
    REPO_ENTITY_NOT_FOUND = 3002,
    REPO_DUPLICATE_ENTITY = 3003,
    REPO_OPERATION_FAILED = 3004,

    // Service Errors (4000-4999)
    SERVICE_INVALID_INPUT = 4001,
    SERVICE_PROCESSING_FAILED = 4002,
    SERVICE_DEPENDENCY_FAILED = 4003,

    // Validation Errors (5000-5999)
    VALIDATION_INVALID_MRZ = 5001,
    VALIDATION_INVALID_SOD = 5002,
    VALIDATION_HASH_MISMATCH = 5003,
    VALIDATION_SIGNATURE_FAILED = 5004,
    VALIDATION_CERTIFICATE_EXPIRED = 5005,
    VALIDATION_CSCA_NOT_FOUND = 5006,
    VALIDATION_CRL_CHECK_FAILED = 5007,

    // Parsing Errors (6000-6999)
    PARSE_ASN1_ERROR = 6001,
    PARSE_DER_ERROR = 6002,
    PARSE_PEM_ERROR = 6003,
    PARSE_INVALID_FORMAT = 6004,
    PARSE_MISSING_FIELD = 6005,

    // System Errors (9000-9999)
    SYSTEM_INTERNAL_ERROR = 9001,
    SYSTEM_NOT_IMPLEMENTED = 9002,
    SYSTEM_RESOURCE_UNAVAILABLE = 9003,
    SYSTEM_TIMEOUT = 9004,
};

/**
 * @brief Convert error code to string
 */
inline std::string errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "SUCCESS";

        // Database
        case ErrorCode::DB_CONNECTION_FAILED: return "DB_CONNECTION_FAILED";
        case ErrorCode::DB_QUERY_FAILED: return "DB_QUERY_FAILED";
        case ErrorCode::DB_NO_DATA_FOUND: return "DB_NO_DATA_FOUND";
        case ErrorCode::DB_CONSTRAINT_VIOLATION: return "DB_CONSTRAINT_VIOLATION";
        case ErrorCode::DB_TIMEOUT: return "DB_TIMEOUT";
        case ErrorCode::DB_POOL_EXHAUSTED: return "DB_POOL_EXHAUSTED";

        // LDAP
        case ErrorCode::LDAP_CONNECTION_FAILED: return "LDAP_CONNECTION_FAILED";
        case ErrorCode::LDAP_BIND_FAILED: return "LDAP_BIND_FAILED";
        case ErrorCode::LDAP_SEARCH_FAILED: return "LDAP_SEARCH_FAILED";
        case ErrorCode::LDAP_NO_SUCH_OBJECT: return "LDAP_NO_SUCH_OBJECT";
        case ErrorCode::LDAP_TIMEOUT: return "LDAP_TIMEOUT";
        case ErrorCode::LDAP_POOL_EXHAUSTED: return "LDAP_POOL_EXHAUSTED";

        // Repository
        case ErrorCode::REPO_INVALID_INPUT: return "REPO_INVALID_INPUT";
        case ErrorCode::REPO_ENTITY_NOT_FOUND: return "REPO_ENTITY_NOT_FOUND";
        case ErrorCode::REPO_DUPLICATE_ENTITY: return "REPO_DUPLICATE_ENTITY";
        case ErrorCode::REPO_OPERATION_FAILED: return "REPO_OPERATION_FAILED";

        // Service
        case ErrorCode::SERVICE_INVALID_INPUT: return "SERVICE_INVALID_INPUT";
        case ErrorCode::SERVICE_PROCESSING_FAILED: return "SERVICE_PROCESSING_FAILED";
        case ErrorCode::SERVICE_DEPENDENCY_FAILED: return "SERVICE_DEPENDENCY_FAILED";

        // Validation
        case ErrorCode::VALIDATION_INVALID_MRZ: return "VALIDATION_INVALID_MRZ";
        case ErrorCode::VALIDATION_INVALID_SOD: return "VALIDATION_INVALID_SOD";
        case ErrorCode::VALIDATION_HASH_MISMATCH: return "VALIDATION_HASH_MISMATCH";
        case ErrorCode::VALIDATION_SIGNATURE_FAILED: return "VALIDATION_SIGNATURE_FAILED";
        case ErrorCode::VALIDATION_CERTIFICATE_EXPIRED: return "VALIDATION_CERTIFICATE_EXPIRED";
        case ErrorCode::VALIDATION_CSCA_NOT_FOUND: return "VALIDATION_CSCA_NOT_FOUND";
        case ErrorCode::VALIDATION_CRL_CHECK_FAILED: return "VALIDATION_CRL_CHECK_FAILED";

        // Parsing
        case ErrorCode::PARSE_ASN1_ERROR: return "PARSE_ASN1_ERROR";
        case ErrorCode::PARSE_DER_ERROR: return "PARSE_DER_ERROR";
        case ErrorCode::PARSE_PEM_ERROR: return "PARSE_PEM_ERROR";
        case ErrorCode::PARSE_INVALID_FORMAT: return "PARSE_INVALID_FORMAT";
        case ErrorCode::PARSE_MISSING_FIELD: return "PARSE_MISSING_FIELD";

        // System
        case ErrorCode::SYSTEM_INTERNAL_ERROR: return "SYSTEM_INTERNAL_ERROR";
        case ErrorCode::SYSTEM_NOT_IMPLEMENTED: return "SYSTEM_NOT_IMPLEMENTED";
        case ErrorCode::SYSTEM_RESOURCE_UNAVAILABLE: return "SYSTEM_RESOURCE_UNAVAILABLE";
        case ErrorCode::SYSTEM_TIMEOUT: return "SYSTEM_TIMEOUT";

        default: return "UNKNOWN_ERROR";
    }
}

/**
 * @brief Convert error code to HTTP status code
 */
inline int errorCodeToHttpStatus(ErrorCode code) {
    int numericCode = static_cast<int>(code);

    if (numericCode == 0) {
        return 200;  // Success
    } else if (numericCode >= 1000 && numericCode < 2000) {
        return 500;  // Database errors -> Internal Server Error
    } else if (numericCode >= 2000 && numericCode < 3000) {
        return 502;  // LDAP errors -> Bad Gateway
    } else if (numericCode >= 3000 && numericCode < 4000) {
        return 500;  // Repository errors -> Internal Server Error
    } else if (numericCode >= 4000 && numericCode < 5000) {
        return 500;  // Service errors -> Internal Server Error
    } else if (numericCode >= 5000 && numericCode < 6000) {
        return 400;  // Validation errors -> Bad Request
    } else if (numericCode >= 6000 && numericCode < 7000) {
        return 400;  // Parsing errors -> Bad Request
    } else if (numericCode >= 9000 && numericCode < 10000) {
        return 500;  // System errors -> Internal Server Error
    }

    return 500;  // Default to Internal Server Error
}

/**
 * @brief Error response builder
 */
class ErrorResponse {
private:
    ErrorCode code_;
    std::string message_;
    std::string details_;
    std::string requestId_;

public:
    ErrorResponse(ErrorCode code, const std::string& message, const std::string& details = "")
        : code_(code), message_(message), details_(details) {}

    /**
     * @brief Set request ID for tracing
     */
    ErrorResponse& setRequestId(const std::string& requestId) {
        requestId_ = requestId;
        return *this;
    }

    /**
     * @brief Convert to JSON response
     */
    Json::Value toJson() const {
        Json::Value json;
        json["success"] = false;
        json["error"]["code"] = errorCodeToString(code_);
        json["error"]["numericCode"] = static_cast<int>(code_);
        json["error"]["message"] = message_;

        if (!details_.empty()) {
            json["error"]["details"] = details_;
        }

        if (!requestId_.empty()) {
            json["requestId"] = requestId_;
        }

        return json;
    }

    /**
     * @brief Get HTTP status code
     */
    int getHttpStatus() const {
        return errorCodeToHttpStatus(code_);
    }

    /**
     * @brief Get error code
     */
    ErrorCode getCode() const {
        return code_;
    }

    /**
     * @brief Get error message
     */
    std::string getMessage() const {
        return message_;
    }
};

} // namespace common

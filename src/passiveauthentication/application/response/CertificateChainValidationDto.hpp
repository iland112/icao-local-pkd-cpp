#pragma once

#include <string>
#include <optional>
#include <chrono>

namespace pa::application::response {

/**
 * DTO for certificate chain validation result.
 */
struct CertificateChainValidationDto {
    bool valid;
    std::string dscSubjectDn;
    std::string dscSerialNumber;
    std::string cscaSubjectDn;
    std::string cscaSerialNumber;
    std::optional<std::chrono::system_clock::time_point> dscNotBefore;
    std::optional<std::chrono::system_clock::time_point> dscNotAfter;
    bool crlChecked;
    bool revoked;
    std::string crlStatus;
    std::string crlStatusDescription;
    std::string crlStatusDetailedDescription;
    std::string crlStatusSeverity;
    std::string crlMessage;
    std::optional<std::string> validationErrors;

    CertificateChainValidationDto()
        : valid(false), crlChecked(false), revoked(false) {}

    CertificateChainValidationDto(
        bool valid,
        std::string dscSubjectDn,
        std::string dscSerialNumber,
        std::string cscaSubjectDn,
        std::string cscaSerialNumber,
        std::optional<std::chrono::system_clock::time_point> dscNotBefore,
        std::optional<std::chrono::system_clock::time_point> dscNotAfter,
        bool crlChecked,
        bool revoked,
        std::string crlStatus,
        std::string crlStatusDescription,
        std::string crlStatusDetailedDescription,
        std::string crlStatusSeverity,
        std::string crlMessage,
        std::optional<std::string> validationErrors
    ) : valid(valid),
        dscSubjectDn(std::move(dscSubjectDn)),
        dscSerialNumber(std::move(dscSerialNumber)),
        cscaSubjectDn(std::move(cscaSubjectDn)),
        cscaSerialNumber(std::move(cscaSerialNumber)),
        dscNotBefore(dscNotBefore),
        dscNotAfter(dscNotAfter),
        crlChecked(crlChecked),
        revoked(revoked),
        crlStatus(std::move(crlStatus)),
        crlStatusDescription(std::move(crlStatusDescription)),
        crlStatusDetailedDescription(std::move(crlStatusDetailedDescription)),
        crlStatusSeverity(std::move(crlStatusSeverity)),
        crlMessage(std::move(crlMessage)),
        validationErrors(std::move(validationErrors)) {}
};

} // namespace pa::application::response

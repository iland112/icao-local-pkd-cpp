#pragma once

#include <string>
#include <optional>

namespace pa::application::response {

/**
 * DTO for SOD signature validation result.
 */
struct SodSignatureValidationDto {
    bool valid;
    std::optional<std::string> signatureAlgorithm;
    std::optional<std::string> hashAlgorithm;
    std::optional<std::string> validationErrors;

    SodSignatureValidationDto() : valid(false) {}

    SodSignatureValidationDto(
        bool valid,
        std::optional<std::string> signatureAlgorithm,
        std::optional<std::string> hashAlgorithm,
        std::optional<std::string> validationErrors
    ) : valid(valid),
        signatureAlgorithm(std::move(signatureAlgorithm)),
        hashAlgorithm(std::move(hashAlgorithm)),
        validationErrors(std::move(validationErrors)) {}
};

} // namespace pa::application::response

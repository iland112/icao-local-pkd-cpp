/**
 * @file validation_service.h
 * @brief Service for certificate validation and revalidation (3-step pipeline)
 */
#pragma once

#include "../repositories/validation_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include "../domain/models/validation_result.h"
#include <memory>
#include <json/json.h>

// Forward declarations for validation library types
namespace icao::validation {
    class ICscaProvider;
    class ICrlProvider;
    class TrustChainBuilder;
    class CrlChecker;
}

namespace icao::relay::services {

/**
 * @brief Service for certificate validation and revalidation operations
 *
 * 3-step revalidation pipeline:
 * 1. Expiration check (all certificates)
 * 2. Trust Chain re-validation (PENDING/INVALID DSCs with csca_found=FALSE)
 * 3. CRL re-check (VALID/EXPIRED_VALID DSCs)
 */
class ValidationService {
public:
    /**
     * @brief Constructor with full dependency injection
     * @param validationRepo Validation repository for data access
     * @param certRepo Certificate repository for CSCA cache
     * @param crlRepo CRL repository for CRL lookup
     */
    ValidationService(
        repositories::ValidationRepository* validationRepo,
        repositories::CertificateRepository* certRepo,
        repositories::CrlRepository* crlRepo
    );

    ~ValidationService();

    /**
     * @brief Full 3-step revalidation pipeline
     *
     * Step 1: Check expiration status changes
     * Step 2: Re-validate trust chains for PENDING DSCs
     * Step 3: Re-check CRL revocation for VALID DSCs
     *
     * @return JSON response with 3-step results
     */
    Json::Value revalidateAll();

private:
    repositories::ValidationRepository* validationRepo_;
    repositories::CertificateRepository* certRepo_;
    repositories::CrlRepository* crlRepo_;

    // Owned validation library instances
    std::unique_ptr<icao::validation::ICscaProvider> cscaProvider_;
    std::unique_ptr<icao::validation::ICrlProvider> crlProvider_;
    std::unique_ptr<icao::validation::TrustChainBuilder> trustChainBuilder_;
    std::unique_ptr<icao::validation::CrlChecker> crlChecker_;

    std::string determineValidationStatus(bool isExpired, const std::string& currentStatus);

    /**
     * @brief Step 2: Trust Chain re-validation for PENDING DSCs
     * @return JSON object with tcProcessed, tcNewlyValid, tcStillPending, tcErrors
     */
    Json::Value revalidateTrustChains();

    /**
     * @brief Step 3: CRL re-check for VALID DSCs
     * @return JSON object with crlChecked, crlRevoked, crlUnavailable, crlExpired, crlErrors
     */
    Json::Value recheckCrls();

    /**
     * @brief Decode hex-encoded certificate data to DER bytes
     */
    std::vector<uint8_t> decodeHexToDer(const std::string& hexData);
};

} // namespace icao::relay::services

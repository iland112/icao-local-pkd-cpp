/**
 * @file pa_verification_service.h
 * @brief Service for PA verification orchestration
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <json/json.h>
#include "../domain/models/pa_verification.h"
#include "../repositories/pa_verification_repository.h"
#include "../repositories/data_group_repository.h"
#include <sod_parser.h>
#include "certificate_validation_service.h"
#include "dsc_auto_registration_service.h"
#include <dg_parser.h>

namespace services {

/**
 * @brief ICAO 9303 Passive Authentication verification orchestrator
 *
 * Coordinates the full PA verification workflow: SOD parsing, certificate chain
 * validation, SOD signature verification, DG hash verification, and CRL checking.
 */
class PaVerificationService {
private:
    repositories::PaVerificationRepository* paRepo_;
    repositories::DataGroupRepository* dgRepo_;
    icao::SodParser* sodParser_;
    CertificateValidationService* certValidator_;
    icao::DgParser* dgParser_;
    DscAutoRegistrationService* dscAutoRegService_;

public:
    /**
     * @brief Constructor with service dependencies
     * @param paRepo PA verification repository
     * @param dgRepo Data group repository
     * @param sodParser SOD parser (ICAO 9303 CMS SignedData)
     * @param certValidator Certificate chain validation service
     * @param dgParser Data group parser
     * @param dscAutoRegService Optional DSC auto-registration service
     * @throws std::invalid_argument if required dependencies are nullptr
     */
    PaVerificationService(
        repositories::PaVerificationRepository* paRepo,
        repositories::DataGroupRepository* dgRepo,
        icao::SodParser* sodParser,
        CertificateValidationService* certValidator,
        icao::DgParser* dgParser,
        DscAutoRegistrationService* dscAutoRegService = nullptr
    );

    /** @brief Destructor */
    ~PaVerificationService() = default;

    /**
     * @brief Execute full ICAO 9303 Passive Authentication verification
     * @param sodData Raw SOD binary data (CMS SignedData)
     * @param dataGroups Map of DG number to raw DG binary data
     * @param documentNumber Travel document number
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @return JSON response with verification result and details
     */
    Json::Value verifyPassiveAuthentication(
        const std::vector<uint8_t>& sodData,
        const std::map<std::string, std::vector<uint8_t>>& dataGroups,
        const std::string& documentNumber,
        const std::string& countryCode
    );

    /**
     * @brief Get paginated PA verification history
     * @param limit Maximum results to return
     * @param offset Pagination offset
     * @param status Optional status filter ("VALID", "INVALID", "ERROR")
     * @param countryCode Optional country code filter
     * @return JSON response with paginated verification records
     */
    Json::Value getVerificationHistory(
        int limit,
        int offset,
        const std::string& status = "",
        const std::string& countryCode = ""
    );

    /**
     * @brief Get a single verification record by ID
     * @param id PA verification UUID
     * @return JSON verification record or null if not found
     */
    Json::Value getVerificationById(const std::string& id);

    /**
     * @brief Get PA verification statistics
     * @return JSON with totals by status, country, and success rate
     */
    Json::Value getStatistics();
};

} // namespace services

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
#include "sod_parser_service.h"
#include "certificate_validation_service.h"
#include "data_group_parser_service.h"

namespace services {

class PaVerificationService {
private:
    repositories::PaVerificationRepository* paRepo_;
    SodParserService* sodParser_;
    CertificateValidationService* certValidator_;
    DataGroupParserService* dgParser_;

public:
    PaVerificationService(
        repositories::PaVerificationRepository* paRepo,
        SodParserService* sodParser,
        CertificateValidationService* certValidator,
        DataGroupParserService* dgParser
    );
    ~PaVerificationService() = default;

    // Main PA verification workflow
    Json::Value verifyPassiveAuthentication(
        const std::vector<uint8_t>& sodData,
        const std::map<std::string, std::vector<uint8_t>>& dataGroups,
        const std::string& documentNumber,
        const std::string& countryCode
    );

    // History and statistics
    Json::Value getVerificationHistory(
        int limit,
        int offset,
        const std::string& status = "",
        const std::string& countryCode = ""
    );

    Json::Value getVerificationById(const std::string& id);
    Json::Value getStatistics();
};

} // namespace services

#pragma once

#include "passiveauthentication/domain/model/DataGroupNumber.hpp"
#include <vector>
#include <map>
#include <string>
#include <cstdint>

namespace pa::application::command {

/**
 * Command for performing Passive Authentication verification.
 */
class PerformPassiveAuthenticationCommand {
private:
    std::vector<uint8_t> sodBytes_;
    std::map<domain::model::DataGroupNumber, std::vector<uint8_t>> dataGroups_;
    std::string issuingCountry_;
    std::string documentNumber_;
    std::string requestIpAddress_;
    std::string requestUserAgent_;
    std::string requestedBy_;

public:
    PerformPassiveAuthenticationCommand(
        std::vector<uint8_t> sodBytes,
        std::map<domain::model::DataGroupNumber, std::vector<uint8_t>> dataGroups,
        std::string issuingCountry,
        std::string documentNumber
    ) : sodBytes_(std::move(sodBytes)),
        dataGroups_(std::move(dataGroups)),
        issuingCountry_(std::move(issuingCountry)),
        documentNumber_(std::move(documentNumber)) {}

    // Builder pattern for optional fields
    PerformPassiveAuthenticationCommand& withRequestMetadata(
        const std::string& ipAddress,
        const std::string& userAgent,
        const std::string& requestedBy
    ) {
        requestIpAddress_ = ipAddress;
        requestUserAgent_ = userAgent;
        requestedBy_ = requestedBy;
        return *this;
    }

    const std::vector<uint8_t>& getSodBytes() const { return sodBytes_; }
    const std::map<domain::model::DataGroupNumber, std::vector<uint8_t>>& getDataGroups() const { return dataGroups_; }
    const std::string& getIssuingCountry() const { return issuingCountry_; }
    const std::string& getDocumentNumber() const { return documentNumber_; }
    const std::string& getRequestIpAddress() const { return requestIpAddress_; }
    const std::string& getRequestUserAgent() const { return requestUserAgent_; }
    const std::string& getRequestedBy() const { return requestedBy_; }
};

} // namespace pa::application::command

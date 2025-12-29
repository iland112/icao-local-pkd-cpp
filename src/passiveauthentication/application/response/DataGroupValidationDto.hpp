#pragma once

#include "passiveauthentication/domain/model/DataGroupNumber.hpp"
#include <string>
#include <map>

namespace pa::application::response {

/**
 * Detail for a single data group validation.
 */
struct DataGroupDetailDto {
    bool valid;
    std::string expectedHash;
    std::string actualHash;

    DataGroupDetailDto() : valid(false) {}

    DataGroupDetailDto(bool valid, std::string expectedHash, std::string actualHash)
        : valid(valid),
          expectedHash(std::move(expectedHash)),
          actualHash(std::move(actualHash)) {}
};

/**
 * DTO for data group hash validation result.
 */
struct DataGroupValidationDto {
    int totalGroups;
    int validGroups;
    int invalidGroups;
    std::map<domain::model::DataGroupNumber, DataGroupDetailDto> details;

    DataGroupValidationDto() : totalGroups(0), validGroups(0), invalidGroups(0) {}

    DataGroupValidationDto(
        int totalGroups,
        int validGroups,
        int invalidGroups,
        std::map<domain::model::DataGroupNumber, DataGroupDetailDto> details
    ) : totalGroups(totalGroups),
        validGroups(validGroups),
        invalidGroups(invalidGroups),
        details(std::move(details)) {}
};

} // namespace pa::application::response

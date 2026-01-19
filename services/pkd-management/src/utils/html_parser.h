#pragma once

#include <string>
#include <vector>
#include "../domain/models/icao_version.h"

namespace utils {

/**
 * @brief HTML parser for ICAO PKD portal
 *
 * Extracts LDIF file information from the public ICAO PKD download page.
 * Uses regex pattern matching to find file names and version numbers.
 */
class HtmlParser {
public:
    /**
     * @brief Parse ICAO portal HTML and extract version information
     *
     * Looks for patterns like:
     * - icaopkd-001-dsccrl-NNNNNN.ldif (DSC/CRL collection)
     * - icaopkd-002-ml-NNNNNN.ldif (Master List collection)
     *
     * @param html Raw HTML content from ICAO portal
     * @return Vector of detected ICAO versions
     */
    static std::vector<domain::models::IcaoVersion> parseVersions(const std::string& html);

private:
    /**
     * @brief Extract DSC/CRL file versions
     */
    static std::vector<domain::models::IcaoVersion> parseDscCrlVersions(const std::string& html);

    /**
     * @brief Extract Master List file versions
     */
    static std::vector<domain::models::IcaoVersion> parseMasterListVersions(const std::string& html);

    /**
     * @brief Get current timestamp in ISO 8601 format
     */
    static std::string getCurrentTimestamp();
};

} // namespace utils

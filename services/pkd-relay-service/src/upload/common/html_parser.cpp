/**
 * @file html_parser.cpp
 * @brief HTML parser implementation for ICAO PKD portal
 */

#include "html_parser.h"
#include <regex>
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <set>

namespace utils {

std::vector<domain::models::IcaoVersion> HtmlParser::parseVersions(const std::string& html) {
    spdlog::info("[HtmlParser] Parsing ICAO portal HTML ({} bytes)", html.size());

    std::vector<domain::models::IcaoVersion> versions;

    // Parse DSC/CRL versions
    auto dscVersions = parseDscCrlVersions(html);
    versions.insert(versions.end(), dscVersions.begin(), dscVersions.end());

    // Parse Master List versions
    auto mlVersions = parseMasterListVersions(html);
    versions.insert(versions.end(), mlVersions.begin(), mlVersions.end());

    // Parse DSC_NC (Non-Conformant) versions
    auto dscNcVersions = parseDscNcVersions(html);
    versions.insert(versions.end(), dscNcVersions.begin(), dscNcVersions.end());

    spdlog::info("[HtmlParser] Found {} total versions (DSC/CRL: {}, ML: {}, DSC_NC: {})",
                versions.size(), dscVersions.size(), mlVersions.size(), dscNcVersions.size());

    return versions;
}

std::vector<domain::models::IcaoVersion> HtmlParser::parseDscCrlVersions(const std::string& html) {
    std::vector<domain::models::IcaoVersion> versions;

    // NEW: Parse from table format (2026-01 portal update)
    // HTML structure: <td>eMRTD Certificates (DSC, BCSC, BCSC-NC) and CRL</td><td>009668</td>
    std::regex tablePattern(
        R"(eMRTD Certificates.*?CRL</td>\s*<td>(\d+)</td>)",
        std::regex::icase
    );

    std::smatch tableMatch;
    if (std::regex_search(html, tableMatch, tablePattern)) {
        int versionNumber = std::stoi(tableMatch.str(1));
        std::string fileName = "icaopkd-001-complete-" +
                              std::string(6 - std::to_string(versionNumber).length(), '0') +
                              std::to_string(versionNumber) + ".ldif";

        auto version = domain::models::IcaoVersion::createDetected(
            "DSC_CRL",
            fileName,
            versionNumber
        );
        version.detectedAt = getCurrentTimestamp();
        versions.push_back(version);

        spdlog::info("[HtmlParser] Found DSC/CRL from table: {} (version {})",
                    fileName, versionNumber);
        return versions;
    }

    // FALLBACK: Old format with direct file links
    // Matches: icaopkd-001-complete-009668.ldif
    std::regex pattern(R"(icaopkd-001-complete-(\d+)\.ldif)");
    std::sregex_iterator iter(html.begin(), html.end(), pattern);
    std::sregex_iterator end;
    std::set<int> seenVersions;

    while (iter != end) {
        std::smatch match = *iter;
        int versionNumber = std::stoi(match.str(1));

        if (seenVersions.find(versionNumber) != seenVersions.end()) {
            ++iter;
            continue;
        }
        seenVersions.insert(versionNumber);

        std::string fileName = match.str(0);
        auto version = domain::models::IcaoVersion::createDetected(
            "DSC_CRL",
            fileName,
            versionNumber
        );
        version.detectedAt = getCurrentTimestamp();
        versions.push_back(version);

        spdlog::debug("[HtmlParser] Found DSC/CRL from link: {} (version {})",
                     fileName, versionNumber);
        ++iter;
    }

    return versions;
}

std::vector<domain::models::IcaoVersion> HtmlParser::parseMasterListVersions(const std::string& html) {
    std::vector<domain::models::IcaoVersion> versions;

    // NEW: Parse from table format (2026-01 portal update)
    // HTML structure: <td>CSCA MasterList</td><td>000334</td>
    std::regex tablePattern(
        R"(CSCA\s+MasterList</td>\s*<td>(\d+)</td>)",
        std::regex::icase
    );

    std::smatch tableMatch;
    if (std::regex_search(html, tableMatch, tablePattern)) {
        int versionNumber = std::stoi(tableMatch.str(1));
        std::string fileName = "icaopkd-002-complete-" +
                              std::string(6 - std::to_string(versionNumber).length(), '0') +
                              std::to_string(versionNumber) + ".ldif";

        auto version = domain::models::IcaoVersion::createDetected(
            "MASTERLIST",
            fileName,
            versionNumber
        );
        version.detectedAt = getCurrentTimestamp();
        versions.push_back(version);

        spdlog::info("[HtmlParser] Found CSCA Master List from table: {} (version {})",
                    fileName, versionNumber);
        return versions;
    }

    // FALLBACK: Old format with direct file links
    // Matches: icaopkd-002-complete-000334.ldif
    std::regex pattern(R"(icaopkd-002-complete-(\d+)\.ldif)");
    std::sregex_iterator iter(html.begin(), html.end(), pattern);
    std::sregex_iterator end;
    std::set<int> seenVersions;

    while (iter != end) {
        std::smatch match = *iter;
        int versionNumber = std::stoi(match.str(1));

        if (seenVersions.find(versionNumber) != seenVersions.end()) {
            ++iter;
            continue;
        }
        seenVersions.insert(versionNumber);

        std::string fileName = match.str(0);
        auto version = domain::models::IcaoVersion::createDetected(
            "MASTERLIST",
            fileName,
            versionNumber
        );
        version.detectedAt = getCurrentTimestamp();
        versions.push_back(version);

        spdlog::debug("[HtmlParser] Found CSCA Master List from link: {} (version {})",
                     fileName, versionNumber);
        ++iter;
    }

    return versions;
}

std::vector<domain::models::IcaoVersion> HtmlParser::parseDscNcVersions(const std::string& html) {
    std::vector<domain::models::IcaoVersion> versions;

    // NEW: Parse from table format (2026-01 portal update)
    // HTML structure: <td>Non Conformant eMRTD PKI objects</td><td>000090</td>
    std::regex tablePattern(
        R"(Non\s+Conformant\s+eMRTD\s+PKI\s+objects</td>\s*<td>(\d+)</td>)",
        std::regex::icase
    );

    std::smatch tableMatch;
    if (std::regex_search(html, tableMatch, tablePattern)) {
        int versionNumber = std::stoi(tableMatch.str(1));
        std::string fileName = "icaopkd-003-complete-" +
                              std::string(6 - std::to_string(versionNumber).length(), '0') +
                              std::to_string(versionNumber) + ".ldif";

        auto version = domain::models::IcaoVersion::createDetected(
            "DSC_NC",
            fileName,
            versionNumber
        );
        version.detectedAt = getCurrentTimestamp();
        versions.push_back(version);

        spdlog::info("[HtmlParser] Found DSC_NC from table: {} (version {})",
                    fileName, versionNumber);
        return versions;
    }

    // FALLBACK: Old format with direct file links
    // Matches: icaopkd-003-complete-000090.ldif
    std::regex pattern(R"(icaopkd-003-complete-(\d+)\.ldif)");
    std::sregex_iterator iter(html.begin(), html.end(), pattern);
    std::sregex_iterator end;
    std::set<int> seenVersions;

    while (iter != end) {
        std::smatch match = *iter;
        int versionNumber = std::stoi(match.str(1));

        if (seenVersions.find(versionNumber) != seenVersions.end()) {
            ++iter;
            continue;
        }
        seenVersions.insert(versionNumber);

        std::string fileName = match.str(0);
        auto version = domain::models::IcaoVersion::createDetected(
            "DSC_NC",
            fileName,
            versionNumber
        );
        version.detectedAt = getCurrentTimestamp();
        versions.push_back(version);

        spdlog::debug("[HtmlParser] Found DSC_NC from link: {} (version {})",
                     fileName, versionNumber);
        ++iter;
    }

    return versions;
}

std::string HtmlParser::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm tm_now;
    gmtime_r(&time_t_now, &tm_now);  // Thread-safe UTC conversion

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace utils

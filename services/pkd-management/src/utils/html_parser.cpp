#include "html_parser.h"
#include <regex>
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>

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

    spdlog::info("[HtmlParser] Found {} total versions (DSC/CRL: {}, ML: {})",
                versions.size(), dscVersions.size(), mlVersions.size());

    return versions;
}

std::vector<domain::models::IcaoVersion> HtmlParser::parseDscCrlVersions(const std::string& html) {
    std::vector<domain::models::IcaoVersion> versions;

    // Regex pattern for DSC/CRL files
    // Matches: icaopkd-001-dsccrl-005973.ldif
    std::regex pattern(R"(icaopkd-001-dsccrl-(\d+)\.ldif)");

    std::sregex_iterator iter(html.begin(), html.end(), pattern);
    std::sregex_iterator end;

    std::set<int> seenVersions;  // Avoid duplicates

    while (iter != end) {
        std::smatch match = *iter;
        int versionNumber = std::stoi(match.str(1));

        // Check for duplicates
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

        spdlog::debug("[HtmlParser] Found DSC/CRL: {} (version {})",
                     fileName, versionNumber);

        ++iter;
    }

    return versions;
}

std::vector<domain::models::IcaoVersion> HtmlParser::parseMasterListVersions(const std::string& html) {
    std::vector<domain::models::IcaoVersion> versions;

    // Regex pattern for Master List files
    // Matches: icaopkd-002-ml-000216.ldif
    std::regex pattern(R"(icaopkd-002-ml-(\d+)\.ldif)");

    std::sregex_iterator iter(html.begin(), html.end(), pattern);
    std::sregex_iterator end;

    std::set<int> seenVersions;  // Avoid duplicates

    while (iter != end) {
        std::smatch match = *iter;
        int versionNumber = std::stoi(match.str(1));

        // Check for duplicates
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

        spdlog::debug("[HtmlParser] Found Master List: {} (version {})",
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

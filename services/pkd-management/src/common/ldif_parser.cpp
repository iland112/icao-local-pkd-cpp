/**
 * @file ldif_parser.cpp
 * @brief LDIF Structure Parser Implementation
 *
 * @author SmartCore Inc.
 * @date 2026-01-31
 * @version v2.2.2
 */

#include "ldif_parser.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace icao {
namespace ldif {

// =============================================================================
// Public Methods
// =============================================================================

LdifStructure LdifParser::parse(const std::string& filePath, int maxEntries) {
    spdlog::info("Parsing LDIF file: {} (maxEntries: {})", filePath, maxEntries);

    // Read file content
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open LDIF file: " + filePath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    if (content.empty()) {
        throw std::runtime_error("LDIF file is empty: " + filePath);
    }

    // Count total entries first (for truncation detection)
    int totalEntries = countEntries(content);
    spdlog::debug("Total LDIF entries: {}", totalEntries);

    // Parse entries up to maxEntries
    LdifStructure result;
    result.totalEntries = totalEntries;
    result.totalAttributes = 0;
    result.truncated = (totalEntries > maxEntries);

    size_t pos = 0;
    int lineNumber = 1;
    int parsedCount = 0;

    while (pos != std::string::npos && parsedCount < maxEntries) {
        LdifEntryStructure entry;
        size_t nextPos = parseEntry(content, pos, lineNumber, entry);

        if (nextPos == pos) {
            // No more entries
            break;
        }

        if (!entry.dn.empty()) {
            // Extract objectClass
            entry.objectClass = extractObjectClass(entry);

            // Update statistics
            result.totalAttributes += static_cast<int>(entry.attributes.size());
            result.objectClassCounts[entry.objectClass]++;

            result.entries.push_back(std::move(entry));
            parsedCount++;
        }

        pos = nextPos;
    }

    spdlog::info("Parsed {} entries (total: {}, truncated: {})",
                 result.entries.size(), result.totalEntries, result.truncated);

    return result;
}

std::pair<bool, size_t> LdifParser::parseBinaryAttribute(const std::string& value) {
    // Binary attributes in LDIF are base64 encoded
    // We detect them by the presence of base64 characters and calculate size

    if (value.empty()) {
        return {false, 0};
    }

    // Check if value contains only valid base64 characters
    bool isBase64 = std::all_of(value.begin(), value.end(), [](char c) {
        return std::isalnum(c) || c == '+' || c == '/' || c == '=' || std::isspace(c);
    });

    if (!isBase64) {
        return {false, 0};
    }

    // Remove whitespace to get actual base64 length
    std::string cleanValue;
    std::copy_if(value.begin(), value.end(), std::back_inserter(cleanValue),
                 [](char c) { return !std::isspace(c); });

    // Calculate decoded size
    size_t decodedSize = calculateDecodedSize(cleanValue.length());

    // Consider it binary if decoded size is reasonably large (> 100 bytes)
    // Small values might just be text that happens to be base64-like
    bool isBinary = decodedSize > 100;

    return {isBinary, decodedSize};
}

std::vector<std::string> LdifParser::extractDnComponents(const std::string& dn) {
    std::vector<std::string> components;

    if (dn.empty()) {
        return components;
    }

    // DN components are separated by commas (,)
    // e.g., "cn=CSCA,o=csca,c=FR,dc=data,dc=download,..."

    size_t start = 0;
    bool inQuotes = false;

    for (size_t i = 0; i < dn.length(); ++i) {
        if (dn[i] == '"') {
            inQuotes = !inQuotes;
        } else if (dn[i] == ',' && !inQuotes) {
            // Found component separator
            std::string component = dn.substr(start, i - start);

            // Trim whitespace
            size_t first = component.find_first_not_of(" \t");
            size_t last = component.find_last_not_of(" \t");

            if (first != std::string::npos && last != std::string::npos) {
                component = component.substr(first, last - first + 1);
                if (!component.empty()) {
                    components.push_back(component);
                }
            }

            start = i + 1;
        }
    }

    // Add last component
    if (start < dn.length()) {
        std::string component = dn.substr(start);

        size_t first = component.find_first_not_of(" \t");
        size_t last = component.find_last_not_of(" \t");

        if (first != std::string::npos && last != std::string::npos) {
            component = component.substr(first, last - first + 1);
            if (!component.empty()) {
                components.push_back(component);
            }
        }
    }

    return components;
}

size_t LdifParser::calculateDecodedSize(size_t base64Length) {
    if (base64Length == 0) {
        return 0;
    }

    // Base64 encoding: 3 bytes → 4 characters
    // Decoded size = (base64Length / 4) * 3

    // Account for padding (=)
    size_t decodedSize = (base64Length / 4) * 3;

    // Adjust for padding
    if (base64Length % 4 != 0) {
        decodedSize += (base64Length % 4) - 1;
    }

    return decodedSize;
}

// =============================================================================
// Private Methods
// =============================================================================

size_t LdifParser::parseEntry(
    const std::string& content,
    size_t startPos,
    int& lineNumber,
    LdifEntryStructure& entry
) {
    entry.dn.clear();
    entry.attributes.clear();
    entry.objectClass.clear();
    entry.lineNumber = lineNumber;

    std::string currentAttrName;
    std::string currentAttrValue;
    bool inContinuation = false;
    bool isDnContinuation = false;  // Track if we're in DN continuation

    auto finalizeAttribute = [&]() {
        if (!currentAttrName.empty()) {
            LdifAttribute attr;
            attr.name = currentAttrName;

            // Detect binary attribute
            bool isBinary = (currentAttrName.find(";binary") != std::string::npos);

            if (isBinary) {
                auto [detected, size] = parseBinaryAttribute(currentAttrValue);
                attr.isBinary = true;
                attr.binarySize = size;

                // Replace value with indicator
                if (currentAttrName == "pkdMasterListContent;binary") {
                    attr.value = "[Binary CMS Data: " + std::to_string(size) + " bytes]";
                } else if (currentAttrName == "userCertificate;binary" ||
                           currentAttrName == "cACertificate;binary") {
                    attr.value = "[Binary Certificate: " + std::to_string(size) + " bytes]";
                } else if (currentAttrName == "certificateRevocationList;binary") {
                    attr.value = "[Binary CRL: " + std::to_string(size) + " bytes]";
                } else {
                    attr.value = "[Binary Data: " + std::to_string(size) + " bytes]";
                }
            } else {
                attr.isBinary = false;
                attr.binarySize = 0;
                attr.value = currentAttrValue;
            }

            entry.attributes.push_back(attr);
            currentAttrName.clear();
            currentAttrValue.clear();
        }
    };

    std::istringstream stream(content.substr(startPos));
    std::string line;
    size_t bytesRead = startPos;
    bool entryStarted = false;

    while (std::getline(stream, line)) {
        bytesRead += line.length() + 1; // +1 for newline
        lineNumber++;

        // Remove trailing CR if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Empty line = entry separator
        if (line.empty()) {
            if (entryStarted) {
                finalizeAttribute();
                return bytesRead;
            }
            continue;
        }

        // Skip comments
        if (line[0] == '#') {
            continue;
        }

        // Continuation line (starts with space)
        if (line[0] == ' ') {
            if (inContinuation) {
                if (isDnContinuation) {
                    entry.dn += line.substr(1);
                } else {
                    currentAttrValue += line.substr(1);
                }
            }
            continue;
        }

        // Finalize previous attribute
        finalizeAttribute();
        inContinuation = false;
        isDnContinuation = false;

        // Parse attribute line
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }

        currentAttrName = line.substr(0, colonPos);

        // Check for base64 encoding (::)
        if (colonPos + 1 < line.size() && line[colonPos + 1] == ':') {
            // Base64 encoded value
            if (currentAttrName.find(";binary") == std::string::npos) {
                currentAttrName += ";binary";
            }

            size_t valueStart = colonPos + 2;
            while (valueStart < line.size() && line[valueStart] == ' ') {
                valueStart++;
            }
            currentAttrValue = line.substr(valueStart);
        } else {
            // Regular value
            size_t valueStart = colonPos + 1;
            while (valueStart < line.size() && line[valueStart] == ' ') {
                valueStart++;
            }
            currentAttrValue = line.substr(valueStart);
        }

        // Special handling for dn
        if (currentAttrName == "dn") {
            entry.dn = currentAttrValue;
            entry.lineNumber = lineNumber - 1; // Entry starts at dn: line
            entryStarted = true;
            inContinuation = true;
            isDnContinuation = true;  // Mark that we're in DN continuation
            // Don't add dn to attributes, it's stored separately
            currentAttrName.clear();
            currentAttrValue.clear();
        } else {
            inContinuation = true;
            isDnContinuation = false;
        }
    }

    // Finalize last attribute
    if (entryStarted) {
        finalizeAttribute();
    }

    return entryStarted ? bytesRead : std::string::npos;
}

int LdifParser::countEntries(const std::string& content) {
    int count = 0;
    std::istringstream stream(content);
    std::string line;
    bool inEntry = false;

    while (std::getline(stream, line)) {
        // Remove trailing CR if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            if (inEntry) {
                count++;
                inEntry = false;
            }
        } else if (line.substr(0, 3) == "dn:") {
            inEntry = true;
        }
    }

    // Count last entry if exists
    if (inEntry) {
        count++;
    }

    return count;
}

std::string LdifParser::extractObjectClass(const LdifEntryStructure& entry) {
    // Find objectClass attribute
    for (const auto& attr : entry.attributes) {
        if (attr.name == "objectClass") {
            // Return the first (or only) objectClass value
            // For multi-valued objectClass, we take the most specific one
            // (usually the last one, but for simplicity we take the first)
            return attr.value;
        }
    }

    return "unknown";
}

}  // namespace ldif
}  // namespace icao

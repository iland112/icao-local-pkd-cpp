/**
 * @file LdifParser.hpp
 * @brief LDIF file parser for PKD data
 */

#pragma once

#include "OpenSslCertificateParser.hpp"
#include "../../domain/model/ParsedFile.hpp"
#include "../../domain/model/CertificateData.hpp"
#include "../../domain/model/CrlData.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <functional>

namespace fileparsing::infrastructure::adapter {

using namespace fileparsing::domain::model;

/**
 * @brief LDIF Entry representation
 */
struct LdifEntry {
    std::string dn;
    std::map<std::string, std::vector<std::string>> attributes;

    bool hasAttribute(const std::string& name) const {
        return attributes.find(name) != attributes.end();
    }

    std::vector<std::string> getAttribute(const std::string& name) const {
        auto it = attributes.find(name);
        if (it != attributes.end()) {
            return it->second;
        }
        return {};
    }

    std::string getFirstAttribute(const std::string& name) const {
        auto values = getAttribute(name);
        return values.empty() ? "" : values[0];
    }
};

/**
 * @brief Progress callback type
 */
using ProgressCallback = std::function<void(int processed, int total)>;

/**
 * @brief LDIF file parser
 */
class LdifParser {
private:
    static constexpr const char* ATTR_USER_CERTIFICATE = "userCertificate;binary";
    static constexpr const char* ATTR_CA_CERTIFICATE = "cACertificate;binary";
    static constexpr const char* ATTR_CRL = "certificateRevocationList;binary";
    static constexpr const char* ATTR_MASTER_LIST = "pkdMasterListContent";

    /**
     * @brief Decode base64 string
     */
    static std::vector<uint8_t> base64Decode(const std::string& encoded) {
        static const std::string base64Chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::vector<uint8_t> result;
        std::vector<int> decodingTable(256, -1);
        for (size_t i = 0; i < base64Chars.size(); i++) {
            decodingTable[static_cast<unsigned char>(base64Chars[i])] = static_cast<int>(i);
        }

        int val = 0;
        int valb = -8;
        for (unsigned char c : encoded) {
            if (decodingTable[c] == -1) continue;
            val = (val << 6) + decodingTable[c];
            valb += 6;
            if (valb >= 0) {
                result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }

    /**
     * @brief Parse LDIF content into entries
     */
    static std::vector<LdifEntry> parseLdifContent(const std::string& content) {
        std::vector<LdifEntry> entries;
        LdifEntry currentEntry;
        std::string currentAttrName;
        std::string currentAttrValue;
        bool inContinuation = false;

        std::istringstream stream(content);
        std::string line;

        auto finalizeAttribute = [&]() {
            if (!currentAttrName.empty()) {
                currentEntry.attributes[currentAttrName].push_back(currentAttrValue);
                currentAttrName.clear();
                currentAttrValue.clear();
            }
        };

        auto finalizeEntry = [&]() {
            finalizeAttribute();
            if (!currentEntry.dn.empty()) {
                entries.push_back(std::move(currentEntry));
                currentEntry = LdifEntry();
            }
        };

        while (std::getline(stream, line)) {
            // Remove trailing CR if present (Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Empty line marks end of entry
            if (line.empty()) {
                finalizeEntry();
                inContinuation = false;
                continue;
            }

            // Skip comments
            if (line[0] == '#') {
                continue;
            }

            // Continuation line (starts with space)
            if (line[0] == ' ') {
                if (inContinuation) {
                    currentAttrValue += line.substr(1);
                }
                continue;
            }

            // New attribute line
            finalizeAttribute();
            inContinuation = false;

            size_t colonPos = line.find(':');
            if (colonPos == std::string::npos) {
                continue;
            }

            currentAttrName = line.substr(0, colonPos);

            // Check for base64 encoded value (::)
            if (colonPos + 1 < line.size() && line[colonPos + 1] == ':') {
                // Base64 encoded, keep the :: marker in attribute name
                currentAttrName += ";binary";
                size_t valueStart = colonPos + 2;
                while (valueStart < line.size() && line[valueStart] == ' ') {
                    valueStart++;
                }
                currentAttrValue = line.substr(valueStart);
            } else {
                // Plain value
                size_t valueStart = colonPos + 1;
                while (valueStart < line.size() && line[valueStart] == ' ') {
                    valueStart++;
                }
                currentAttrValue = line.substr(valueStart);
            }

            // Handle DN specially
            if (currentAttrName == "dn") {
                currentEntry.dn = currentAttrValue;
                currentAttrName.clear();
                currentAttrValue.clear();
            } else {
                inContinuation = true;
            }
        }

        // Finalize last entry
        finalizeEntry();

        return entries;
    }

public:
    /**
     * @brief Parse LDIF file content
     */
    static ParsedFile parse(
        const fileupload::domain::model::UploadId& uploadId,
        const std::vector<uint8_t>& content,
        ProgressCallback progressCallback = nullptr
    ) {
        std::string contentStr(content.begin(), content.end());
        return parse(uploadId, contentStr, progressCallback);
    }

    /**
     * @brief Parse LDIF string content
     */
    static ParsedFile parse(
        const fileupload::domain::model::UploadId& uploadId,
        const std::string& content,
        ProgressCallback progressCallback = nullptr
    ) {
        ParsedFile result(uploadId);

        // Parse LDIF entries
        auto entries = parseLdifContent(content);
        result.setTotalEntries(static_cast<int>(entries.size()));

        spdlog::info("Parsing {} LDIF entries for upload {}", entries.size(), uploadId.toString());

        int processed = 0;
        for (const auto& entry : entries) {
            try {
                parseEntry(entry, result);
            } catch (const std::exception& e) {
                spdlog::warn("Error parsing entry {}: {}", entry.dn, e.what());
                result.addError({entry.dn, "PARSE_ERROR", e.what()});
            }

            result.incrementProcessedEntries();
            processed++;

            // Report progress every 100 entries
            if (progressCallback && processed % 100 == 0) {
                progressCallback(processed, static_cast<int>(entries.size()));
            }
        }

        // Final progress update
        if (progressCallback) {
            progressCallback(processed, static_cast<int>(entries.size()));
        }

        spdlog::info("Parsed {} certificates, {} CRLs, {} errors from LDIF",
            result.getCertificates().size(),
            result.getCrls().size(),
            result.getErrors().size()
        );

        return result;
    }

private:
    /**
     * @brief Parse single LDIF entry
     */
    static void parseEntry(const LdifEntry& entry, ParsedFile& result) {
        // Check for certificate
        if (entry.hasAttribute(ATTR_USER_CERTIFICATE)) {
            parseCertificateEntry(entry, ATTR_USER_CERTIFICATE, result);
        } else if (entry.hasAttribute(ATTR_CA_CERTIFICATE)) {
            parseCertificateEntry(entry, ATTR_CA_CERTIFICATE, result);
        }

        // Check for CRL
        if (entry.hasAttribute(ATTR_CRL)) {
            parseCrlEntry(entry, result);
        }

        // Check for Master List
        if (entry.hasAttribute(ATTR_MASTER_LIST)) {
            // Master List parsing will be handled separately
            spdlog::debug("Found Master List entry: {}", entry.dn);
        }
    }

    /**
     * @brief Parse certificate from entry
     */
    static void parseCertificateEntry(
        const LdifEntry& entry,
        const std::string& attrName,
        ParsedFile& result
    ) {
        std::string base64Value = entry.getFirstAttribute(attrName);
        if (base64Value.empty()) {
            return;
        }

        // Decode base64
        std::vector<uint8_t> derBytes = base64Decode(base64Value);
        if (derBytes.empty()) {
            throw shared::exception::InfrastructureException(
                "DECODE_ERROR",
                "Failed to decode base64 certificate data"
            );
        }

        // Parse certificate
        auto certData = OpenSslCertificateParser::parseCertificate(
            derBytes,
            entry.dn,
            entry.attributes
        );

        result.addCertificate(std::move(certData));
    }

    /**
     * @brief Parse CRL from entry
     */
    static void parseCrlEntry(const LdifEntry& entry, ParsedFile& result) {
        std::string base64Value = entry.getFirstAttribute(ATTR_CRL);
        if (base64Value.empty()) {
            return;
        }

        // Decode base64
        std::vector<uint8_t> derBytes = base64Decode(base64Value);
        if (derBytes.empty()) {
            throw shared::exception::InfrastructureException(
                "DECODE_ERROR",
                "Failed to decode base64 CRL data"
            );
        }

        // Parse CRL
        auto crlData = OpenSslCertificateParser::parseCrl(derBytes);
        result.addCrl(std::move(crlData));
    }
};

} // namespace fileparsing::infrastructure::adapter

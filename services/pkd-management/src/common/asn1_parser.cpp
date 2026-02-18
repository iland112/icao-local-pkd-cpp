/**
 * @file asn1_parser.cpp
 * @brief ASN.1 Structure Parser Implementation
 */

#include "asn1_parser.h"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <array>
#include <fstream>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace icao {
namespace asn1 {

std::string executeAsn1Parse(const std::string& filePath, int maxLines) {
    spdlog::debug("[ASN1Parser] Parsing ASN.1 via OpenSSL C API for: {} (maxLines: {})", filePath, maxLines);

    // Read DER file into memory (no shell command execution)
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }

    std::vector<unsigned char> derData(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    if (derData.empty()) {
        throw std::runtime_error("Empty DER file: " + filePath);
    }

    // Use OpenSSL ASN1_parse_dump to generate same output as "openssl asn1parse -i"
    BIO* out = BIO_new(BIO_s_mem());
    if (!out) {
        throw std::runtime_error("Failed to create BIO for ASN.1 parsing");
    }

    int rc = ASN1_parse_dump(out, derData.data(), static_cast<long>(derData.size()), 1, 0);

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(out, &bptr);

    std::string result;
    if (rc == 1 && bptr && bptr->length > 0) {
        result.assign(bptr->data, bptr->length);
    }
    BIO_free(out);

    if (result.empty()) {
        throw std::runtime_error("ASN.1 parsing produced empty output for: " + filePath);
    }

    // Apply line limit
    if (maxLines > 0) {
        std::istringstream stream(result);
        std::string line;
        std::string truncated;
        int lineCount = 0;
        while (std::getline(stream, line) && lineCount < maxLines) {
            truncated += line + "\n";
            lineCount++;
        }
        spdlog::debug("[ASN1Parser] ASN.1 parse output: {} lines (limit: {}, truncated: {})",
                      lineCount, maxLines, lineCount >= maxLines);
        return truncated;
    }

    spdlog::debug("[ASN1Parser] ASN.1 parse output: {} bytes", result.size());
    return result;
}

Json::Value parseAsn1Output(const std::string& asn1ParseOutput) {
    Json::Value root(Json::arrayValue);
    std::istringstream stream(asn1ParseOutput);
    std::string line;

    // Manual parsing of OpenSSL asn1parse output format (no std::regex - avoids SEGFAULT):
    // Example: "    0:d=0  hl=4 l=8192 cons: SEQUENCE"
    // Example: "    4:d=1  hl=2 l=   3 prim: OBJECT            :sha256WithRSAEncryption"

    int lineNum = 0;
    while (std::getline(stream, line)) {
        lineNum++;

        // Find offset: skip leading spaces, read digits before ':'
        size_t pos = 0;
        while (pos < line.size() && line[pos] == ' ') pos++;
        size_t digitStart = pos;
        while (pos < line.size() && std::isdigit(line[pos])) pos++;
        if (pos == digitStart || pos >= line.size() || line[pos] != ':') continue;
        int offset = 0;
        try { offset = std::stoi(line.substr(digitStart, pos - digitStart)); } catch (...) { continue; }
        pos++; // skip ':'

        // Find d=N
        auto findField = [&](const std::string& prefix) -> int {
            size_t fpos = line.find(prefix, pos);
            if (fpos == std::string::npos) return -1;
            fpos += prefix.size();
            size_t numStart = fpos;
            while (fpos < line.size() && (std::isdigit(line[fpos]) || line[fpos] == ' ')) fpos++;
            std::string numStr = line.substr(numStart, fpos - numStart);
            // Remove leading/trailing spaces from number
            size_t ns = numStr.find_first_not_of(' ');
            if (ns == std::string::npos) return -1;
            numStr = numStr.substr(ns);
            size_t ne = numStr.find_first_not_of("0123456789");
            if (ne != std::string::npos) numStr = numStr.substr(0, ne);
            try { return std::stoi(numStr); } catch (...) { return -1; }
        };

        int depth = findField("d=");
        int headerLength = findField("hl=");
        int length = findField("l=");
        if (depth < 0 || headerLength < 0 || length < 0) continue;

        // Find cons/prim
        std::string constructType;
        size_t consPos = line.find("cons:", pos);
        size_t primPos = line.find("prim:", pos);
        size_t tagStart;
        if (consPos != std::string::npos && (primPos == std::string::npos || consPos < primPos)) {
            constructType = "cons";
            tagStart = consPos + 5;
        } else if (primPos != std::string::npos) {
            constructType = "prim";
            tagStart = primPos + 5;
        } else {
            continue;
        }

        // Extract tag and optional value (separated by ':')
        while (tagStart < line.size() && line[tagStart] == ' ') tagStart++;
        std::string tag, value;
        size_t colonPos = line.find(':', tagStart);
        if (colonPos != std::string::npos) {
            tag = line.substr(tagStart, colonPos - tagStart);
            // Trim trailing spaces from tag
            while (!tag.empty() && tag.back() == ' ') tag.pop_back();
            value = line.substr(colonPos + 1);
            // Trim leading spaces from value
            size_t vs = value.find_first_not_of(' ');
            if (vs != std::string::npos) value = value.substr(vs);
            else value.clear();
        } else {
            tag = line.substr(tagStart);
            while (!tag.empty() && (tag.back() == ' ' || tag.back() == '\n' || tag.back() == '\r')) tag.pop_back();
        }

        // Create node
        Json::Value node;
        node["offset"] = offset;
        node["depth"] = depth;
        node["headerLength"] = headerLength;
        node["length"] = length;
        node["tag"] = tag;
        node["isConstructed"] = (constructType == "cons");
        if (!value.empty()) {
            node["value"] = value;
        }
        node["children"] = Json::Value(Json::arrayValue);

        // Add to tree: navigate from root each time to avoid storing pointers
        // (Json::Value::append() may invalidate previously stored pointers)
        if (depth == 0) {
            root.append(node);
        } else if (depth > 0 && depth < 100) {
            // Walk from root following the last child at each depth level
            Json::Value* container = &root;
            bool attached = false;
            for (int d = 0; d < depth; d++) {
                if (container->empty()) break;
                Json::Value& lastNode = (*container)[container->size() - 1];
                if (d == depth - 1) {
                    lastNode["children"].append(node);
                    attached = true;
                    break;
                }
                container = &(lastNode["children"]);
            }
            if (!attached) {
                spdlog::warn("[ASN1Parser] Could not attach node at depth {} (line {})", depth, lineNum);
            }
        } else {
            spdlog::warn("[ASN1Parser] Invalid depth {} at line {}", depth, lineNum);
        }
    }

    spdlog::debug("[ASN1Parser] Parsed {} root-level ASN.1 nodes", root.size());
    return root;
}

Json::Value parseAsn1Structure(const std::string& filePath, int maxLines) {
    Json::Value result;
    result["success"] = false;

    try {
        // Execute OpenSSL asn1parse with line limit
        std::string asn1Output = executeAsn1Parse(filePath, maxLines);

        if (asn1Output.empty()) {
            result["error"] = "Empty ASN.1 parse output";
            return result;
        }

        // Parse output into JSON tree
        Json::Value tree = parseAsn1Output(asn1Output);

        result["success"] = true;
        result["tree"] = tree;
        result["maxLines"] = maxLines;
        result["truncated"] = (maxLines > 0);  // Indicate if output was limited

        // Calculate statistics
        int totalNodes = 0;
        int constructedNodes = 0;
        int primitiveNodes = 0;

        std::function<void(const Json::Value&)> countNodes = [&](const Json::Value& node) {
            totalNodes++;
            if (node.isMember("isConstructed") && node["isConstructed"].asBool()) {
                constructedNodes++;
            } else {
                primitiveNodes++;
            }
            if (node.isMember("children") && node["children"].isArray()) {
                for (const auto& child : node["children"]) {
                    countNodes(child);
                }
            }
        };

        for (const auto& node : tree) {
            countNodes(node);
        }

        result["statistics"]["totalNodes"] = totalNodes;
        result["statistics"]["constructedNodes"] = constructedNodes;
        result["statistics"]["primitiveNodes"] = primitiveNodes;

    } catch (const std::exception& e) {
        spdlog::error("[ASN1Parser] Parse failed: {}", e.what());
        result["error"] = std::string("ASN.1 parsing failed: ") + e.what();
    }

    return result;
}

} // namespace asn1
} // namespace icao

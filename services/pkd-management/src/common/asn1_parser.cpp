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
#include <regex>
#include <array>

namespace icao {
namespace asn1 {

std::string executeAsn1Parse(const std::string& filePath, int maxLines) {
    spdlog::debug("[ASN1Parser] Executing OpenSSL asn1parse for: {} (maxLines: {})", filePath, maxLines);

    // Construct OpenSSL asn1parse command
    // -i: indent output for readability
    // -inform DER: input format is DER (binary)
    std::string cmd = "openssl asn1parse -inform DER -i -in \"" + filePath + "\" 2>&1";

    std::array<char, 128> buffer;
    std::string result;

    // Execute command and capture output
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("Failed to execute OpenSSL asn1parse command");
    }

    int lineCount = 0;
    bool limitReached = false;

    // Read output with line limit enforced in C++
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        if (maxLines > 0 && lineCount >= maxLines) {
            limitReached = true;
            break;  // Stop reading after maxLines
        }
        result += buffer.data();
        lineCount++;
    }

    // Close pipe (ignore return code if we stopped early)
    int returnCode = pclose(pipe.release());

    if (returnCode != 0 && !limitReached) {
        // Check if this is an actual error
        if (result.find("Can't open") != std::string::npos || result.find("error") != std::string::npos) {
            spdlog::error("[ASN1Parser] OpenSSL asn1parse failed with return code: {}", returnCode);
            throw std::runtime_error("OpenSSL asn1parse failed: " + result);
        }
    }

    spdlog::debug("[ASN1Parser] ASN.1 parse output: {} lines, {} bytes (limit: {}, truncated: {})",
                  lineCount, result.size(), maxLines, limitReached);
    return result;
}

Json::Value parseAsn1Output(const std::string& asn1ParseOutput) {
    Json::Value root(Json::arrayValue);
    std::istringstream stream(asn1ParseOutput);
    std::string line;

    // Regex to parse OpenSSL asn1parse output format:
    // Example: "    0:d=0  hl=4 l=8192 cons: SEQUENCE"
    // Groups: offset, depth, headerLength, length, constructType, tag, value
    std::regex lineRegex(R"(^\s*(\d+):d=(\d+)\s+hl=(\d+)\s+l=\s*(\d+)\s+(cons|prim):\s+(.+?)(?:\s*:\s*(.*))?$)");

    std::vector<Json::Value*> stack;  // Stack for tracking parent nodes at each depth
    stack.resize(100, nullptr);       // Pre-allocate for depths 0-99

    int lineNum = 0;
    while (std::getline(stream, line)) {
        lineNum++;
        std::smatch match;

        if (!std::regex_match(line, match, lineRegex)) {
            // Skip lines that don't match expected format
            continue;
        }

        int offset = std::stoi(match[1].str());
        int depth = std::stoi(match[2].str());
        int headerLength = std::stoi(match[3].str());
        int length = std::stoi(match[4].str());
        std::string constructType = match[5].str();
        std::string tag = match[6].str();
        std::string value = match.size() > 7 ? match[7].str() : "";

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

        // Add to tree structure
        if (depth == 0) {
            // Root level node
            root.append(node);
            stack[0] = &root[root.size() - 1];
        } else {
            // Child node - find parent at depth-1
            if (depth > 0 && depth < 100 && stack[depth - 1] != nullptr) {
                Json::Value* parent = stack[depth - 1];
                (*parent)["children"].append(node);
                stack[depth] = &((*parent)["children"][(*parent)["children"].size() - 1]);
            } else {
                spdlog::warn("[ASN1Parser] Invalid depth {} at line {}", depth, lineNum);
            }
        }
    }

    spdlog::debug("[ASN1Parser] Parsed {} ASN.1 nodes", root.size());
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

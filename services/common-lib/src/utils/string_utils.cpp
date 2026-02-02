/**
 * @file string_utils.cpp
 * @brief Common string utility functions implementation
 *
 * Implements string manipulation functions with safety and correctness.
 */

#include "icao/utils/string_utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace icao {
namespace utils {

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::string trim(const std::string& str) {
    // Find first non-whitespace character
    size_t start = 0;
    while (start < str.length() && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }
    
    // If all whitespace, return empty string
    if (start == str.length()) {
        return "";
    }
    
    // Find last non-whitespace character
    size_t end = str.length();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        --end;
    }
    
    return str.substr(start, end - start);
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;

    if (str.empty()) {
        tokens.push_back("");  // Empty string → [""]
        return tokens;
    }

    std::string token;
    std::istringstream tokenStream(str);

    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }

    // Handle trailing delimiter: "a,b," should produce ["a", "b", ""]
    if (!str.empty() && str.back() == delimiter) {
        tokens.push_back("");
    }

    return tokens;
}

std::string bytesToHex(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return "";
    }
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    
    return oss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    // Validate input
    if (hex.length() % 2 != 0) {
        throw std::invalid_argument("Hex string must have even length");
    }
    
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        char c1 = hex[i];
        char c2 = hex[i + 1];
        
        // Validate hex characters
        if (!std::isxdigit(static_cast<unsigned char>(c1)) || 
            !std::isxdigit(static_cast<unsigned char>(c2))) {
            throw std::invalid_argument("Invalid hex character in string");
        }
        
        // Convert to byte
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
        bytes.push_back(byte);
    }
    
    return bytes;
}

} // namespace utils
} // namespace icao

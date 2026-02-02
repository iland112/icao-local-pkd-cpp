/**
 * @file string_utils.h
 * @brief String manipulation utilities
 *
 * Common string operations used across ICAO PKD services.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace icao {
namespace utils {

/**
 * @brief Convert string to lowercase
 *
 * @param str Input string
 * @return Lowercase string
 */
std::string toLowerCase(const std::string& str);

/**
 * @brief Convert string to uppercase
 *
 * @param str Input string
 * @return Uppercase string
 */
std::string toUpperCase(const std::string& str);

/**
 * @brief Trim whitespace from both ends
 *
 * @param str Input string
 * @return Trimmed string
 */
std::string trim(const std::string& str);

/**
 * @brief Trim whitespace from left end
 *
 * @param str Input string
 * @return Left-trimmed string
 */
std::string trimLeft(const std::string& str);

/**
 * @brief Trim whitespace from right end
 *
 * @param str Input string
 * @return Right-trimmed string
 */
std::string trimRight(const std::string& str);

/**
 * @brief Split string by delimiter
 *
 * @param str Input string
 * @param delimiter Delimiter character
 * @return Vector of string parts
 */
std::vector<std::string> split(const std::string& str, char delimiter);

/**
 * @brief Join strings with delimiter
 *
 * @param parts Vector of strings
 * @param delimiter Delimiter string
 * @return Joined string
 */
std::string join(const std::vector<std::string>& parts, const std::string& delimiter);

/**
 * @brief Check if string starts with prefix
 *
 * @param str Input string
 * @param prefix Prefix to check
 * @return true if str starts with prefix
 */
bool startsWith(const std::string& str, const std::string& prefix);

/**
 * @brief Check if string ends with suffix
 *
 * @param str Input string
 * @param suffix Suffix to check
 * @return true if str ends with suffix
 */
bool endsWith(const std::string& str, const std::string& suffix);

/**
 * @brief Replace all occurrences of substring
 *
 * @param str Input string
 * @param from Substring to replace
 * @param to Replacement string
 * @return String with replacements
 */
std::string replaceAll(const std::string& str, const std::string& from, const std::string& to);

/**
 * @brief Convert binary data to hex string
 *
 * @param data Binary data
 * @param lowercase true for lowercase hex, false for uppercase
 * @return Hex string (2 chars per byte)
 */
std::string toHex(const std::vector<uint8_t>& data, bool lowercase = true);

/**
 * @brief Convert hex string to binary data
 *
 * @param hex Hex string (must be even length)
 * @return Binary data, or std::nullopt on error
 */
std::optional<std::vector<uint8_t>> fromHex(const std::string& hex);

/**
 * @brief Encode string to Base64
 *
 * @param data Binary data
 * @return Base64-encoded string
 */
std::string toBase64(const std::vector<uint8_t>& data);

/**
 * @brief Decode Base64 string
 *
 * @param base64 Base64-encoded string
 * @return Binary data, or std::nullopt on error
 */
std::optional<std::vector<uint8_t>> fromBase64(const std::string& base64);

/**
 * @brief Check if string contains only ASCII characters
 *
 * @param str Input string
 * @return true if all characters are ASCII (0-127)
 */
bool isAscii(const std::string& str);

/**
 * @brief Check if string is valid UTF-8
 *
 * @param str Input string
 * @return true if valid UTF-8 encoding
 */
bool isValidUtf8(const std::string& str);

/**
 * @brief Escape special characters for JSON
 *
 * @param str Input string
 * @return JSON-escaped string
 */
std::string escapeJson(const std::string& str);

/**
 * @brief Format string with printf-style arguments
 *
 * @param format Format string
 * @param args Variadic arguments
 * @return Formatted string
 */
template<typename... Args>
std::string format(const std::string& format, Args... args) {
    int size = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
    if (size <= 0) {
        return "";
    }
    std::vector<char> buffer(size);
    std::snprintf(buffer.data(), size, format.c_str(), args...);
    return std::string(buffer.data(), buffer.data() + size - 1);
}

} // namespace utils
} // namespace icao

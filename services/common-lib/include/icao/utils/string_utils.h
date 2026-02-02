/**
 * @file string_utils.h
 * @brief Common string utility functions
 *
 * Provides essential string manipulation functions used across services.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace icao {
namespace utils {

/**
 * @brief Convert string to lowercase
 *
 * @param str Input string
 * @return Lowercase version of the string
 *
 * @example
 * std::string upper = "HELLO";
 * std::string lower = toLower(upper);  // "hello"
 */
std::string toLower(const std::string& str);

/**
 * @brief Convert string to uppercase
 *
 * @param str Input string
 * @return Uppercase version of the string
 *
 * @example
 * std::string lower = "hello";
 * std::string upper = toUpper(lower);  // "HELLO"
 */
std::string toUpper(const std::string& str);

/**
 * @brief Trim whitespace from both ends of string
 *
 * Removes leading and trailing whitespace characters (space, tab, newline, etc.).
 *
 * @param str Input string
 * @return Trimmed string
 *
 * @example
 * std::string padded = "  hello  ";
 * std::string trimmed = trim(padded);  // "hello"
 */
std::string trim(const std::string& str);

/**
 * @brief Split string by delimiter
 *
 * @param str Input string
 * @param delimiter Character to split on
 * @return Vector of substrings
 *
 * @example
 * std::string csv = "a,b,c";
 * auto parts = split(csv, ',');  // ["a", "b", "c"]
 */
std::vector<std::string> split(const std::string& str, char delimiter);

/**
 * @brief Convert bytes to hexadecimal string (lowercase)
 *
 * @param data Pointer to byte array
 * @param len Length of byte array
 * @return Hex-encoded string (lowercase, no separators)
 *
 * @example
 * uint8_t bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
 * std::string hex = bytesToHex(bytes, 4);  // "deadbeef"
 */
std::string bytesToHex(const uint8_t* data, size_t len);

/**
 * @brief Convert hexadecimal string to bytes
 *
 * Accepts both uppercase and lowercase hex characters.
 * Input string must have even length.
 *
 * @param hex Hex-encoded string (even length)
 * @return Vector of bytes
 * @throws std::invalid_argument if hex string is invalid
 *
 * @example
 * std::string hex = "deadbeef";
 * auto bytes = hexToBytes(hex);  // {0xDE, 0xAD, 0xBE, 0xEF}
 */
std::vector<uint8_t> hexToBytes(const std::string& hex);

} // namespace utils
} // namespace icao

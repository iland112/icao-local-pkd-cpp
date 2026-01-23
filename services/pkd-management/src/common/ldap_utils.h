#pragma once

#include <string>
#include <sstream>
#include <iomanip>

namespace ldap_utils {

/**
 * @brief Escape LDAP DN component value according to RFC 4514
 *
 * Escapes special characters in DN attribute values:
 * - , (comma)
 * - + (plus)
 * - " (double quote)
 * - \ (backslash)
 * - < (less than)
 * - > (greater than)
 * - ; (semicolon)
 * - = (equals)
 * - Leading space or # (hash)
 * - Trailing space
 *
 * @param value The DN component value to escape
 * @return Escaped string safe for use in LDAP DN
 *
 * @example
 *   escapeDnComponent("John, Doe") → "John\\, Doe"
 *   escapeDnComponent(" Leading") → "\\ Leading"
 *   escapeDnComponent("Trailing ") → "Trailing\\ "
 */
inline std::string escapeDnComponent(const std::string& value) {
    if (value.empty()) return value;

    std::string escaped;
    escaped.reserve(value.size() * 2); // Reserve extra space for escapes

    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];

        // Escape special characters (RFC 4514)
        if (c == ',' || c == '=' || c == '+' || c == '"' || c == '\\' ||
            c == '<' || c == '>' || c == ';') {
            escaped += '\\';
            escaped += c;
        }
        // Escape leading space or hash
        else if (i == 0 && (c == ' ' || c == '#')) {
            escaped += '\\';
            escaped += c;
        }
        // Escape trailing space
        else if (i == value.size() - 1 && c == ' ') {
            escaped += '\\';
            escaped += c;
        }
        // Escape null byte as \00
        else if (c == '\0') {
            escaped += "\\00";
        }
        else {
            escaped += c;
        }
    }

    return escaped;
}

/**
 * @brief Escape LDAP filter value according to RFC 4515
 *
 * Escapes special characters in LDAP search filter values:
 * - * (asterisk) → \\2a
 * - ( (left paren) → \\28
 * - ) (right paren) → \\29
 * - \ (backslash) → \\5c
 * - NUL (null byte) → \\00
 * - Non-printable characters → \\HH (hex)
 *
 * This prevents LDAP filter injection attacks where malicious input
 * could alter the filter logic (e.g., "admin*)(uid=*" attempting to
 * match all users instead of searching for "admin*)(uid=*" literally).
 *
 * @param value The filter value to escape
 * @return Escaped string safe for use in LDAP search filters
 *
 * @example
 *   escapeFilterValue("John*") → "John\\2a"
 *   escapeFilterValue("admin*)(uid=*") → "admin\\2a\\29\\28uid=\\2a"
 *   escapeFilterValue("test\\backslash") → "test\\5cbackslash"
 *
 * @see RFC 4515 Section 3: Search Filter encoding
 */
inline std::string escapeFilterValue(const std::string& value) {
    if (value.empty()) return value;

    std::string escaped;
    escaped.reserve(value.size() * 3); // Reserve extra space for hex escapes

    for (unsigned char c : value) {
        switch (c) {
            case '*':
                escaped += "\\2a";
                break;
            case '(':
                escaped += "\\28";
                break;
            case ')':
                escaped += "\\29";
                break;
            case '\\':
                escaped += "\\5c";
                break;
            case '\0':
                escaped += "\\00";
                break;
            default:
                // Escape non-printable characters (< 0x20 or > 0x7E) as \HH
                if (c < 0x20 || c > 0x7E) {
                    std::ostringstream oss;
                    oss << "\\" << std::hex << std::setw(2) << std::setfill('0') << (int)c;
                    escaped += oss.str();
                } else {
                    escaped += c;
                }
        }
    }

    return escaped;
}

/**
 * @brief Build safe LDAP filter with escaped value
 *
 * Convenience function for building common LDAP filters with automatic escaping.
 *
 * @param attribute LDAP attribute name (not escaped, assumed safe)
 * @param value User-provided value to search for (will be escaped)
 * @param op Comparison operator: "=" (equals), "~=" (approx), ">=" (gte), "<=" (lte)
 * @return Safe LDAP filter string
 *
 * @example
 *   buildFilter("cn", "John*", "=") → "(cn=John\\2a)"
 *   buildFilter("uid", "admin*)(uid=*", "=") → "(uid=admin\\2a\\29\\28uid=\\2a)"
 */
inline std::string buildFilter(const std::string& attribute,
                                const std::string& value,
                                const std::string& op = "=") {
    return "(" + attribute + op + escapeFilterValue(value) + ")";
}

/**
 * @brief Build LDAP substring filter with wildcards (safe)
 *
 * Creates a substring filter with wildcards, escaping the value but preserving
 * leading/trailing asterisks for wildcard matching.
 *
 * @param attribute LDAP attribute name
 * @param value User-provided substring (will be escaped)
 * @param prefix If true, adds leading wildcard (*value)
 * @param suffix If true, adds trailing wildcard (value*)
 * @return Safe LDAP substring filter
 *
 * @example
 *   buildSubstringFilter("cn", "John", false, true) → "(cn=John*)"
 *   buildSubstringFilter("cn", "J*hn", true, true) → "(cn=*J\\2ahn*)"
 */
inline std::string buildSubstringFilter(const std::string& attribute,
                                         const std::string& value,
                                         bool prefix = true,
                                         bool suffix = true) {
    std::string filter = "(" + attribute + "=";
    if (prefix) filter += "*";
    filter += escapeFilterValue(value);
    if (suffix) filter += "*";
    filter += ")";
    return filter;
}

} // namespace ldap_utils

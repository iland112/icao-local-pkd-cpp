/**
 * @file time_utils.h
 * @brief Time and date utilities
 *
 * Provides conversion between OpenSSL ASN1_TIME and std::chrono,
 * plus formatting and parsing functions.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <openssl/asn1.h>

namespace icao {
namespace utils {

/**
 * @brief Convert ASN1_TIME to system_clock time_point
 *
 * Used for certificate validity periods (notBefore, notAfter).
 *
 * @param asn1Time OpenSSL ASN1_TIME structure
 * @return std::chrono time_point, or std::nullopt on error
 */
std::optional<std::chrono::system_clock::time_point> asn1TimeToTimePoint(
    const ASN1_TIME* asn1Time
);

/**
 * @brief Convert time_point to ASN1_TIME
 *
 * @param tp std::chrono time_point
 * @return ASN1_TIME structure (caller must free with ASN1_TIME_free),
 *         or nullptr on error
 *
 * @warning Caller is responsible for freeing returned ASN1_TIME
 */
ASN1_TIME* timePointToAsn1Time(
    const std::chrono::system_clock::time_point& tp
);

/**
 * @brief Format time_point as ISO 8601 string
 *
 * @param tp std::chrono time_point
 * @param includeMilliseconds Include milliseconds in output
 * @return ISO 8601 string (e.g., "2026-02-02T12:34:56Z")
 */
std::string formatIso8601(
    const std::chrono::system_clock::time_point& tp,
    bool includeMilliseconds = false
);

/**
 * @brief Parse ISO 8601 string to time_point
 *
 * @param iso8601 ISO 8601 formatted string
 * @return std::chrono time_point, or std::nullopt on error
 */
std::optional<std::chrono::system_clock::time_point> parseIso8601(
    const std::string& iso8601
);

/**
 * @brief Format time_point as RFC 3339 string
 *
 * Similar to ISO 8601 but with timezone offset support.
 *
 * @param tp std::chrono time_point
 * @return RFC 3339 string (e.g., "2026-02-02T12:34:56+00:00")
 */
std::string formatRfc3339(
    const std::chrono::system_clock::time_point& tp
);

/**
 * @brief Format time_point as human-readable string
 *
 * @param tp std::chrono time_point
 * @param format strftime format string (default: "%Y-%m-%d %H:%M:%S")
 * @return Formatted string
 */
std::string formatHumanReadable(
    const std::chrono::system_clock::time_point& tp,
    const std::string& format = "%Y-%m-%d %H:%M:%S"
);

/**
 * @brief Get current time as time_point
 *
 * @return Current system time
 */
inline std::chrono::system_clock::time_point now() {
    return std::chrono::system_clock::now();
}

/**
 * @brief Calculate duration between two time points
 *
 * @param start Start time
 * @param end End time
 * @return Duration in seconds
 */
inline std::chrono::seconds duration(
    const std::chrono::system_clock::time_point& start,
    const std::chrono::system_clock::time_point& end
) {
    return std::chrono::duration_cast<std::chrono::seconds>(end - start);
}

/**
 * @brief Calculate days between two time points
 *
 * @param start Start time
 * @param end End time
 * @return Number of days (can be negative)
 */
int daysBetween(
    const std::chrono::system_clock::time_point& start,
    const std::chrono::system_clock::time_point& end
);

/**
 * @brief Add days to time_point
 *
 * @param tp Time point
 * @param days Number of days to add (can be negative)
 * @return New time_point
 */
std::chrono::system_clock::time_point addDays(
    const std::chrono::system_clock::time_point& tp,
    int days
);

/**
 * @brief Add months to time_point
 *
 * @param tp Time point
 * @param months Number of months to add (can be negative)
 * @return New time_point
 */
std::chrono::system_clock::time_point addMonths(
    const std::chrono::system_clock::time_point& tp,
    int months
);

/**
 * @brief Add years to time_point
 *
 * @param tp Time point
 * @param years Number of years to add (can be negative)
 * @return New time_point
 */
std::chrono::system_clock::time_point addYears(
    const std::chrono::system_clock::time_point& tp,
    int years
);

/**
 * @brief Convert Unix timestamp (seconds since epoch) to time_point
 *
 * @param timestamp Unix timestamp
 * @return std::chrono time_point
 */
std::chrono::system_clock::time_point fromUnixTimestamp(int64_t timestamp);

/**
 * @brief Convert time_point to Unix timestamp
 *
 * @param tp std::chrono time_point
 * @return Unix timestamp (seconds since epoch)
 */
int64_t toUnixTimestamp(const std::chrono::system_clock::time_point& tp);

/**
 * @brief Check if year is leap year
 *
 * @param year Year number
 * @return true if leap year
 */
bool isLeapYear(int year);

/**
 * @brief Get number of days in month
 *
 * @param year Year number
 * @param month Month number (1-12)
 * @return Number of days in month
 */
int daysInMonth(int year, int month);

} // namespace utils
} // namespace icao

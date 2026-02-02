/**
 * @file time_utils.h
 * @brief Time and ASN.1 conversion utilities
 *
 * Provides conversion functions for ASN.1 time structures and integers.
 *
 * @version 1.0.0
 * @date 2026-02-02
 */

#pragma once

#include <string>
#include <chrono>
#include <openssl/asn1.h>

namespace icao {
namespace utils {

/**
 * @brief Convert ASN1_TIME to ISO8601 string
 *
 * Converts OpenSSL ASN1_TIME structure to ISO8601 formatted string.
 * Format: YYYY-MM-DDTHH:MM:SSZ
 *
 * @param time ASN1_TIME structure (must not be null)
 * @return ISO8601 formatted string, or empty string on error
 *
 * @example
 * const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
 * std::string iso = asn1TimeToIso8601(notAfter);  // "2032-06-14T15:45:09Z"
 */
std::string asn1TimeToIso8601(const ASN1_TIME* time);

/**
 * @brief Convert ASN1_TIME to chrono::time_point
 *
 * Converts OpenSSL ASN1_TIME structure to C++ chrono time_point.
 *
 * @param time ASN1_TIME structure (must not be null)
 * @return std::chrono::system_clock::time_point
 * @throws std::runtime_error if conversion fails
 *
 * @example
 * const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
 * auto timePoint = asn1TimeToTimePoint(notBefore);
 * 
 * // Check if certificate is expired
 * auto now = std::chrono::system_clock::now();
 * bool expired = timePoint < now;
 */
std::chrono::system_clock::time_point asn1TimeToTimePoint(const ASN1_TIME* time);

/**
 * @brief Convert ASN1_INTEGER to hexadecimal string
 *
 * Converts OpenSSL ASN1_INTEGER (used for serial numbers) to hex string.
 * Result is lowercase without separators.
 *
 * @param integer ASN1_INTEGER structure (must not be null)
 * @return Hex-encoded string (lowercase), or empty string on error
 *
 * @example
 * const ASN1_INTEGER* serial = X509_get0_serialNumber(cert);
 * std::string serialHex = asn1IntegerToHex(serial);  // "59b6e258"
 */
std::string asn1IntegerToHex(const ASN1_INTEGER* integer);

} // namespace utils
} // namespace icao

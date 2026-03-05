/**
 * @file time_utils.cpp
 * @brief Time and ASN.1 conversion utilities implementation
 *
 * Implements conversion functions for ASN.1 time structures and integers.
 */

#include "icao/utils/time_utils.h"
#include <openssl/bn.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace icao {
namespace utils {

std::string asn1TimeToIso8601(const ASN1_TIME* time) {
    if (!time) {
        return "";
    }

    // Convert to time_point first
    std::chrono::system_clock::time_point tp = asn1TimeToTimePoint(time);
    
    // Convert to time_t
    std::time_t time_t_value = std::chrono::system_clock::to_time_t(tp);
    
    // Convert to tm struct (UTC)
    struct tm tm_time;
    if (!gmtime_r(&time_t_value, &tm_time)) {
        return "";
    }
    
    // Format as ISO8601: YYYY-MM-DDTHH:MM:SSZ
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (tm_time.tm_year + 1900) << '-'
        << std::setw(2) << (tm_time.tm_mon + 1) << '-'
        << std::setw(2) << tm_time.tm_mday << 'T'
        << std::setw(2) << tm_time.tm_hour << ':'
        << std::setw(2) << tm_time.tm_min << ':'
        << std::setw(2) << tm_time.tm_sec << 'Z';
    
    return oss.str();
}

std::chrono::system_clock::time_point asn1TimeToTimePoint(const ASN1_TIME* asn1_time) {
    if (!asn1_time) {
        return std::chrono::system_clock::time_point{};
    }

    struct tm tm_time;
    std::memset(&tm_time, 0, sizeof(tm_time));

    // Validate ASN1_TIME data length before parsing
    if (!asn1_time->data || asn1_time->length < 12) {
        throw std::runtime_error("ASN1_TIME data too short or null");
    }

    // Copy to null-terminated buffer for safe sscanf
    char timeBuf[32];
    int copyLen = std::min(static_cast<int>(sizeof(timeBuf) - 1), asn1_time->length);
    std::memcpy(timeBuf, asn1_time->data, copyLen);
    timeBuf[copyLen] = '\0';
    const char* str = timeBuf;

    if (asn1_time->type == V_ASN1_UTCTIME) {
        // YYMMDDhhmmssZ (minimum 12 chars)
        if (asn1_time->length < 12) {
            throw std::runtime_error("UTCTIME too short");
        }
        int scanned = sscanf(str, "%2d%2d%2d%2d%2d%2d",
                             &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
                             &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec);
        
        if (scanned != 6) {
            throw std::runtime_error("Failed to parse UTCTIME");
        }

        tm_time.tm_year += (tm_time.tm_year < 50) ? 100 : 0; // Y2K adjustment
    } else if (asn1_time->type == V_ASN1_GENERALIZEDTIME) {
        // YYYYMMDDhhmmssZ (minimum 14 chars)
        if (asn1_time->length < 14) {
            throw std::runtime_error("GENERALIZEDTIME too short");
        }
        int scanned = sscanf(str, "%4d%2d%2d%2d%2d%2d",
                             &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
                             &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec);
        
        if (scanned != 6) {
            throw std::runtime_error("Failed to parse GENERALIZEDTIME");
        }

        tm_time.tm_year -= 1900;
    } else {
        throw std::runtime_error("Unknown ASN1_TIME type");
    }

    tm_time.tm_mon -= 1; // Month is 0-11
    tm_time.tm_isdst = 0; // UTC

    std::time_t time = timegm(&tm_time);
    if (time == -1) {
        throw std::runtime_error("Failed to convert to time_t");
    }
    
    return std::chrono::system_clock::from_time_t(time);
}

std::string asn1IntegerToHex(const ASN1_INTEGER* asn1_int) {
    if (!asn1_int) {
        return "";
    }

    BIGNUM* bn = ASN1_INTEGER_to_BN(asn1_int, nullptr);
    if (!bn) {
        return "";
    }

    char* hex_str = BN_bn2hex(bn);
    if (!hex_str) {
        BN_free(bn);
        return "";
    }

    std::string result(hex_str);

    // Convert to lowercase
    for (char& c : result) {
        c = std::tolower(static_cast<unsigned char>(c));
    }

    OPENSSL_free(hex_str);
    BN_free(bn);

    return result;
}

} // namespace utils
} // namespace icao

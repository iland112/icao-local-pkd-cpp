#include "validation_result.h"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace icao::relay::domain {

bool ValidationResult::isExpired() const {
    if (not_after_.empty()) {
        return false;  // Cannot determine expiration without date
    }

    // Parse ISO 8601 timestamp from not_after_
    std::tm tm = {};
    std::istringstream ss(not_after_);

    // Try parsing ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");

    if (ss.fail()) {
        // Try alternative format: YYYY-MM-DD HH:MM:SS
        ss.clear();
        ss.str(not_after_);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

        if (ss.fail()) {
            return false;  // Invalid date format
        }
    }

    // Convert to time_t
    std::time_t certTime = std::mktime(&tm);
    std::time_t now = std::time(nullptr);

    // Certificate is expired if current time is past not_after
    return now > certTime;
}

} // namespace icao::relay::domain

#pragma once

#include <string>
#include <random>
#include <sstream>
#include <iomanip>

namespace shared::util {

/**
 * UUID generation utility.
 */
class UuidUtil {
public:
    /**
     * Generate a UUID v4 (random).
     */
    static std::string generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;

        uint64_t ab = dis(gen);
        uint64_t cd = dis(gen);

        // Set version to 4 (random)
        ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        // Set variant to RFC 4122
        cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');

        // Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        ss << std::setw(8) << ((ab >> 32) & 0xFFFFFFFF) << "-";
        ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << "-";
        ss << std::setw(4) << (ab & 0xFFFF) << "-";
        ss << std::setw(4) << ((cd >> 48) & 0xFFFF) << "-";
        ss << std::setw(12) << (cd & 0xFFFFFFFFFFFFULL);

        return ss.str();
    }

    /**
     * Validate UUID format.
     */
    static bool isValid(const std::string& uuid) {
        if (uuid.length() != 36) {
            return false;
        }

        for (size_t i = 0; i < uuid.length(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (uuid[i] != '-') {
                    return false;
                }
            } else {
                char c = uuid[i];
                if (!((c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) {
                    return false;
                }
            }
        }

        return true;
    }
};

} // namespace shared::util

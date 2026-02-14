/** @file password_hash.cpp
 *  @brief PBKDF2-HMAC-SHA256 password hashing implementation
 */

#include "password_hash.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace auth {

namespace {

// Convert binary data to hex string
std::string toHex(const unsigned char* data, size_t length) {
    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

// Convert hex string to binary data
std::vector<unsigned char> fromHex(const std::string& hex) {
    std::vector<unsigned char> result;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(std::stoi(byteString, nullptr, 16));
        result.push_back(byte);
    }
    return result;
}

} // anonymous namespace

std::string hashPassword(const std::string& password, int iterations) {
    // Generate random salt (16 bytes = 128 bits)
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        throw std::runtime_error("Failed to generate random salt");
    }

    // Hash password with PBKDF2-HMAC-SHA256
    const int keyLength = 32; // 256 bits
    unsigned char hash[keyLength];

    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), password.length(),
            salt, sizeof(salt),
            iterations,
            EVP_sha256(),
            keyLength,
            hash) != 1) {
        throw std::runtime_error("PBKDF2 hashing failed");
    }

    // Format: $pbkdf2$<iterations>$<salt>$<hash>
    std::ostringstream result;
    result << "$pbkdf2$"
           << iterations << "$"
           << toHex(salt, sizeof(salt)) << "$"
           << toHex(hash, keyLength);

    return result.str();
}

bool verifyPassword(const std::string& password, const std::string& storedHash) {
    try {
        // Parse stored hash
        if (storedHash.substr(0, 8) != "$pbkdf2$") {
            spdlog::error("Invalid hash format: {}", storedHash.substr(0, 8));
            return false;
        }

        // Extract components
        int iterations = extractIterations(storedHash);
        std::string saltHex = extractSalt(storedHash);

        // Convert salt from hex
        std::vector<unsigned char> salt = fromHex(saltHex);

        // Hash input password with same salt and iterations
        const int keyLength = 32;
        unsigned char hash[keyLength];

        if (PKCS5_PBKDF2_HMAC(
                password.c_str(), password.length(),
                salt.data(), salt.size(),
                iterations,
                EVP_sha256(),
                keyLength,
                hash) != 1) {
            spdlog::error("PBKDF2 hashing failed during verification");
            return false;
        }

        // Extract stored hash
        size_t lastDollar = storedHash.rfind('$');
        std::string storedHashHex = storedHash.substr(lastDollar + 1);

        // Compare hashes
        std::string computedHashHex = toHex(hash, keyLength);
        return computedHashHex == storedHashHex;

    } catch (const std::exception& e) {
        spdlog::error("Password verification failed: {}", e.what());
        return false;
    }
}

std::string extractSalt(const std::string& storedHash) {
    // Format: $pbkdf2$<iterations>$<salt>$<hash>
    size_t firstDollar = storedHash.find('$', 1);   // After $pbkdf2
    size_t secondDollar = storedHash.find('$', firstDollar + 1); // After iterations
    size_t thirdDollar = storedHash.find('$', secondDollar + 1); // After salt

    if (firstDollar == std::string::npos ||
        secondDollar == std::string::npos ||
        thirdDollar == std::string::npos) {
        throw std::runtime_error("Invalid hash format: cannot extract salt");
    }

    return storedHash.substr(secondDollar + 1, thirdDollar - secondDollar - 1);
}

int extractIterations(const std::string& storedHash) {
    // Format: $pbkdf2$<iterations>$<salt>$<hash>
    size_t firstDollar = storedHash.find('$', 1);   // After $pbkdf2
    size_t secondDollar = storedHash.find('$', firstDollar + 1); // After iterations

    if (firstDollar == std::string::npos || secondDollar == std::string::npos) {
        throw std::runtime_error("Invalid hash format: cannot extract iterations");
    }

    std::string iterStr = storedHash.substr(firstDollar + 1, secondDollar - firstDollar - 1);
    return std::stoi(iterStr);
}

} // namespace auth

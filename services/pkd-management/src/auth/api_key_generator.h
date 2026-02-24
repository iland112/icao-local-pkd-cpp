#pragma once

/**
 * @file api_key_generator.h
 * @brief API Key generation and hashing utilities
 *
 * Generates cryptographically secure API keys and computes SHA-256 hashes.
 * Key format: icao_{prefix}_{random} (46 chars total)
 */

#include <string>

namespace auth {

struct ApiKeyInfo {
    std::string key;      // Full API key (shown once to user)
    std::string hash;     // SHA-256 hex hash (stored in DB)
    std::string prefix;   // First segment for identification (e.g., "icao_ab12cd34")
};

/**
 * @brief Generate a new API key with cryptographic randomness
 * @return ApiKeyInfo with key, hash, and prefix
 */
ApiKeyInfo generateApiKey();

/**
 * @brief Compute SHA-256 hex hash of an API key
 * @param apiKey The raw API key string
 * @return 64-character lowercase hex hash
 */
std::string hashApiKey(const std::string& apiKey);

} // namespace auth

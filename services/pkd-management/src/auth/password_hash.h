#pragma once

#include <string>

namespace auth {

/**
 * @brief Hash a password using PBKDF2-HMAC-SHA256
 *
 * This function generates a secure password hash using PBKDF2 with SHA-256.
 * The hash format is: $pbkdf2$<iterations>$<salt>$<hash>
 *
 * @param password Plain text password to hash
 * @param iterations Number of PBKDF2 iterations (default: 310000, OWASP 2023 recommendation)
 * @return Hashed password string in custom format
 */
std::string hashPassword(const std::string& password, int iterations = 310000);

/**
 * @brief Verify a password against a stored hash
 *
 * @param password Plain text password to verify
 * @param storedHash Stored hash from database (in $pbkdf2$ format)
 * @return true if password matches, false otherwise
 */
bool verifyPassword(const std::string& password, const std::string& storedHash);

/**
 * @brief Extract salt from a stored hash
 *
 * Internal function to parse the hash format and extract the salt.
 *
 * @param storedHash Hash string in $pbkdf2$ format
 * @return Salt as hex string
 */
std::string extractSalt(const std::string& storedHash);

/**
 * @brief Extract iterations from a stored hash
 *
 * Internal function to parse the hash format and extract iteration count.
 *
 * @param storedHash Hash string in $pbkdf2$ format
 * @return Iteration count
 */
int extractIterations(const std::string& storedHash);

} // namespace auth

/**
 * @file main_utils.cpp
 * @brief Utility function implementations for PKD Management service
 *
 * Contains crypto, file validation, and X.509 helper functions
 * previously defined in main.cpp.
 */

#include "main_utils.h"

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/bio.h>

#include <spdlog/spdlog.h>

#include <sstream>
#include <iomanip>
#include <random>
#include <cctype>
#include <regex>

#include "certificate_utils.h"

// --- File Upload Security ---

/**
 * @brief Sanitize filename to prevent path traversal attacks
 * @param filename Original filename from upload
 * @return Sanitized filename (alphanumeric, dash, underscore, dot only)
 */
std::string sanitizeFilename(const std::string& filename) {
    std::string sanitized;

    // Only allow alphanumeric, dash, underscore, and dot
    for (char c : filename) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            sanitized += c;
        } else {
            sanitized += '_';  // Replace invalid chars with underscore
        }
    }

    // Prevent path traversal
    if (sanitized.find("..") != std::string::npos) {
        throw std::runtime_error("Invalid filename: contains '..'");
    }

    // Limit length to 255 characters
    if (sanitized.length() > 255) {
        sanitized = sanitized.substr(0, 255);
    }

    // Ensure filename is not empty
    if (sanitized.empty()) {
        throw std::runtime_error("Invalid filename: empty after sanitization");
    }

    return sanitized;
}

/**
 * @brief Validate LDIF file format
 * @param content File content as string
 * @return true if valid LDIF format
 */
bool isValidLdifFile(const std::string& content) {
    // LDIF files must contain "dn:" or "version:" entries
    if (content.find("dn:") == std::string::npos &&
        content.find("version:") == std::string::npos) {
        return false;
    }

    // Basic size check (should have at least some content)
    if (content.size() < 10) {
        return false;
    }

    return true;
}

/**
 * @brief Validate PKCS#7 (Master List) file format
 * @param content File content as binary vector
 * @return true if valid PKCS#7 DER format
 */
bool isValidP7sFile(const std::vector<uint8_t>& content) {
    // Check for PKCS#7 ASN.1 DER magic bytes
    // DER SEQUENCE: 0x30 followed by length encoding
    if (content.size() < 4) {
        return false;
    }

    // First byte should be 0x30 (SEQUENCE tag)
    if (content[0] != 0x30) {
        return false;
    }

    // Second byte should be length encoding
    // DER length encoding:
    // - 0x00-0x7F: short form (length <= 127 bytes)
    // - 0x80: indefinite form (not used in DER, but accept for compatibility)
    // - 0x81-0x84: long form (1-4 bytes for length)
    if (content[1] >= 0x80 && content[1] <= 0x84) {
        // Long form or indefinite form - valid
        return true;
    }
    if (content[1] >= 0x01 && content[1] <= 0x7F) {
        // Short form - valid
        return true;
    }

    // Invalid length encoding
    return false;
}

/**
 * @brief Generate UUID v4
 */
std::string generateUuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);

    // Set version (4) and variant (RFC 4122)
    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (ab >> 32) << '-';
    ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << '-';
    ss << std::setw(4) << (ab & 0xFFFF) << '-';
    ss << std::setw(4) << (cd >> 48) << '-';
    ss << std::setw(12) << (cd & 0x0000FFFFFFFFFFFFULL);

    return ss.str();
}

/**
 * @brief Compute SHA256 hash of content
 */
std::string computeFileHash(const std::vector<uint8_t>& content) {
    unsigned char hash[32];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        spdlog::error("Failed to create EVP_MD_CTX");
        return "";
    }
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, content.data(), content.size());
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// --- Certificate/CRL Parsing and DB Storage Functions ---

/**
 * @brief Base64 decode
 */
std::vector<uint8_t> base64Decode(const std::string& encoded) {
    static const std::string base64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<uint8_t> result;
    std::vector<int> decodingTable(256, -1);
    for (size_t i = 0; i < base64Chars.size(); i++) {
        decodingTable[static_cast<unsigned char>(base64Chars[i])] = static_cast<int>(i);
    }

    int val = 0;
    int valb = -8;
    for (unsigned char c : encoded) {
        if (decodingTable[c] == -1) continue;
        val = (val << 6) + decodingTable[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

/**
 * @brief Convert X509_NAME to string
 */
std::string x509NameToString(X509_NAME* name) {
    if (!name) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

/**
 * @brief Convert ASN1_INTEGER to hex string
 */
std::string asn1IntegerToHex(const ASN1_INTEGER* asn1Int) {
    if (!asn1Int) return "";
    BIGNUM* bn = ASN1_INTEGER_to_BN(asn1Int, nullptr);
    if (!bn) return "";
    char* hex = BN_bn2hex(bn);
    if (!hex) { BN_free(bn); return ""; }
    std::string result(hex);
    OPENSSL_free(hex);
    BN_free(bn);
    return result;
}

// Delegate to certificate_utils namespace (declared in main_utils.h at global scope)
std::string asn1TimeToIso8601(const ASN1_TIME* asn1Time) {
    return certificate_utils::asn1TimeToIso8601(asn1Time);
}

std::string extractCountryCode(const std::string& dn) {
    return certificate_utils::extractCountryCode(dn);
}

std::string extractCountryCodeFromDn(const std::string& dn) {
    std::string code = extractCountryCode(dn);
    return code.empty() ? "XX" : code;
}

/**
 * @brief Update Master List DB record with LDAP DN after successful LDAP storage
 */
void updateMasterListLdapStatus(const std::string& mlId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    spdlog::warn("[UpdateMasterListLdapStatus] Stub implementation - needs MasterListRepository");
    spdlog::debug("[UpdateMasterListLdapStatus] Would update LDAP status: ml_id={}, ldap_dn={}",
                 mlId.substr(0, 8) + "...", ldapDn);
}

// --- Database Storage Functions ---

/**
 * @brief Save Master List to database
 * @return Master List ID or empty string on failure
 */
std::string saveMasterList(const std::string& uploadId,
                            const std::string& countryCode, const std::string& signerDn,
                            const std::string& fingerprint, int cscaCount,
                            const std::vector<uint8_t>& mlBinary) {
    std::string mlId = generateUuid();

    spdlog::warn("[SaveMasterList] Stub implementation - needs MasterListRepository");
    spdlog::info("[SaveMasterList] Would save Master List: upload={}, country={}, signer={}, csca_count={}, binary_size={}",
                uploadId.substr(0, 8) + "...", countryCode, signerDn.substr(0, 30) + "...", cscaCount, mlBinary.size());

    // Return generated UUID for now (actual save not implemented)
    return mlId;
}

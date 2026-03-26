/**
 * @file db_crl_provider.cpp
 * @brief ICrlProvider adapter implementation for database-backed CRL lookup
 */

#include "db_crl_provider.h"
#include <vector>
#include <cstdint>
#include <openssl/x509.h>

namespace adapters {

DbCrlProvider::DbCrlProvider(repositories::CrlRepository* crlRepo)
    : crlRepo_(crlRepo)
{
    if (!crlRepo_) {
        throw std::invalid_argument("DbCrlProvider: crlRepo cannot be nullptr");
    }
}

X509_CRL* DbCrlProvider::findCrlByCountry(const std::string& countryCode) {
    Json::Value crlData = crlRepo_->findByCountryCode(countryCode);
    if (crlData.isNull()) {
        return nullptr;
    }

    std::string crlBinaryHex = crlData.get("crl_binary", "").asString();
    if (crlBinaryHex.empty()) {
        return nullptr;
    }

    // Decode hex to DER bytes (handle \x prefix and double-encoding)
    std::vector<uint8_t> derBytes;
    size_t hexStart = 0;
    if (crlBinaryHex.size() > 2 && crlBinaryHex[0] == '\\' && crlBinaryHex[1] == 'x') {
        hexStart = 2;
    }
    derBytes.reserve((crlBinaryHex.size() - hexStart) / 2);
    for (size_t i = hexStart; i + 1 < crlBinaryHex.size(); i += 2) {
        char h[3] = {crlBinaryHex[i], crlBinaryHex[i + 1], 0};
        derBytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
    }

    // Handle double-encoded BYTEA (decoded bytes start with \x = 0x5C 0x78)
    if (derBytes.size() > 2 && derBytes[0] == 0x5C && derBytes[1] == 0x78) {
        std::vector<uint8_t> innerBytes;
        innerBytes.reserve((derBytes.size() - 2) / 2);
        for (size_t i = 2; i + 1 < derBytes.size(); i += 2) {
            char h[3] = {static_cast<char>(derBytes[i]), static_cast<char>(derBytes[i + 1]), 0};
            innerBytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
        }
        derBytes = std::move(innerBytes);
    }

    if (derBytes.empty()) {
        return nullptr;
    }

    // Parse DER bytes to X509_CRL
    const unsigned char* p = derBytes.data();
    return d2i_X509_CRL(nullptr, &p, static_cast<long>(derBytes.size()));
}

} // namespace adapters

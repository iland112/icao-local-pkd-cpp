/**
 * @file data_group.cpp
 * @brief Implementation of DataGroup domain model
 */

#include "data_group.h"
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

namespace domain {
namespace models {

Json::Value DataGroup::toJson(bool includeRawData) const {
    Json::Value json;

    // Identification
    json["dgNumber"] = dgNumber;
    json["dgTag"] = dgTag;
    json["description"] = getDescription();

    // Hash verification
    json["expectedHash"] = expectedHash;
    json["actualHash"] = actualHash;
    json["hashValid"] = hashValid;
    json["hashAlgorithm"] = hashAlgorithm;

    // Data size
    json["dataSize"] = static_cast<int>(dataSize);

    // Parsing status
    json["parsingSuccess"] = parsingSuccess;
    if (parsingErrors) {
        json["parsingErrors"] = *parsingErrors;
    }

    // Content type
    if (contentType) {
        json["contentType"] = *contentType;
    }

    // Raw data (optional - only if requested)
    if (includeRawData && rawData && !rawData->empty()) {
        // Base64 encode or hex encode for JSON
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t byte : *rawData) {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        json["rawDataHex"] = oss.str();
    }

    return json;
}

DataGroup DataGroup::fromRawData(
    const std::string& dgNumber,
    const std::vector<uint8_t>& data,
    const std::string& expectedHash,
    const std::string& hashAlgorithm)
{
    DataGroup dg;
    dg.dgNumber = dgNumber;
    dg.rawData = data;
    dg.dataSize = data.size();
    dg.expectedHash = expectedHash;
    dg.hashAlgorithm = hashAlgorithm;

    // Compute actual hash
    dg.actualHash = dg.computeHash(hashAlgorithm);

    // Verify hash
    dg.hashValid = dg.verifyHash();

    dg.parsingSuccess = true;

    return dg;
}

std::string DataGroup::computeHash(const std::string& hashAlgorithm) const {
    if (!rawData || rawData->empty()) {
        spdlog::warn("Cannot compute hash: rawData is empty for {}", dgNumber);
        return "";
    }

    const EVP_MD* md = nullptr;

    if (hashAlgorithm == "SHA-1" || hashAlgorithm == "SHA1") {
        md = EVP_sha1();
    } else if (hashAlgorithm == "SHA-256" || hashAlgorithm == "SHA256") {
        md = EVP_sha256();
    } else if (hashAlgorithm == "SHA-384" || hashAlgorithm == "SHA384") {
        md = EVP_sha384();
    } else if (hashAlgorithm == "SHA-512" || hashAlgorithm == "SHA512") {
        md = EVP_sha512();
    } else {
        spdlog::error("Unsupported hash algorithm: {}", hashAlgorithm);
        return "";
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        spdlog::error("Failed to create EVP_MD_CTX");
        return "";
    }

    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        spdlog::error("Failed to initialize digest");
        EVP_MD_CTX_free(ctx);
        return "";
    }

    if (EVP_DigestUpdate(ctx, rawData->data(), rawData->size()) != 1) {
        spdlog::error("Failed to update digest");
        EVP_MD_CTX_free(ctx);
        return "";
    }

    if (EVP_DigestFinal_ex(ctx, hash, &hashLen) != 1) {
        spdlog::error("Failed to finalize digest");
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);

    // Convert to hex string
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < hashLen; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }

    return oss.str();
}

std::string DataGroup::getDescription() const {
    if (dgNumber == "DG1") return "Machine Readable Zone (MRZ)";
    if (dgNumber == "DG2") return "Encoded Face";
    if (dgNumber == "DG3") return "Encoded Fingerprint(s)";
    if (dgNumber == "DG4") return "Encoded Iris(es)";
    if (dgNumber == "DG5") return "Displayed Portrait";
    if (dgNumber == "DG6") return "Reserved for Future Use";
    if (dgNumber == "DG7") return "Displayed Signature or Usual Mark";
    if (dgNumber == "DG8") return "Data Feature(s)";
    if (dgNumber == "DG9") return "Structure Feature(s)";
    if (dgNumber == "DG10") return "Substance Feature(s)";
    if (dgNumber == "DG11") return "Additional Personal Detail(s)";
    if (dgNumber == "DG12") return "Additional Document Detail(s)";
    if (dgNumber == "DG13") return "Optional Detail(s)";
    if (dgNumber == "DG14") return "Reserved for Future Use";
    if (dgNumber == "DG15") return "Active Authentication Public Key Info";

    return "Unknown Data Group";
}

} // namespace models
} // namespace domain

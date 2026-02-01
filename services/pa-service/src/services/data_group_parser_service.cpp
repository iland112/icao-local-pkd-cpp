/**
 * @file data_group_parser_service.cpp
 * @brief Implementation of DataGroupParserService
 */

#include "data_group_parser_service.h"
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

namespace services {

DataGroupParserService::DataGroupParserService() {
    spdlog::debug("DataGroupParserService initialized");
}

Json::Value DataGroupParserService::parseDg1(const std::vector<uint8_t>& dg1Data) {
    // Placeholder - extract MRZ from DG1 ASN.1 structure
    Json::Value result;
    result["success"] = false;
    result["message"] = "DG1 parsing not yet implemented";
    return result;
}

Json::Value DataGroupParserService::parseMrzText(const std::string& mrzText) {
    // Placeholder - parse MRZ text lines
    Json::Value result;
    result["success"] = false;
    result["message"] = "MRZ text parsing not yet implemented";
    return result;
}

Json::Value DataGroupParserService::parseDg2(const std::vector<uint8_t>& dg2Data) {
    // Placeholder - extract face image from DG2
    Json::Value result;
    result["success"] = false;
    result["message"] = "DG2 parsing not yet implemented";
    return result;
}

bool DataGroupParserService::verifyDataGroupHash(
    const std::vector<uint8_t>& dgData,
    const std::string& expectedHash,
    const std::string& hashAlgorithm)
{
    std::string actualHash = computeHash(dgData, hashAlgorithm);
    return actualHash == expectedHash;
}

std::string DataGroupParserService::computeHash(
    const std::vector<uint8_t>& data,
    const std::string& algorithm)
{
    const EVP_MD* md = nullptr;

    if (algorithm == "SHA-1" || algorithm == "SHA1") {
        md = EVP_sha1();
    } else if (algorithm == "SHA-256" || algorithm == "SHA256") {
        md = EVP_sha256();
    } else if (algorithm == "SHA-384" || algorithm == "SHA384") {
        md = EVP_sha384();
    } else if (algorithm == "SHA-512" || algorithm == "SHA512") {
        md = EVP_sha512();
    } else {
        spdlog::error("Unsupported hash algorithm: {}", algorithm);
        return "";
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash, &hashLen);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < hashLen; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }

    return oss.str();
}

} // namespace services

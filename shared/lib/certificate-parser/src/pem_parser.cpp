/**
 * @file pem_parser.cpp
 * @brief Implementation of PEM format parser
 */

#include "pem_parser.h"
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <cstring>
#include <sstream>

namespace icao {
namespace certificate_parser {

PemParseResult PemParser::parse(const std::vector<uint8_t>& data) {
    PemParseResult result;

    if (data.empty()) {
        result.errorMessage = "Empty data";
        return result;
    }

    // Convert to string for processing
    std::string content(data.begin(), data.end());
    return parse(content);
}

PemParseResult PemParser::parse(const std::string& pemString) {
    PemParseResult result;

    if (pemString.empty()) {
        result.errorMessage = "Empty string";
        return result;
    }

    // Extract all PEM blocks
    std::vector<std::string> blocks = extractPemBlocks(pemString);

    if (blocks.empty()) {
        result.errorMessage = "No PEM blocks found";
        return result;
    }

    // Parse each certificate block
    for (const auto& block : blocks) {
        if (!isCertificateBlock(block)) {
            continue;  // Skip non-certificate blocks
        }

        X509* cert = parsePemBlock(block);
        if (cert) {
            result.certificates.push_back(cert);
            result.certificateCount++;
        } else {
            result.parseErrors++;
        }
    }

    if (result.certificateCount == 0) {
        result.errorMessage = "No valid certificates found";
        return result;
    }

    result.success = true;
    return result;
}

X509* PemParser::parseSingle(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return nullptr;
    }

    // Create BIO from data
    BIO* bio = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    if (!bio) {
        return nullptr;
    }

    // Read PEM certificate
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    BIO_free(bio);
    return cert;
}

bool PemParser::isPemFormat(const std::vector<uint8_t>& data) {
    if (data.size() < 27) {  // Minimum size for "-----BEGIN CERTIFICATE-----"
        return false;
    }

    std::string content(data.begin(), std::min(data.begin() + 1000, data.end()));
    return content.find("-----BEGIN CERTIFICATE-----") != std::string::npos ||
           content.find("-----BEGIN PKCS7-----") != std::string::npos;
}

std::vector<std::string> PemParser::extractPemBlocks(const std::string& content) {
    std::vector<std::string> blocks;
    std::istringstream iss(content);
    std::string line;
    std::string currentBlock;
    bool inBlock = false;

    while (std::getline(iss, line)) {
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }

        if (line.find("-----BEGIN") != std::string::npos) {
            inBlock = true;
            currentBlock = line + "\n";
        } else if (line.find("-----END") != std::string::npos) {
            if (inBlock) {
                currentBlock += line + "\n";
                blocks.push_back(currentBlock);
                currentBlock.clear();
                inBlock = false;
            }
        } else if (inBlock) {
            currentBlock += line + "\n";
        }
    }

    return blocks;
}

std::string PemParser::toPem(X509* cert) {
    if (!cert) {
        return "";
    }

    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return "";
    }

    if (PEM_write_bio_X509(bio, cert) != 1) {
        BIO_free(bio);
        return "";
    }

    // Get PEM data from BIO
    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);

    std::string pemString;
    if (mem && mem->data && mem->length > 0) {
        pemString.assign(mem->data, mem->length);
    }

    BIO_free(bio);
    return pemString;
}

X509* PemParser::parsePemBlock(const std::string& pemBlock) {
    if (pemBlock.empty()) {
        return nullptr;
    }

    // Create BIO from block
    BIO* bio = BIO_new_mem_buf(pemBlock.data(), static_cast<int>(pemBlock.size()));
    if (!bio) {
        return nullptr;
    }

    // Read certificate from PEM
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    BIO_free(bio);
    return cert;
}

bool PemParser::isCertificateBlock(const std::string& block) {
    return block.find("-----BEGIN CERTIFICATE-----") != std::string::npos ||
           block.find("-----BEGIN X509 CERTIFICATE-----") != std::string::npos ||
           block.find("-----BEGIN PKCS7-----") != std::string::npos;
}

} // namespace certificate_parser
} // namespace icao

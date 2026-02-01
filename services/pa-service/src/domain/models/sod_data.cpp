/**
 * @file sod_data.cpp
 * @brief Implementation of SodData domain model
 */

#include "sod_data.h"
#include <spdlog/spdlog.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

namespace domain {
namespace models {

// Copy constructor
SodData::SodData(const SodData& other)
    : signatureAlgorithm(other.signatureAlgorithm),
      signatureAlgorithmOid(other.signatureAlgorithmOid),
      hashAlgorithm(other.hashAlgorithm),
      hashAlgorithmOid(other.hashAlgorithmOid),
      dscCertificate(nullptr),
      dataGroupHashes(other.dataGroupHashes),
      signedAttributes(other.signedAttributes),
      ldsSecurityObjectVersion(other.ldsSecurityObjectVersion),
      ldsSecurityObjectOid(other.ldsSecurityObjectOid),
      rawSodData(other.rawSodData),
      parsingErrors(other.parsingErrors),
      parsingSuccess(other.parsingSuccess)
{
    // Deep copy of X509 certificate
    if (other.dscCertificate) {
        dscCertificate = X509_dup(other.dscCertificate);
        if (!dscCertificate) {
            spdlog::error("Failed to duplicate X509 certificate in SodData copy constructor");
        }
    }
}

// Copy assignment
SodData& SodData::operator=(const SodData& other) {
    if (this != &other) {
        // Free existing certificate
        if (dscCertificate) {
            X509_free(dscCertificate);
            dscCertificate = nullptr;
        }

        // Copy all fields
        signatureAlgorithm = other.signatureAlgorithm;
        signatureAlgorithmOid = other.signatureAlgorithmOid;
        hashAlgorithm = other.hashAlgorithm;
        hashAlgorithmOid = other.hashAlgorithmOid;
        dataGroupHashes = other.dataGroupHashes;
        signedAttributes = other.signedAttributes;
        ldsSecurityObjectVersion = other.ldsSecurityObjectVersion;
        ldsSecurityObjectOid = other.ldsSecurityObjectOid;
        rawSodData = other.rawSodData;
        parsingErrors = other.parsingErrors;
        parsingSuccess = other.parsingSuccess;

        // Deep copy certificate
        if (other.dscCertificate) {
            dscCertificate = X509_dup(other.dscCertificate);
            if (!dscCertificate) {
                spdlog::error("Failed to duplicate X509 certificate in SodData copy assignment");
            }
        }
    }
    return *this;
}

// Move constructor
SodData::SodData(SodData&& other) noexcept
    : signatureAlgorithm(std::move(other.signatureAlgorithm)),
      signatureAlgorithmOid(std::move(other.signatureAlgorithmOid)),
      hashAlgorithm(std::move(other.hashAlgorithm)),
      hashAlgorithmOid(std::move(other.hashAlgorithmOid)),
      dscCertificate(other.dscCertificate),
      dataGroupHashes(std::move(other.dataGroupHashes)),
      signedAttributes(std::move(other.signedAttributes)),
      ldsSecurityObjectVersion(std::move(other.ldsSecurityObjectVersion)),
      ldsSecurityObjectOid(std::move(other.ldsSecurityObjectOid)),
      rawSodData(std::move(other.rawSodData)),
      parsingErrors(std::move(other.parsingErrors)),
      parsingSuccess(other.parsingSuccess)
{
    other.dscCertificate = nullptr;  // Transfer ownership
}

// Move assignment
SodData& SodData::operator=(SodData&& other) noexcept {
    if (this != &other) {
        // Free existing certificate
        if (dscCertificate) {
            X509_free(dscCertificate);
        }

        // Move all fields
        signatureAlgorithm = std::move(other.signatureAlgorithm);
        signatureAlgorithmOid = std::move(other.signatureAlgorithmOid);
        hashAlgorithm = std::move(other.hashAlgorithm);
        hashAlgorithmOid = std::move(other.hashAlgorithmOid);
        dscCertificate = other.dscCertificate;
        dataGroupHashes = std::move(other.dataGroupHashes);
        signedAttributes = std::move(other.signedAttributes);
        ldsSecurityObjectVersion = std::move(other.ldsSecurityObjectVersion);
        ldsSecurityObjectOid = std::move(other.ldsSecurityObjectOid);
        rawSodData = std::move(other.rawSodData);
        parsingErrors = std::move(other.parsingErrors);
        parsingSuccess = other.parsingSuccess;

        other.dscCertificate = nullptr;  // Transfer ownership
    }
    return *this;
}

Json::Value SodData::toJson() const {
    Json::Value json;

    // Algorithms
    json["signatureAlgorithm"] = signatureAlgorithm;
    json["signatureAlgorithmOid"] = signatureAlgorithmOid;
    json["hashAlgorithm"] = hashAlgorithm;
    json["hashAlgorithmOid"] = hashAlgorithmOid;

    // DSC certificate (PEM format)
    if (dscCertificate) {
        BIO* bio = BIO_new(BIO_s_mem());
        if (bio) {
            if (PEM_write_bio_X509(bio, dscCertificate)) {
                char* pemData = nullptr;
                long pemLen = BIO_get_mem_data(bio, &pemData);
                if (pemData && pemLen > 0) {
                    json["dscCertificatePem"] = std::string(pemData, pemLen);
                }
            }
            BIO_free(bio);
        }
    }

    // Data group hashes
    Json::Value dgHashesJson;
    for (const auto& [dgNum, hash] : dataGroupHashes) {
        dgHashesJson[dgNum] = hash;
    }
    json["dataGroupHashes"] = dgHashesJson;

    // Signed attributes
    if (!signedAttributes.empty()) {
        Json::Value attrsJson;
        for (const auto& [key, value] : signedAttributes) {
            attrsJson[key] = value;
        }
        json["signedAttributes"] = attrsJson;
    }

    // LDS Security Object
    json["ldsSecurityObjectVersion"] = ldsSecurityObjectVersion;
    if (ldsSecurityObjectOid) {
        json["ldsSecurityObjectOid"] = *ldsSecurityObjectOid;
    }

    // Parsing status
    json["parsingSuccess"] = parsingSuccess;
    if (parsingErrors) {
        json["parsingErrors"] = *parsingErrors;
    }

    // Data group count
    json["dataGroupCount"] = static_cast<int>(dataGroupHashes.size());

    return json;
}

} // namespace models
} // namespace domain

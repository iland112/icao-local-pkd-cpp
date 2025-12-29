#pragma once

#include "passiveauthentication/domain/model/DataGroupNumber.hpp"
#include "passiveauthentication/domain/model/DataGroupHash.hpp"
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <openssl/x509.h>

namespace pa::domain::port {

/**
 * DSC (Document Signer Certificate) information extracted from SOD.
 */
struct DscInfo {
    std::string subjectDn;
    std::string serialNumber;  // Hex-encoded
};

/**
 * Port interface for SOD (Security Object Document) parsing.
 *
 * Provides SOD parsing and verification operations.
 * Infrastructure layer implements this interface using OpenSSL.
 */
class SodParserPort {
public:
    virtual ~SodParserPort() = default;

    /**
     * Parse data group hashes from SOD.
     *
     * @param sodBytes SOD binary data (optionally wrapped with Tag 0x77)
     * @return map of DataGroupNumber to DataGroupHash
     */
    virtual std::map<model::DataGroupNumber, model::DataGroupHash> parseDataGroupHashes(
        const std::vector<uint8_t>& sodBytes
    ) = 0;

    /**
     * Verify SOD signature using DSC public key.
     *
     * @param sodBytes SOD binary data
     * @param dscPublicKey DSC public key (OpenSSL EVP_PKEY*)
     * @return true if signature is valid
     */
    virtual bool verifySignature(
        const std::vector<uint8_t>& sodBytes,
        EVP_PKEY* dscPublicKey
    ) = 0;

    /**
     * Verify SOD signature using DSC certificate.
     *
     * @param sodBytes SOD binary data
     * @param dscCert DSC certificate (OpenSSL X509*)
     * @return true if signature is valid
     */
    virtual bool verifySignature(
        const std::vector<uint8_t>& sodBytes,
        X509* dscCert
    ) = 0;

    /**
     * Extract hash algorithm from SOD (e.g., "SHA-256", "SHA-384").
     *
     * @param sodBytes SOD binary data
     * @return hash algorithm name
     */
    virtual std::string extractHashAlgorithm(
        const std::vector<uint8_t>& sodBytes
    ) = 0;

    /**
     * Extract signature algorithm from SOD (e.g., "SHA256withRSA").
     *
     * @param sodBytes SOD binary data
     * @return signature algorithm name
     */
    virtual std::string extractSignatureAlgorithm(
        const std::vector<uint8_t>& sodBytes
    ) = 0;

    /**
     * Extract DSC information from SOD.
     *
     * @param sodBytes SOD binary data
     * @return DscInfo with subject DN and serial number
     */
    virtual DscInfo extractDscInfo(
        const std::vector<uint8_t>& sodBytes
    ) = 0;

    /**
     * Extract full DSC certificate from SOD.
     *
     * @param sodBytes SOD binary data
     * @return X509* certificate (caller takes ownership)
     */
    virtual X509* extractDscCertificate(
        const std::vector<uint8_t>& sodBytes
    ) = 0;

    /**
     * Unwrap ICAO 9303 Tag 0x77 wrapper from SOD if present.
     *
     * @param sodBytes SOD binary data (potentially wrapped)
     * @return pure CMS SignedData bytes
     */
    virtual std::vector<uint8_t> unwrapIcaoSod(
        const std::vector<uint8_t>& sodBytes
    ) = 0;
};

} // namespace pa::domain::port

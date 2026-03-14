/**
 * @file cvc_signature.cpp
 * @brief CVC signature verification implementation using OpenSSL EVP API
 */

#include "icao/cvc/cvc_signature.h"
#include "icao/cvc/eac_oids.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include <cstring>
#include <memory>

namespace icao::cvc {

// RAII helpers for OpenSSL objects
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;

static const EVP_MD* getDigest(const std::string& algOid) {
    if (algOid == oid::TA_RSA_V1_5_SHA_1 || algOid == oid::TA_RSA_PSS_SHA_1
        || algOid == oid::TA_ECDSA_SHA_1) {
        return EVP_sha1();
    }
    if (algOid == oid::TA_ECDSA_SHA_224) {
        return EVP_sha224();
    }
    if (algOid == oid::TA_RSA_V1_5_SHA_256 || algOid == oid::TA_RSA_PSS_SHA_256
        || algOid == oid::TA_ECDSA_SHA_256) {
        return EVP_sha256();
    }
    if (algOid == oid::TA_ECDSA_SHA_384) {
        return EVP_sha384();
    }
    if (algOid == oid::TA_ECDSA_SHA_512) {
        return EVP_sha512();
    }
    return nullptr;
}

SignatureVerifyResult CvcSignatureVerifier::verify(const CvcCertificate& cert,
                                                    const CvcPublicKey& issuerKey) {
    if (cert.bodyRaw.empty() || cert.signature.empty()) {
        return {false, "Missing body or signature data"};
    }

    if (issuerKey.algorithmOid.empty()) {
        return {false, "Issuer public key has no algorithm OID"};
    }

    if (isRsaAlgorithm(issuerKey.algorithmOid)) {
        return verifyRsa(cert.bodyRaw, cert.signature, issuerKey);
    }
    if (isEcdsaAlgorithm(issuerKey.algorithmOid)) {
        return verifyEcdsa(cert.bodyRaw, cert.signature, issuerKey);
    }

    return {false, "Unsupported algorithm: " + issuerKey.algorithmOid};
}

SignatureVerifyResult CvcSignatureVerifier::verifySelfSigned(const CvcCertificate& cert) {
    return verify(cert, cert.publicKey);
}

SignatureVerifyResult CvcSignatureVerifier::verifyRsa(const std::vector<uint8_t>& data,
                                                      const std::vector<uint8_t>& signature,
                                                      const CvcPublicKey& key) {
    if (key.modulus.empty() || key.exponent.empty()) {
        return {false, "RSA key missing modulus or exponent"};
    }

    const EVP_MD* md = getDigest(key.algorithmOid);
    if (!md) {
        return {false, "Unsupported digest for OID: " + key.algorithmOid};
    }

    // Create BIGNUM for modulus and exponent
    BnPtr n(BN_bin2bn(key.modulus.data(), static_cast<int>(key.modulus.size()), nullptr), BN_free);
    BnPtr e(BN_bin2bn(key.exponent.data(), static_cast<int>(key.exponent.size()), nullptr), BN_free);
    if (!n || !e) {
        return {false, "Failed to create BN from key data"};
    }

    // Build EVP_PKEY with RSA
    EvpPkeyPtr pkey(EVP_PKEY_new(), EVP_PKEY_free);
    if (!pkey) {
        return {false, "Failed to create EVP_PKEY"};
    }

    RSA* rsa = RSA_new();
    if (!rsa) {
        return {false, "Failed to create RSA structure"};
    }

    // RSA_set0_key takes ownership of BN pointers
    if (RSA_set0_key(rsa, n.release(), e.release(), nullptr) != 1) {
        RSA_free(rsa);
        return {false, "Failed to set RSA key components"};
    }

    // EVP_PKEY_assign_RSA takes ownership of rsa
    if (EVP_PKEY_assign_RSA(pkey.get(), rsa) != 1) {
        return {false, "Failed to assign RSA to EVP_PKEY"};
    }

    // Verify signature
    EvpMdCtxPtr ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
        return {false, "Failed to create EVP_MD_CTX"};
    }

    if (EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, pkey.get()) != 1) {
        return {false, "EVP_DigestVerifyInit failed"};
    }

    int rc = EVP_DigestVerify(ctx.get(), signature.data(), signature.size(),
                              data.data(), data.size());

    return {rc == 1, rc == 1 ? "RSA signature valid" : "RSA signature verification failed"};
}

SignatureVerifyResult CvcSignatureVerifier::verifyEcdsa(const std::vector<uint8_t>& data,
                                                        const std::vector<uint8_t>& signature,
                                                        const CvcPublicKey& key) {
    if (key.prime.empty() || key.order.empty() || key.generator.empty()) {
        return {false, "ECDSA key missing domain parameters"};
    }

    if (key.publicPoint.empty()) {
        return {false, "ECDSA key missing public point"};
    }

    const EVP_MD* md = getDigest(key.algorithmOid);
    if (!md) {
        return {false, "Unsupported digest for OID: " + key.algorithmOid};
    }

    // Create EC group from explicit domain parameters
    BnPtr p(BN_bin2bn(key.prime.data(), static_cast<int>(key.prime.size()), nullptr), BN_free);
    BnPtr a(BN_bin2bn(key.coeffA.data(), static_cast<int>(key.coeffA.size()), nullptr), BN_free);
    BnPtr b(BN_bin2bn(key.coeffB.data(), static_cast<int>(key.coeffB.size()), nullptr), BN_free);
    BnPtr order(BN_bin2bn(key.order.data(), static_cast<int>(key.order.size()), nullptr), BN_free);

    if (!p || !a || !b || !order) {
        return {false, "Failed to create BN from EC parameters"};
    }

    BnPtr cofactor(nullptr, BN_free);
    if (!key.cofactor.empty()) {
        cofactor.reset(BN_bin2bn(key.cofactor.data(), static_cast<int>(key.cofactor.size()), nullptr));
    } else {
        cofactor.reset(BN_new());
        if (cofactor) BN_set_word(cofactor.get(), 1);
    }

    EC_GROUP* group = EC_GROUP_new_curve_GFp(p.get(), a.get(), b.get(), nullptr);
    if (!group) {
        return {false, "Failed to create EC_GROUP"};
    }

    // Set generator point
    EC_POINT* genPoint = EC_POINT_new(group);
    if (!genPoint) {
        EC_GROUP_free(group);
        return {false, "Failed to create generator EC_POINT"};
    }

    if (EC_POINT_oct2point(group, genPoint, key.generator.data(),
                           key.generator.size(), nullptr) != 1) {
        EC_POINT_free(genPoint);
        EC_GROUP_free(group);
        return {false, "Failed to decode generator point"};
    }

    if (EC_GROUP_set_generator(group, genPoint, order.get(), cofactor.get()) != 1) {
        EC_POINT_free(genPoint);
        EC_GROUP_free(group);
        return {false, "Failed to set generator on group"};
    }
    EC_POINT_free(genPoint);

    // Create EC_KEY and set public key
    EC_KEY* ecKey = EC_KEY_new();
    if (!ecKey) {
        EC_GROUP_free(group);
        return {false, "Failed to create EC_KEY"};
    }

    if (EC_KEY_set_group(ecKey, group) != 1) {
        EC_KEY_free(ecKey);
        EC_GROUP_free(group);
        return {false, "Failed to set EC_KEY group"};
    }

    EC_POINT* pubPoint = EC_POINT_new(group);
    if (!pubPoint) {
        EC_KEY_free(ecKey);
        EC_GROUP_free(group);
        return {false, "Failed to create public EC_POINT"};
    }

    if (EC_POINT_oct2point(group, pubPoint, key.publicPoint.data(),
                           key.publicPoint.size(), nullptr) != 1) {
        EC_POINT_free(pubPoint);
        EC_KEY_free(ecKey);
        EC_GROUP_free(group);
        return {false, "Failed to decode public point"};
    }

    if (EC_KEY_set_public_key(ecKey, pubPoint) != 1) {
        EC_POINT_free(pubPoint);
        EC_KEY_free(ecKey);
        EC_GROUP_free(group);
        return {false, "Failed to set EC public key"};
    }
    EC_POINT_free(pubPoint);

    // Build EVP_PKEY
    EvpPkeyPtr pkey(EVP_PKEY_new(), EVP_PKEY_free);
    if (!pkey) {
        EC_KEY_free(ecKey);
        EC_GROUP_free(group);
        return {false, "Failed to create EVP_PKEY"};
    }

    // EVP_PKEY_assign_EC_KEY takes ownership of ecKey
    if (EVP_PKEY_assign_EC_KEY(pkey.get(), ecKey) != 1) {
        EC_KEY_free(ecKey);
        EC_GROUP_free(group);
        return {false, "Failed to assign EC_KEY to EVP_PKEY"};
    }

    EC_GROUP_free(group);

    // CVC ECDSA signatures are plain (r||s), not DER-encoded
    // Convert plain signature to DER for OpenSSL verification
    size_t sigLen = signature.size();
    if (sigLen == 0 || sigLen % 2 != 0) {
        return {false, "Invalid ECDSA signature length"};
    }

    size_t halfLen = sigLen / 2;
    BnPtr r(BN_bin2bn(signature.data(), static_cast<int>(halfLen), nullptr), BN_free);
    BnPtr s(BN_bin2bn(signature.data() + halfLen, static_cast<int>(halfLen), nullptr), BN_free);
    if (!r || !s) {
        return {false, "Failed to parse ECDSA signature r/s"};
    }

    ECDSA_SIG* ecdsaSig = ECDSA_SIG_new();
    if (!ecdsaSig) {
        return {false, "Failed to create ECDSA_SIG"};
    }

    // ECDSA_SIG_set0 takes ownership
    if (ECDSA_SIG_set0(ecdsaSig, r.release(), s.release()) != 1) {
        ECDSA_SIG_free(ecdsaSig);
        return {false, "Failed to set ECDSA_SIG r/s"};
    }

    // Convert to DER
    unsigned char* derSig = nullptr;
    int derLen = i2d_ECDSA_SIG(ecdsaSig, &derSig);
    ECDSA_SIG_free(ecdsaSig);

    if (derLen <= 0 || !derSig) {
        return {false, "Failed to DER-encode ECDSA signature"};
    }

    // Verify with DER signature
    EvpMdCtxPtr ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
        OPENSSL_free(derSig);
        return {false, "Failed to create EVP_MD_CTX"};
    }

    if (EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, pkey.get()) != 1) {
        OPENSSL_free(derSig);
        return {false, "EVP_DigestVerifyInit failed"};
    }

    int rc = EVP_DigestVerify(ctx.get(), derSig, static_cast<size_t>(derLen),
                              data.data(), data.size());

    OPENSSL_free(derSig);

    return {rc == 1, rc == 1 ? "ECDSA signature valid" : "ECDSA signature verification failed"};
}

const void* CvcSignatureVerifier::getDigestForOid(const std::string& algOid) {
    return static_cast<const void*>(getDigest(algOid));
}

} // namespace icao::cvc

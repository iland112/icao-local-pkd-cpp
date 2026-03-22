#include "csr_service.h"
#include "../repositories/csr_repository.h"
#include "../auth/personal_info_crypto.h"
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <memory>

namespace services {

CsrService::CsrService(repositories::CsrRepository* csrRepo,
                        common::IQueryExecutor* queryExecutor)
    : csrRepo_(csrRepo), queryExecutor_(queryExecutor)
{
}

std::string CsrService::buildSubjectDn(const std::string& countryCode,
                                        const std::string& organization,
                                        const std::string& commonName)
{
    std::string dn;
    if (!countryCode.empty()) dn += "/C=" + countryCode;
    if (!organization.empty()) dn += "/O=" + organization;
    if (!commonName.empty()) dn += "/CN=" + commonName;
    return dn.empty() ? "/CN=ICAO PKD" : dn;
}

// Helper: compute SHA-256 fingerprint of public key
static std::string computePubKeyFingerprint(EVP_PKEY* pkey) {
    unsigned char fp[32];
    unsigned int fpLen = 0;

    std::unique_ptr<BIO, decltype(&BIO_free)> pubBio(BIO_new(BIO_s_mem()), BIO_free);
    i2d_PUBKEY_bio(pubBio.get(), pkey);
    char* pubData = nullptr;
    long pubLen = BIO_get_mem_data(pubBio.get(), &pubData);

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> mdCtx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (mdCtx) {
        EVP_DigestInit_ex(mdCtx.get(), EVP_sha256(), nullptr);
        EVP_DigestUpdate(mdCtx.get(), pubData, pubLen);
        EVP_DigestFinal_ex(mdCtx.get(), fp, &fpLen);
    }

    std::string hex;
    hex.reserve(64);
    static const char hexChars[] = "0123456789abcdef";
    for (unsigned int i = 0; i < fpLen; i++) {
        hex += hexChars[fp[i] >> 4];
        hex += hexChars[fp[i] & 0x0F];
    }
    return hex;
}

CsrGenerateResult CsrService::generate(const CsrGenerateRequest& req)
{
    CsrGenerateResult result;
    result.success = false;

    spdlog::info("[CsrService] Generating CSR: C={}, O={}, CN={}", req.countryCode, req.organization, req.commonName);

    // 1. Generate RSA-2048 key pair
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> keyCtx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    if (!keyCtx || EVP_PKEY_keygen_init(keyCtx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(keyCtx.get(), 2048) <= 0) {
        result.errorMessage = "Failed to initialize RSA-2048 key generation";
        return result;
    }

    EVP_PKEY* rawKey = nullptr;
    if (EVP_PKEY_keygen(keyCtx.get(), &rawKey) <= 0) {
        result.errorMessage = "RSA-2048 key generation failed";
        return result;
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(rawKey, EVP_PKEY_free);

    // 2. Create X509_REQ (CSR)
    std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)> x509Req(X509_REQ_new(), X509_REQ_free);
    if (!x509Req) { result.errorMessage = "Failed to create X509_REQ"; return result; }

    X509_REQ_set_version(x509Req.get(), 0);
    result.subjectDn = buildSubjectDn(req.countryCode, req.organization, req.commonName);
    X509_NAME* name = X509_REQ_get_subject_name(x509Req.get());
    if (!req.countryCode.empty())
        X509_NAME_add_entry_by_txt(name, "C", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(req.countryCode.c_str()), -1, -1, 0);
    if (!req.organization.empty())
        X509_NAME_add_entry_by_txt(name, "O", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(req.organization.c_str()), -1, -1, 0);
    if (!req.commonName.empty())
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(req.commonName.c_str()), -1, -1, 0);

    X509_REQ_set_pubkey(x509Req.get(), pkey.get());
    if (X509_REQ_sign(x509Req.get(), pkey.get(), EVP_sha256()) <= 0) {
        result.errorMessage = "CSR signing failed (SHA256withRSA)";
        return result;
    }

    // 3. Export CSR PEM
    std::unique_ptr<BIO, decltype(&BIO_free)> pemBio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_X509_REQ(pemBio.get(), x509Req.get());
    char* pemData = nullptr;
    long pemLen = BIO_get_mem_data(pemBio.get(), &pemData);
    result.csrPem = std::string(pemData, pemLen);

    // 4. Export CSR DER
    std::unique_ptr<BIO, decltype(&BIO_free)> derBio(BIO_new(BIO_s_mem()), BIO_free);
    i2d_X509_REQ_bio(derBio.get(), x509Req.get());
    char* derData = nullptr;
    long derLen = BIO_get_mem_data(derBio.get(), &derData);
    std::vector<uint8_t> csrDer(derData, derData + derLen);

    // 5. Fingerprint
    result.publicKeyFingerprint = computePubKeyFingerprint(pkey.get());

    // 6. Export & encrypt private key
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_PrivateKey(keyBio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr);
    char* keyData = nullptr;
    long keyLen = BIO_get_mem_data(keyBio.get(), &keyData);
    std::string privateKeyEncrypted = auth::pii::encrypt(std::string(keyData, keyLen));
    std::string csrPemEncrypted = auth::pii::encrypt(result.csrPem);

    // 7. Save to DB
    std::string csrId = csrRepo_->save(
        result.subjectDn, req.countryCode, req.organization, req.commonName,
        "RSA-2048", "SHA256withRSA", csrPemEncrypted, csrDer,
        result.publicKeyFingerprint, privateKeyEncrypted, req.memo, req.createdBy);

    if (csrId.empty()) { result.errorMessage = "Failed to save CSR to database"; return result; }

    result.id = csrId;
    result.success = true;
    spdlog::info("[CsrService] CSR generated: id={}, dn={}", csrId, result.subjectDn);
    return result;
}

CsrGenerateResult CsrService::importCsr(
    const std::string& csrPem, const std::string& privateKeyPem,
    const std::string& memo, const std::string& createdBy)
{
    CsrGenerateResult result;
    result.success = false;

    // 1. Parse CSR
    std::unique_ptr<BIO, decltype(&BIO_free)> csrBio(
        BIO_new_mem_buf(csrPem.data(), static_cast<int>(csrPem.size())), BIO_free);
    std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)> x509Req(
        PEM_read_bio_X509_REQ(csrBio.get(), nullptr, nullptr, nullptr), X509_REQ_free);
    if (!x509Req) { result.errorMessage = "Invalid CSR PEM format"; return result; }

    // 2. Parse private key
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(
        BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size())), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey) { result.errorMessage = "Invalid private key PEM format"; return result; }

    // 3. Verify CSR signature + key match
    EVP_PKEY* csrPubKey = X509_REQ_get0_pubkey(x509Req.get());
    if (!csrPubKey) { result.errorMessage = "Failed to extract public key from CSR"; return result; }
    if (X509_REQ_verify(x509Req.get(), csrPubKey) != 1) { result.errorMessage = "CSR signature verification failed"; return result; }
    if (EVP_PKEY_eq(pkey.get(), csrPubKey) != 1) { result.errorMessage = "Private key does not match CSR public key"; return result; }

    // 4. Extract subject DN
    X509_NAME* name = X509_REQ_get_subject_name(x509Req.get());
    char subjectBuf[1024] = {};
    X509_NAME_oneline(name, subjectBuf, sizeof(subjectBuf));
    result.subjectDn = std::string(subjectBuf);

    std::string countryCode, organization, commonName;
    int idx = X509_NAME_get_index_by_NID(name, NID_countryName, -1);
    if (idx >= 0) { auto* e = X509_NAME_get_entry(name, idx); auto* d = X509_NAME_ENTRY_get_data(e); countryCode.assign(reinterpret_cast<const char*>(ASN1_STRING_get0_data(d)), ASN1_STRING_length(d)); }
    idx = X509_NAME_get_index_by_NID(name, NID_organizationName, -1);
    if (idx >= 0) { auto* e = X509_NAME_get_entry(name, idx); auto* d = X509_NAME_ENTRY_get_data(e); organization.assign(reinterpret_cast<const char*>(ASN1_STRING_get0_data(d)), ASN1_STRING_length(d)); }
    idx = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (idx >= 0) { auto* e = X509_NAME_get_entry(name, idx); auto* d = X509_NAME_ENTRY_get_data(e); commonName.assign(reinterpret_cast<const char*>(ASN1_STRING_get0_data(d)), ASN1_STRING_length(d)); }

    // 5. DER + fingerprint
    int derLen = i2d_X509_REQ(x509Req.get(), nullptr);
    std::vector<uint8_t> csrDer(derLen);
    unsigned char* derPtr = csrDer.data();
    i2d_X509_REQ(x509Req.get(), &derPtr);

    result.publicKeyFingerprint = computePubKeyFingerprint(csrPubKey);

    // 6. Detect algorithm
    int keyBits = EVP_PKEY_bits(pkey.get());
    std::string keyAlgorithm = (EVP_PKEY_base_id(pkey.get()) == EVP_PKEY_RSA) ? "RSA-" + std::to_string(keyBits) : "UNKNOWN-" + std::to_string(keyBits);
    const X509_ALGOR* sigAlg = nullptr;
    X509_REQ_get0_signature(x509Req.get(), nullptr, &sigAlg);
    int sigNid = OBJ_obj2nid(sigAlg->algorithm);
    std::string signatureAlgorithm = (sigNid != NID_undef) ? OBJ_nid2sn(sigNid) : "unknown";

    // 7. Encrypt and save
    std::string csrId = csrRepo_->save(
        result.subjectDn, countryCode, organization, commonName,
        keyAlgorithm, signatureAlgorithm,
        auth::pii::encrypt(csrPem), csrDer, result.publicKeyFingerprint,
        auth::pii::encrypt(privateKeyPem), memo, createdBy);

    if (csrId.empty()) { result.errorMessage = "Failed to save imported CSR to database"; return result; }

    result.id = csrId;
    result.csrPem = csrPem;
    result.success = true;
    spdlog::info("[CsrService] CSR imported: id={}, dn={}", csrId, result.subjectDn);
    return result;
}

CsrGenerateResult CsrService::registerCertificate(
    const std::string& id, const std::string& certPem, const std::string& username)
{
    CsrGenerateResult result;
    result.success = false;
    result.id = id;

    Json::Value csrData = csrRepo_->findById(id);
    if (csrData.isNull()) { result.errorMessage = "CSR not found"; return result; }
    if (csrData.get("status", "").asString() == "ISSUED") { result.errorMessage = "Certificate already registered for this CSR"; return result; }

    // Parse certificate
    std::unique_ptr<BIO, decltype(&BIO_free)> certBio(
        BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size())), BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> cert(
        PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!cert) { BIO_reset(certBio.get()); cert.reset(d2i_X509_bio(certBio.get(), nullptr)); }
    if (!cert) { result.errorMessage = "Failed to parse certificate (invalid PEM or DER)"; return result; }

    // Verify public key match
    EVP_PKEY* certPubKey = X509_get0_pubkey(cert.get());
    if (!certPubKey) { result.errorMessage = "Failed to extract public key from certificate"; return result; }
    std::string certFpHex = computePubKeyFingerprint(certPubKey);
    std::string csrFingerprint = csrData.get("public_key_fingerprint", "").asString();
    if (certFpHex != csrFingerprint) {
        result.errorMessage = "Certificate public key does not match CSR (fingerprint mismatch)";
        return result;
    }

    // Extract metadata
    static const char hexChars[] = "0123456789abcdef";

    ASN1_INTEGER* serialAsn1 = X509_get_serialNumber(cert.get());
    BIGNUM* serialBn = ASN1_INTEGER_to_BN(serialAsn1, nullptr);
    char* serialHex = serialBn ? BN_bn2hex(serialBn) : nullptr;
    std::string serial = serialHex ? serialHex : "unknown";
    if (serialHex) OPENSSL_free(serialHex);
    if (serialBn) BN_free(serialBn);

    char subjectBuf[1024] = {}, issuerBuf[1024] = {};
    X509_NAME_oneline(X509_get_subject_name(cert.get()), subjectBuf, sizeof(subjectBuf));
    X509_NAME_oneline(X509_get_issuer_name(cert.get()), issuerBuf, sizeof(issuerBuf));

    auto asn1TimeToStr = [](const ASN1_TIME* t) -> std::string {
        if (!t) return "";
        struct tm tm_val = {};
        if (ASN1_TIME_to_tm(t, &tm_val) != 1) return "";
        char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
        return buf;
    };

    unsigned char certDigest[32]; unsigned int certDigestLen = 0;
    X509_digest(cert.get(), EVP_sha256(), certDigest, &certDigestLen);
    std::string certDigestHex;
    for (unsigned int i = 0; i < certDigestLen; i++) { certDigestHex += hexChars[certDigest[i] >> 4]; certDigestHex += hexChars[certDigest[i] & 0x0F]; }

    int certDerLen = i2d_X509(cert.get(), nullptr);
    std::vector<uint8_t> certDer(certDerLen);
    unsigned char* dPtr = certDer.data();
    i2d_X509(cert.get(), &dPtr);

    bool saved = csrRepo_->registerCertificate(
        id, auth::pii::encrypt(certPem), certDer,
        serial, subjectBuf, issuerBuf,
        asn1TimeToStr(X509_get0_notBefore(cert.get())),
        asn1TimeToStr(X509_get0_notAfter(cert.get())),
        certDigestHex, username);

    if (!saved) { result.errorMessage = "Failed to save certificate to database"; return result; }

    result.success = true;
    result.subjectDn = subjectBuf;
    result.publicKeyFingerprint = certDigestHex;
    spdlog::info("[CsrService] Certificate registered for CSR: id={}, issuer={}", id, issuerBuf);
    return result;
}

Json::Value CsrService::getById(const std::string& id)
{
    Json::Value data = csrRepo_->findById(id);
    if (!data.isNull()) {
        std::string encPem = data.get("csr_pem", "").asString();
        if (!encPem.empty()) data["csr_pem"] = auth::pii::decrypt(encPem);
    }
    return data;
}

Json::Value CsrService::list(int page, int pageSize, const std::string& statusFilter)
{
    int offset = (page - 1) * pageSize;
    Json::Value response;
    response["success"] = true;
    response["total"] = csrRepo_->countAll(statusFilter);
    response["page"] = page;
    response["pageSize"] = pageSize;
    response["data"] = csrRepo_->findAll(pageSize, offset, statusFilter);
    return response;
}

std::string CsrService::getPemById(const std::string& id)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query = (dbType == "oracle")
            ? "SELECT TO_CHAR(csr_pem) as csr_pem FROM csr_request WHERE id = $1"
            : "SELECT csr_pem FROM csr_request WHERE id = $1";
        Json::Value result = queryExecutor_->executeQuery(query, {id});
        if (result.empty()) return "";
        return auth::pii::decrypt(result[0].get("csr_pem", "").asString());
    } catch (const std::exception& e) {
        spdlog::error("[CsrService] getPemById failed: {}", e.what());
        return "";
    }
}

bool CsrService::deleteById(const std::string& id)
{
    return csrRepo_->deleteById(id);
}

CsrGenerateResult CsrService::signWithCA(
    const std::string& id,
    const std::string& caKeyPath,
    const std::string& caCertPath,
    const std::string& outputDir,
    const std::string& username)
{
    CsrGenerateResult result;
    result.success = false;
    result.id = id;

    // 1. Load CSR from DB
    Json::Value csrData = csrRepo_->findById(id);
    if (csrData.isNull()) { result.errorMessage = "CSR not found"; return result; }
    if (csrData.get("status", "").asString() == "ISSUED") {
        result.errorMessage = "Certificate already issued for this CSR";
        return result;
    }

    // 2. Load Private CA key
    std::unique_ptr<BIO, decltype(&BIO_free)> caKeyBio(
        BIO_new_file(caKeyPath.c_str(), "r"), BIO_free);
    if (!caKeyBio) { result.errorMessage = "Cannot open CA key file: " + caKeyPath; return result; }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> caKey(
        PEM_read_bio_PrivateKey(caKeyBio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!caKey) { result.errorMessage = "Failed to parse CA key"; return result; }

    // 3. Load Private CA certificate
    std::unique_ptr<BIO, decltype(&BIO_free)> caCertBio(
        BIO_new_file(caCertPath.c_str(), "r"), BIO_free);
    if (!caCertBio) { result.errorMessage = "Cannot open CA cert file: " + caCertPath; return result; }
    std::unique_ptr<X509, decltype(&X509_free)> caCert(
        PEM_read_bio_X509(caCertBio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!caCert) { result.errorMessage = "Failed to parse CA certificate"; return result; }

    // 4. Load CSR DER from DB and parse
    std::string encPem = csrData.get("csr_pem", "").asString();
    std::string csrPem = auth::pii::decrypt(encPem);
    if (csrPem.empty()) { result.errorMessage = "Failed to decrypt CSR PEM"; return result; }

    std::unique_ptr<BIO, decltype(&BIO_free)> csrBio(
        BIO_new_mem_buf(csrPem.data(), static_cast<int>(csrPem.size())), BIO_free);
    std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)> csr(
        PEM_read_bio_X509_REQ(csrBio.get(), nullptr, nullptr, nullptr), X509_REQ_free);
    if (!csr) { result.errorMessage = "Failed to parse CSR"; return result; }

    // 5. Create X509 certificate from CSR
    std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
    if (!cert) { result.errorMessage = "Failed to create X509 structure"; return result; }

    X509_set_version(cert.get(), 2);  // v3

    // Serial number (random)
    std::unique_ptr<ASN1_INTEGER, decltype(&ASN1_INTEGER_free)> serialNum(
        ASN1_INTEGER_new(), ASN1_INTEGER_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> bn(BN_new(), BN_free);
    BN_rand(bn.get(), 64, 0, 0);
    BN_to_ASN1_INTEGER(bn.get(), serialNum.get());
    X509_set_serialNumber(cert.get(), serialNum.get());

    // Validity: now ~ 365 days
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 365 * 24 * 3600);

    // Subject from CSR, Issuer from CA
    X509_set_subject_name(cert.get(), X509_REQ_get_subject_name(csr.get()));
    X509_set_issuer_name(cert.get(), X509_get_subject_name(caCert.get()));

    // Public key from CSR
    EVP_PKEY* reqPubKey = X509_REQ_get0_pubkey(csr.get());
    X509_set_pubkey(cert.get(), reqPubKey);

    // Sign with CA key (SHA256)
    if (!X509_sign(cert.get(), caKey.get(), EVP_sha256())) {
        result.errorMessage = "Failed to sign certificate with CA key";
        return result;
    }

    // 6. Convert to PEM
    std::unique_ptr<BIO, decltype(&BIO_free)> certPemBio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_X509(certPemBio.get(), cert.get());
    char* pemData = nullptr;
    long pemLen = BIO_get_mem_data(certPemBio.get(), &pemData);
    std::string certPem(pemData, pemLen);

    // 7. Register certificate via existing registerCertificate method
    auto regResult = registerCertificate(id, certPem, username);
    if (!regResult.success) return regResult;

    // 8. Save files to outputDir for TLS usage
    try {
        // Client certificate
        {
            std::string clientCertPath = outputDir + "/client.pem";
            std::unique_ptr<BIO, decltype(&BIO_free)> outBio(
                BIO_new_file(clientCertPath.c_str(), "w"), BIO_free);
            if (outBio) PEM_write_bio_X509(outBio.get(), cert.get());
            spdlog::info("[CsrService] Client cert saved: {}", clientCertPath);
        }

        // Client private key (query directly from DB — not in findById for security)
        {
            std::string dbType = queryExecutor_->getDatabaseType();
            std::string keyQuery = (dbType == "oracle")
                ? "SELECT TO_CHAR(private_key_encrypted) as private_key_encrypted FROM csr_request WHERE id = $1"
                : "SELECT private_key_encrypted FROM csr_request WHERE id = $1";
            auto keyResult = queryExecutor_->executeQuery(keyQuery, {id});
            std::string encKey = keyResult.empty() ? "" : keyResult[0].get("private_key_encrypted", "").asString();
            std::string privKeyPem = auth::pii::decrypt(encKey);
            if (!privKeyPem.empty()) {
                std::string keyPath = outputDir + "/client-key.pem";
                std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(
                    BIO_new_file(keyPath.c_str(), "w"), BIO_free);
                if (keyBio) BIO_write(keyBio.get(), privKeyPem.data(), static_cast<int>(privKeyPem.size()));
                spdlog::info("[CsrService] Client key saved: {}", keyPath);
            }
        }

        // CA certificate (copy)
        {
            std::string caOutPath = outputDir + "/ca.pem";
            std::unique_ptr<BIO, decltype(&BIO_free)> caOutBio(
                BIO_new_file(caOutPath.c_str(), "w"), BIO_free);
            if (caOutBio) PEM_write_bio_X509(caOutBio.get(), caCert.get());
            spdlog::info("[CsrService] CA cert saved: {}", caOutPath);
        }
    } catch (const std::exception& e) {
        spdlog::warn("[CsrService] Failed to save TLS files: {} (non-critical)", e.what());
    }

    result.success = true;
    result.subjectDn = regResult.subjectDn;
    result.publicKeyFingerprint = regResult.publicKeyFingerprint;
    spdlog::info("[CsrService] CSR signed with CA: id={}, issuer={}",
                id, X509_NAME_oneline(X509_get_subject_name(caCert.get()), nullptr, 0));
    return result;
}

} // namespace services

#include "csr_service.h"
#include "../repositories/csr_repository.h"
#include "../auth/personal_info_crypto.h"
#include <icao/audit/audit_log.h>
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

// --- Helper: Audit log ---
void CsrService::logAudit(icao::audit::OperationType opType,
                           const std::string& username,
                           const std::string& resourceId,
                           const Json::Value& metadata,
                           bool success,
                           const std::string& errorMessage)
{
    try {
        icao::audit::AuditLogEntry entry;
        entry.operationType = opType;
        entry.username = username;
        entry.success = success;
        entry.resourceId = resourceId;
        if (!errorMessage.empty()) entry.errorMessage = errorMessage;
        if (!metadata.isNull()) entry.metadata = metadata;
        icao::audit::logOperation(queryExecutor_, entry);
    } catch (...) {
        spdlog::warn("[CsrService] Audit log failed for {}", icao::audit::operationTypeToString(opType));
    }
}

std::string CsrService::buildSubjectDn(const std::string& countryCode,
                                        const std::string& organization,
                                        const std::string& commonName)
{
    std::string dn;
    if (!countryCode.empty()) {
        dn += "/C=" + countryCode;
    }
    if (!organization.empty()) {
        dn += "/O=" + organization;
    }
    if (!commonName.empty()) {
        dn += "/CN=" + commonName;
    }
    return dn.empty() ? "/CN=ICAO PKD" : dn;
}

CsrGenerateResult CsrService::generate(const CsrGenerateRequest& req)
{
    CsrGenerateResult result;
    result.success = false;

    spdlog::info("[CsrService] Generating CSR: C={}, O={}, CN={}", req.countryCode, req.organization, req.commonName);

    // --- 1. Generate RSA-2048 key pair ---
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> keyCtx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    if (!keyCtx) {
        result.errorMessage = "Failed to create key context";
        spdlog::error("[CsrService] {}", result.errorMessage);
        logAudit(icao::audit::OperationType::CSR_GENERATE, req.createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    if (EVP_PKEY_keygen_init(keyCtx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(keyCtx.get(), 2048) <= 0) {
        result.errorMessage = "Failed to initialize key generation";
        spdlog::error("[CsrService] {}", result.errorMessage);
        logAudit(icao::audit::OperationType::CSR_GENERATE, req.createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    EVP_PKEY* rawKey = nullptr;
    if (EVP_PKEY_keygen(keyCtx.get(), &rawKey) <= 0) {
        result.errorMessage = "RSA-2048 key generation failed";
        spdlog::error("[CsrService] {}", result.errorMessage);
        logAudit(icao::audit::OperationType::CSR_GENERATE, req.createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(rawKey, EVP_PKEY_free);

    // --- 2. Create X509_REQ (CSR) ---
    std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)> x509Req(X509_REQ_new(), X509_REQ_free);
    if (!x509Req) {
        result.errorMessage = "Failed to create X509_REQ";
        spdlog::error("[CsrService] {}", result.errorMessage);
        logAudit(icao::audit::OperationType::CSR_GENERATE, req.createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    // Set version (v1 = 0)
    X509_REQ_set_version(x509Req.get(), 0);

    // Set subject DN
    result.subjectDn = buildSubjectDn(req.countryCode, req.organization, req.commonName);
    X509_NAME* name = X509_REQ_get_subject_name(x509Req.get());
    if (!req.countryCode.empty()) {
        X509_NAME_add_entry_by_txt(name, "C", MBSTRING_UTF8,
            reinterpret_cast<const unsigned char*>(req.countryCode.c_str()), -1, -1, 0);
    }
    if (!req.organization.empty()) {
        X509_NAME_add_entry_by_txt(name, "O", MBSTRING_UTF8,
            reinterpret_cast<const unsigned char*>(req.organization.c_str()), -1, -1, 0);
    }
    if (!req.commonName.empty()) {
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
            reinterpret_cast<const unsigned char*>(req.commonName.c_str()), -1, -1, 0);
    }

    // Set public key
    X509_REQ_set_pubkey(x509Req.get(), pkey.get());

    // Sign CSR with SHA256withRSA
    if (X509_REQ_sign(x509Req.get(), pkey.get(), EVP_sha256()) <= 0) {
        result.errorMessage = "CSR signing failed (SHA256withRSA)";
        spdlog::error("[CsrService] {}", result.errorMessage);
        logAudit(icao::audit::OperationType::CSR_GENERATE, req.createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    // --- 3. Export CSR to PEM ---
    std::unique_ptr<BIO, decltype(&BIO_free)> pemBio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_X509_REQ(pemBio.get(), x509Req.get());
    char* pemData = nullptr;
    long pemLen = BIO_get_mem_data(pemBio.get(), &pemData);
    result.csrPem = std::string(pemData, pemLen);

    // --- 4. Export CSR to DER ---
    std::unique_ptr<BIO, decltype(&BIO_free)> derBio(BIO_new(BIO_s_mem()), BIO_free);
    i2d_X509_REQ_bio(derBio.get(), x509Req.get());
    char* derData = nullptr;
    long derLen = BIO_get_mem_data(derBio.get(), &derData);
    std::vector<uint8_t> csrDer(derData, derData + derLen);

    // --- 5. Compute public key fingerprint (SHA-256) ---
    unsigned char fingerprint[32];
    unsigned int fpLen = 0;
    {
        std::unique_ptr<BIO, decltype(&BIO_free)> pubBio(BIO_new(BIO_s_mem()), BIO_free);
        i2d_PUBKEY_bio(pubBio.get(), pkey.get());
        char* pubData = nullptr;
        long pubLen = BIO_get_mem_data(pubBio.get(), &pubData);

        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> mdCtx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (mdCtx) {
            EVP_DigestInit_ex(mdCtx.get(), EVP_sha256(), nullptr);
            EVP_DigestUpdate(mdCtx.get(), pubData, pubLen);
            EVP_DigestFinal_ex(mdCtx.get(), fingerprint, &fpLen);
        }
    }

    // Convert fingerprint to hex
    std::string fpHex;
    fpHex.reserve(64);
    static const char hexChars[] = "0123456789abcdef";
    for (unsigned int i = 0; i < fpLen; i++) {
        fpHex += hexChars[fingerprint[i] >> 4];
        fpHex += hexChars[fingerprint[i] & 0x0F];
    }
    result.publicKeyFingerprint = fpHex;

    // --- 6. Export private key to PEM and encrypt ---
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_PrivateKey(keyBio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr);
    char* keyData = nullptr;
    long keyLen = BIO_get_mem_data(keyBio.get(), &keyData);
    std::string privateKeyPem(keyData, keyLen);

    // Encrypt all sensitive data with AES-256-GCM (PII_ENCRYPTION_KEY)
    std::string privateKeyEncrypted = auth::pii::encrypt(privateKeyPem);
    std::string csrPemEncrypted = auth::pii::encrypt(result.csrPem);

    // --- 7. Save to DB (all sensitive fields encrypted) ---
    std::string csrId = csrRepo_->save(
        result.subjectDn,
        req.countryCode,
        req.organization,
        req.commonName,
        "RSA-2048",
        "SHA256withRSA",
        csrPemEncrypted,   // CSR PEM encrypted
        csrDer,            // DER binary (for export, encrypted at rest via DB TDE or storage encryption)
        fpHex,
        privateKeyEncrypted,
        req.memo,
        req.createdBy
    );

    if (csrId.empty()) {
        result.errorMessage = "Failed to save CSR to database";
        spdlog::error("[CsrService] {}", result.errorMessage);
        logAudit(icao::audit::OperationType::CSR_GENERATE, req.createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    result.id = csrId;
    result.success = true;

    // Audit log — success
    Json::Value meta;
    meta["subjectDn"] = result.subjectDn;
    meta["fingerprint"] = fpHex;
    meta["keyAlgorithm"] = "RSA-2048";
    meta["signatureAlgorithm"] = "SHA256withRSA";
    logAudit(icao::audit::OperationType::CSR_GENERATE, req.createdBy, csrId, meta);

    spdlog::info("[CsrService] CSR generated successfully: id={}, dn={}, fingerprint={}",
        csrId, result.subjectDn, fpHex);
    return result;
}

Json::Value CsrService::getById(const std::string& id, const std::string& username)
{
    Json::Value data = csrRepo_->findById(id);
    if (!data.isNull()) {
        // Decrypt CSR PEM for display
        std::string encryptedPem = data.get("csr_pem", "").asString();
        if (!encryptedPem.empty()) {
            data["csr_pem"] = auth::pii::decrypt(encryptedPem);
        }

        // Audit log — view
        Json::Value meta;
        meta["subjectDn"] = data.get("subject_dn", "").asString();
        logAudit(icao::audit::OperationType::CSR_VIEW, username, id, meta);
    }
    return data;
}

Json::Value CsrService::list(int page, int pageSize, const std::string& statusFilter)
{
    int offset = (page - 1) * pageSize;
    int total = csrRepo_->countAll(statusFilter);

    Json::Value response;
    response["success"] = true;
    response["total"] = total;
    response["page"] = page;
    response["pageSize"] = pageSize;
    response["data"] = csrRepo_->findAll(pageSize, offset, statusFilter);
    return response;
}

std::vector<uint8_t> CsrService::getDerById(const std::string& id, const std::string& username)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            query = "SELECT RAWTOHEX(DBMS_LOB.SUBSTR(csr_der, DBMS_LOB.GETLENGTH(csr_der), 1)) as csr_der_hex "
                    "FROM csr_request WHERE id = $1";
        } else {
            query = "SELECT encode(csr_der, 'hex') as csr_der_hex FROM csr_request WHERE id = $1";
        }

        Json::Value result = queryExecutor_->executeQuery(query, {id});
        if (result.empty()) return {};

        std::string hexStr = result[0].get("csr_der_hex", "").asString();
        if (hexStr.empty()) return {};

        // Convert hex to bytes
        std::vector<uint8_t> der;
        der.reserve(hexStr.size() / 2);
        for (size_t i = 0; i + 1 < hexStr.size(); i += 2) {
            uint8_t byte = 0;
            char c1 = hexStr[i], c2 = hexStr[i + 1];
            byte = static_cast<uint8_t>(
                ((c1 >= 'a' ? c1 - 'a' + 10 : c1 - '0') << 4) |
                 (c2 >= 'a' ? c2 - 'a' + 10 : c2 - '0'));
            der.push_back(byte);
        }

        // Audit log — export DER
        Json::Value meta;
        meta["format"] = "DER";
        logAudit(icao::audit::OperationType::CSR_EXPORT, username, id, meta);

        return der;
    } catch (const std::exception& e) {
        spdlog::error("[CsrService] getDerById failed: {}", e.what());
        return {};
    }
}

std::string CsrService::getPemById(const std::string& id, const std::string& username)
{
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            query = "SELECT TO_CHAR(csr_pem) as csr_pem FROM csr_request WHERE id = $1";
        } else {
            query = "SELECT csr_pem FROM csr_request WHERE id = $1";
        }

        Json::Value result = queryExecutor_->executeQuery(query, {id});
        if (result.empty()) return "";

        // Decrypt CSR PEM
        std::string encryptedPem = result[0].get("csr_pem", "").asString();
        std::string pem = auth::pii::decrypt(encryptedPem);

        // Audit log — export PEM
        Json::Value meta;
        meta["format"] = "PEM";
        logAudit(icao::audit::OperationType::CSR_EXPORT, username, id, meta);

        return pem;
    } catch (const std::exception& e) {
        spdlog::error("[CsrService] getPemById failed: {}", e.what());
        return "";
    }
}

CsrGenerateResult CsrService::importCsr(
    const std::string& csrPem,
    const std::string& privateKeyPem,
    const std::string& memo,
    const std::string& createdBy)
{
    CsrGenerateResult result;
    result.success = false;

    spdlog::info("[CsrService] Importing external CSR");

    // --- 1. Parse CSR ---
    std::unique_ptr<BIO, decltype(&BIO_free)> csrBio(
        BIO_new_mem_buf(csrPem.data(), static_cast<int>(csrPem.size())), BIO_free);
    if (!csrBio) {
        result.errorMessage = "Failed to create BIO for CSR";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)> x509Req(
        PEM_read_bio_X509_REQ(csrBio.get(), nullptr, nullptr, nullptr), X509_REQ_free);
    if (!x509Req) {
        result.errorMessage = "Invalid CSR PEM format";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    // --- 2. Parse private key ---
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(
        BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size())), BIO_free);
    if (!keyBio) {
        result.errorMessage = "Failed to create BIO for private key";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey) {
        result.errorMessage = "Invalid private key PEM format";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    // --- 3. Verify CSR signature with private key's public key ---
    EVP_PKEY* csrPubKey = X509_REQ_get0_pubkey(x509Req.get());
    if (!csrPubKey) {
        result.errorMessage = "Failed to extract public key from CSR";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    if (X509_REQ_verify(x509Req.get(), csrPubKey) != 1) {
        result.errorMessage = "CSR signature verification failed";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    // Verify private key matches CSR public key
    if (EVP_PKEY_eq(pkey.get(), csrPubKey) != 1) {
        result.errorMessage = "Private key does not match CSR public key";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    // --- 4. Extract subject DN ---
    X509_NAME* name = X509_REQ_get_subject_name(x509Req.get());
    char subjectBuf[1024] = {};
    X509_NAME_oneline(name, subjectBuf, sizeof(subjectBuf));
    result.subjectDn = std::string(subjectBuf);

    // Extract individual DN components
    std::string countryCode, organization, commonName;
    int idx = X509_NAME_get_index_by_NID(name, NID_countryName, -1);
    if (idx >= 0) {
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, idx);
        ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        countryCode = std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(data)), ASN1_STRING_length(data));
    }
    idx = X509_NAME_get_index_by_NID(name, NID_organizationName, -1);
    if (idx >= 0) {
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, idx);
        ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        organization = std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(data)), ASN1_STRING_length(data));
    }
    idx = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (idx >= 0) {
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, idx);
        ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        commonName = std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(data)), ASN1_STRING_length(data));
    }

    // --- 5. Export CSR to DER ---
    int derLen = i2d_X509_REQ(x509Req.get(), nullptr);
    std::vector<uint8_t> csrDer(derLen);
    unsigned char* derPtr = csrDer.data();
    i2d_X509_REQ(x509Req.get(), &derPtr);

    // --- 6. Compute public key fingerprint ---
    unsigned char fingerprint[32];
    unsigned int fpLen = 0;
    {
        std::unique_ptr<BIO, decltype(&BIO_free)> pubBio(BIO_new(BIO_s_mem()), BIO_free);
        i2d_PUBKEY_bio(pubBio.get(), csrPubKey);
        char* pubData = nullptr;
        long pubLen = BIO_get_mem_data(pubBio.get(), &pubData);

        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> mdCtx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (mdCtx) {
            EVP_DigestInit_ex(mdCtx.get(), EVP_sha256(), nullptr);
            EVP_DigestUpdate(mdCtx.get(), pubData, pubLen);
            EVP_DigestFinal_ex(mdCtx.get(), fingerprint, &fpLen);
        }
    }

    std::string fpHex;
    fpHex.reserve(64);
    static const char hexChars[] = "0123456789abcdef";
    for (unsigned int i = 0; i < fpLen; i++) {
        fpHex += hexChars[fingerprint[i] >> 4];
        fpHex += hexChars[fingerprint[i] & 0x0F];
    }
    result.publicKeyFingerprint = fpHex;

    // --- 7. Detect algorithm ---
    int keyType = EVP_PKEY_base_id(pkey.get());
    int keyBits = EVP_PKEY_bits(pkey.get());
    std::string keyAlgorithm = (keyType == EVP_PKEY_RSA) ? "RSA-" + std::to_string(keyBits) : "UNKNOWN-" + std::to_string(keyBits);

    // Get signature algorithm from CSR
    const X509_ALGOR* sigAlg = nullptr;
    X509_REQ_get0_signature(x509Req.get(), nullptr, &sigAlg);
    int sigNid = OBJ_obj2nid(sigAlg->algorithm);
    std::string signatureAlgorithm = (sigNid != NID_undef) ? OBJ_nid2sn(sigNid) : "unknown";

    // --- 8. Encrypt and save ---
    std::string privateKeyEncrypted = auth::pii::encrypt(privateKeyPem);
    std::string csrPemEncrypted = auth::pii::encrypt(csrPem);

    std::string csrId = csrRepo_->save(
        result.subjectDn,
        countryCode,
        organization,
        commonName,
        keyAlgorithm,
        signatureAlgorithm,
        csrPemEncrypted,
        csrDer,
        fpHex,
        privateKeyEncrypted,
        memo,
        createdBy
    );

    if (csrId.empty()) {
        result.errorMessage = "Failed to save imported CSR to database";
        logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, "", Json::nullValue, false, result.errorMessage);
        return result;
    }

    result.id = csrId;
    result.csrPem = csrPem;
    result.success = true;

    Json::Value meta;
    meta["subjectDn"] = result.subjectDn;
    meta["fingerprint"] = fpHex;
    meta["source"] = "IMPORT";
    meta["keyAlgorithm"] = keyAlgorithm;
    logAudit(icao::audit::OperationType::CSR_GENERATE, createdBy, csrId, meta);

    spdlog::info("[CsrService] CSR imported successfully: id={}, dn={}", csrId, result.subjectDn);
    return result;
}

CsrGenerateResult CsrService::registerCertificate(
    const std::string& id,
    const std::string& certPem,
    const std::string& username)
{
    CsrGenerateResult result;
    result.success = false;
    result.id = id;

    spdlog::info("[CsrService] Registering certificate for CSR: {}", id);

    // --- 1. Verify CSR exists and is in correct state ---
    Json::Value csrData = csrRepo_->findById(id);
    if (csrData.isNull()) {
        result.errorMessage = "CSR not found";
        logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, Json::nullValue, false, result.errorMessage);
        return result;
    }

    std::string currentStatus = csrData.get("status", "").asString();
    if (currentStatus == "ISSUED") {
        result.errorMessage = "Certificate already registered for this CSR";
        logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, Json::nullValue, false, result.errorMessage);
        return result;
    }

    // --- 2. Parse X.509 certificate ---
    std::unique_ptr<BIO, decltype(&BIO_free)> certBio(
        BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size())), BIO_free);
    if (!certBio) {
        result.errorMessage = "Failed to create BIO for certificate";
        logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, Json::nullValue, false, result.errorMessage);
        return result;
    }

    std::unique_ptr<X509, decltype(&X509_free)> cert(
        PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!cert) {
        // Try DER format
        BIO_reset(certBio.get());
        cert.reset(d2i_X509_bio(certBio.get(), nullptr));
    }
    if (!cert) {
        result.errorMessage = "Failed to parse certificate (invalid PEM or DER format)";
        logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, Json::nullValue, false, result.errorMessage);
        return result;
    }

    // --- 3. Verify public key matches CSR ---
    // Get CSR's public key fingerprint from DB
    std::string csrFingerprint = csrData.get("public_key_fingerprint", "").asString();

    // Compute certificate's public key fingerprint
    EVP_PKEY* certPubKey = X509_get0_pubkey(cert.get());
    if (!certPubKey) {
        result.errorMessage = "Failed to extract public key from certificate";
        logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, Json::nullValue, false, result.errorMessage);
        return result;
    }

    unsigned char certFp[32];
    unsigned int certFpLen = 0;
    {
        std::unique_ptr<BIO, decltype(&BIO_free)> pubBio(BIO_new(BIO_s_mem()), BIO_free);
        i2d_PUBKEY_bio(pubBio.get(), certPubKey);
        char* pubData = nullptr;
        long pubLen = BIO_get_mem_data(pubBio.get(), &pubData);

        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> mdCtx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (mdCtx) {
            EVP_DigestInit_ex(mdCtx.get(), EVP_sha256(), nullptr);
            EVP_DigestUpdate(mdCtx.get(), pubData, pubLen);
            EVP_DigestFinal_ex(mdCtx.get(), certFp, &certFpLen);
        }
    }

    std::string certFpHex;
    certFpHex.reserve(64);
    static const char hexChars[] = "0123456789abcdef";
    for (unsigned int i = 0; i < certFpLen; i++) {
        certFpHex += hexChars[certFp[i] >> 4];
        certFpHex += hexChars[certFp[i] & 0x0F];
    }

    if (certFpHex != csrFingerprint) {
        result.errorMessage = "Certificate public key does not match CSR (fingerprint mismatch)";
        Json::Value meta;
        meta["csrFingerprint"] = csrFingerprint;
        meta["certFingerprint"] = certFpHex;
        logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, meta, false, result.errorMessage);
        return result;
    }

    // --- 4. Extract certificate metadata ---
    // Serial number
    ASN1_INTEGER* serialAsn1 = X509_get_serialNumber(cert.get());
    BIGNUM* serialBn = ASN1_INTEGER_to_BN(serialAsn1, nullptr);
    char* serialHex = serialBn ? BN_bn2hex(serialBn) : nullptr;
    std::string serial = serialHex ? serialHex : "unknown";
    if (serialHex) OPENSSL_free(serialHex);
    if (serialBn) BN_free(serialBn);

    // Subject DN
    char subjectBuf[1024] = {};
    X509_NAME_oneline(X509_get_subject_name(cert.get()), subjectBuf, sizeof(subjectBuf));
    std::string certSubjectDn(subjectBuf);

    // Issuer DN
    char issuerBuf[1024] = {};
    X509_NAME_oneline(X509_get_issuer_name(cert.get()), issuerBuf, sizeof(issuerBuf));
    std::string certIssuerDn(issuerBuf);

    // Validity dates (format: YYYY-MM-DD HH:MM:SS)
    auto asn1TimeToString = [](const ASN1_TIME* t) -> std::string {
        if (!t) return "";
        struct tm tm_val = {};
        if (ASN1_TIME_to_tm(t, &tm_val) != 1) return "";
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
        return std::string(buf);
    };

    std::string notBefore = asn1TimeToString(X509_get0_notBefore(cert.get()));
    std::string notAfter = asn1TimeToString(X509_get0_notAfter(cert.get()));

    // Certificate fingerprint (SHA-256 of DER)
    unsigned char certDigest[32];
    unsigned int certDigestLen = 0;
    X509_digest(cert.get(), EVP_sha256(), certDigest, &certDigestLen);
    std::string certDigestHex;
    certDigestHex.reserve(64);
    for (unsigned int i = 0; i < certDigestLen; i++) {
        certDigestHex += hexChars[certDigest[i] >> 4];
        certDigestHex += hexChars[certDigest[i] & 0x0F];
    }

    // --- 5. Export certificate to DER ---
    int derLen = i2d_X509(cert.get(), nullptr);
    std::vector<uint8_t> certDer(derLen);
    unsigned char* derPtr = certDer.data();
    i2d_X509(cert.get(), &derPtr);

    // --- 6. Encrypt certificate PEM ---
    std::string certPemEncrypted = auth::pii::encrypt(certPem);

    // --- 7. Save to DB ---
    bool saved = csrRepo_->registerCertificate(
        id, certPemEncrypted, certDer,
        serial, certSubjectDn, certIssuerDn,
        notBefore, notAfter, certDigestHex,
        username
    );

    if (!saved) {
        result.errorMessage = "Failed to save certificate to database";
        logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, Json::nullValue, false, result.errorMessage);
        return result;
    }

    result.success = true;
    result.subjectDn = certSubjectDn;
    result.publicKeyFingerprint = certDigestHex;

    // Audit log — success
    Json::Value meta;
    meta["certSubjectDn"] = certSubjectDn;
    meta["certIssuerDn"] = certIssuerDn;
    meta["certSerial"] = serial;
    meta["certFingerprint"] = certDigestHex;
    meta["notBefore"] = notBefore;
    meta["notAfter"] = notAfter;
    logAudit(icao::audit::OperationType::CSR_GENERATE, username, id, meta);

    spdlog::info("[CsrService] Certificate registered for CSR: id={}, issuer={}, serial={}, expires={}",
        id, certIssuerDn, serial, notAfter);
    return result;
}

bool CsrService::deleteById(const std::string& id, const std::string& username)
{
    // Get info before deletion for audit
    Json::Value existing = csrRepo_->findById(id);
    std::string subjectDn = existing.isNull() ? "" : existing.get("subject_dn", "").asString();

    bool deleted = csrRepo_->deleteById(id);

    // Audit log — delete (success or failure)
    Json::Value meta;
    meta["subjectDn"] = subjectDn;
    logAudit(icao::audit::OperationType::CSR_DELETE, username, id, meta, deleted,
             deleted ? "" : "Delete failed");

    return deleted;
}

} // namespace services

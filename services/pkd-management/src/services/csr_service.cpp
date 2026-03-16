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
        return result;
    }

    if (EVP_PKEY_keygen_init(keyCtx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(keyCtx.get(), 2048) <= 0) {
        result.errorMessage = "Failed to initialize key generation";
        spdlog::error("[CsrService] {}", result.errorMessage);
        return result;
    }

    EVP_PKEY* rawKey = nullptr;
    if (EVP_PKEY_keygen(keyCtx.get(), &rawKey) <= 0) {
        result.errorMessage = "RSA-2048 key generation failed";
        spdlog::error("[CsrService] {}", result.errorMessage);
        return result;
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(rawKey, EVP_PKEY_free);

    // --- 2. Create X509_REQ (CSR) ---
    std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)> x509Req(X509_REQ_new(), X509_REQ_free);
    if (!x509Req) {
        result.errorMessage = "Failed to create X509_REQ";
        spdlog::error("[CsrService] {}", result.errorMessage);
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

    // Encrypt private key with PII encryption module
    std::string privateKeyEncrypted = auth::pii::encrypt(privateKeyPem);

    // --- 7. Save to DB ---
    std::string csrId = csrRepo_->save(
        result.subjectDn,
        req.countryCode,
        req.organization,
        req.commonName,
        "RSA-2048",
        "SHA256withRSA",
        result.csrPem,
        csrDer,
        fpHex,
        privateKeyEncrypted,
        req.memo,
        req.createdBy
    );

    if (csrId.empty()) {
        result.errorMessage = "Failed to save CSR to database";
        spdlog::error("[CsrService] {}", result.errorMessage);
        return result;
    }

    result.id = csrId;
    result.success = true;

    // Audit log
    try {
        icao::audit::AuditLogEntry entry;
        entry.operationType = icao::audit::OperationType::UNKNOWN;
        entry.operationSubtype = "CSR_GENERATE";
        entry.username = req.createdBy;
        entry.success = true;
        entry.resourceId = csrId;
        Json::Value meta;
        meta["subjectDn"] = result.subjectDn;
        meta["fingerprint"] = fpHex;
        entry.metadata = meta;
        icao::audit::logOperation(queryExecutor_, entry);
    } catch (...) {
        spdlog::warn("[CsrService] Audit log failed for CSR_GENERATE");
    }

    spdlog::info("[CsrService] CSR generated successfully: id={}, dn={}, fingerprint={}",
        csrId, result.subjectDn, fpHex);
    return result;
}

Json::Value CsrService::getById(const std::string& id)
{
    return csrRepo_->findById(id);
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

std::vector<uint8_t> CsrService::getDerById(const std::string& id)
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
        return der;
    } catch (const std::exception& e) {
        spdlog::error("[CsrService] getDerById failed: {}", e.what());
        return {};
    }
}

std::string CsrService::getPemById(const std::string& id)
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

        return result[0].get("csr_pem", "").asString();
    } catch (const std::exception& e) {
        spdlog::error("[CsrService] getPemById failed: {}", e.what());
        return "";
    }
}

bool CsrService::deleteById(const std::string& id)
{
    bool deleted = csrRepo_->deleteById(id);
    if (deleted) {
        try {
            icao::audit::AuditLogEntry entry;
            entry.operationType = icao::audit::OperationType::UNKNOWN;
            entry.operationSubtype = "CSR_DELETE";
            entry.success = true;
            entry.resourceId = id;
            icao::audit::logOperation(queryExecutor_, entry);
        } catch (...) {
            spdlog::warn("[CsrService] Audit log failed for CSR_DELETE");
        }
    }
    return deleted;
}

} // namespace services

/**
 * @file cvc_service.cpp
 * @brief CVC upload/preview business logic
 */

#include "services/cvc_service.h"
#include "repositories/cvc_certificate_repository.h"

#include <icao/cvc/cvc_parser.h>
#include <icao/cvc/cvc_signature.h>
#include <spdlog/spdlog.h>

namespace eac::services {

CvcService::CvcService(repositories::CvcCertificateRepository* repo) : repo_(repo) {}

std::optional<domain::CvcCertificateRecord> CvcService::uploadCvc(
    const std::vector<uint8_t>& binary, const std::string& sourceType) {

    auto cert = icao::cvc::CvcParser::parse(binary);
    if (!cert) {
        spdlog::warn("Failed to parse CVC certificate");
        return std::nullopt;
    }

    // Check for duplicate
    if (repo_->existsByFingerprint(cert->fingerprintSha256)) {
        spdlog::info("CVC already exists: {}", cert->fingerprintSha256);
        return std::nullopt;
    }

    // Self-signed verification for CVCA
    auto record = toRecord(*cert, sourceType);
    if (cert->type == icao::cvc::CvcType::CVCA) {
        auto result = icao::cvc::CvcSignatureVerifier::verifySelfSigned(*cert);
        record.signatureValid = result.valid;
        record.validationStatus = result.valid ? "VALID" : "INVALID";
        record.validationMessage = result.message;
    }

    if (!repo_->save(record)) {
        spdlog::error("Failed to save CVC certificate");
        return std::nullopt;
    }

    spdlog::info("CVC saved: {} ({}, {})", cert->chr, record.cvcType, cert->countryCode);

    // Fetch back from DB to get the DB-generated ID (SYS_GUID() on Oracle)
    auto saved = repo_->findByFingerprint(cert->fingerprintSha256);
    if (saved) return *saved;
    return record;
}

std::optional<icao::cvc::CvcCertificate> CvcService::previewCvc(const std::vector<uint8_t>& binary) {
    return icao::cvc::CvcParser::parse(binary);
}

Json::Value CvcService::cvcToJson(const icao::cvc::CvcCertificate& cert) {
    Json::Value j;
    j["cvcType"] = icao::cvc::cvcTypeToString(cert.type);
    j["countryCode"] = cert.countryCode;
    j["car"] = cert.car;
    j["chr"] = cert.chr;
    j["profileIdentifier"] = cert.profileIdentifier;
    j["effectiveDate"] = cert.effectiveDate;
    j["expirationDate"] = cert.expirationDate;
    j["fingerprintSha256"] = cert.fingerprintSha256;

    // Public key
    Json::Value pk;
    pk["algorithmOid"] = cert.publicKey.algorithmOid;
    pk["algorithmName"] = cert.publicKey.algorithmName;
    j["publicKey"] = pk;

    // CHAT
    Json::Value chat;
    chat["role"] = icao::cvc::chatRoleToString(cert.chat.role);
    chat["roleOid"] = cert.chat.roleOid;
    Json::Value perms(Json::arrayValue);
    for (const auto& p : cert.chat.permissions) {
        perms.append(p);
    }
    chat["permissions"] = perms;
    j["chat"] = chat;

    return j;
}

domain::CvcCertificateRecord CvcService::toRecord(
    const icao::cvc::CvcCertificate& cert, const std::string& sourceType) {

    domain::CvcCertificateRecord r;
    r.cvcType = icao::cvc::cvcTypeToString(cert.type);
    r.countryCode = cert.countryCode;
    r.car = cert.car;
    r.chr = cert.chr;
    r.chatOid = cert.chat.roleOid;
    r.chatRole = icao::cvc::chatRoleToString(cert.chat.role);

    // Permissions as JSON array
    Json::Value perms(Json::arrayValue);
    for (const auto& p : cert.chat.permissions) {
        perms.append(p);
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    r.chatPermissions = Json::writeString(wb, perms);

    r.publicKeyOid = cert.publicKey.algorithmOid;
    r.publicKeyAlgorithm = cert.publicKey.algorithmName;
    r.effectiveDate = cert.effectiveDate;
    r.expirationDate = cert.expirationDate;
    r.fingerprintSha256 = cert.fingerprintSha256;
    r.validationStatus = "PENDING";
    r.sourceType = sourceType;

    return r;
}

} // namespace eac::services

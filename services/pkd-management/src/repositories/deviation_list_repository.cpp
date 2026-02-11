#include "deviation_list_repository.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <stdexcept>

namespace repositories {

DeviationListRepository::DeviationListRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("DeviationListRepository: queryExecutor cannot be nullptr");
    }
    spdlog::debug("[DeviationListRepository] Initialized (DB type: {})", queryExecutor_->getDatabaseType());
}

std::string DeviationListRepository::save(const std::string& uploadId,
                                           const std::string& issuerCountry,
                                           int version,
                                           const std::string& hashAlgorithm,
                                           const std::string& signingTime,
                                           const std::vector<uint8_t>& dlBinary,
                                           const std::string& fingerprint,
                                           const std::string& signerDn,
                                           const std::string& signerCertificateId,
                                           bool signatureValid,
                                           int deviationCount) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string dlId = generateUuid();

        // Convert binary DL to hex string (\\x prefix for BLOB detection)
        std::ostringstream hexStream;
        hexStream << "\\\\x";
        for (size_t i = 0; i < dlBinary.size(); i++) {
            hexStream << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(dlBinary[i]);
        }
        std::string dlDataHex = hexStream.str();

        // Strip timezone for Oracle timestamps
        auto stripTz = [](const std::string& ts) -> std::string {
            if (ts.empty()) return "";
            return ts.length() > 19 ? ts.substr(0, 19) : ts;
        };

        std::string sigValidStr = signatureValid ? "1" : "0";
        std::string versionStr = std::to_string(version);
        std::string devCountStr = std::to_string(deviationCount);

        std::string query;
        std::vector<std::string> params;

        if (dbType == "oracle") {
            query =
                "INSERT INTO deviation_list ("
                "id, upload_id, issuer_country, version, hash_algorithm, "
                "signing_time, dl_binary, fingerprint_sha256, "
                "signer_dn, signer_certificate_id, signature_valid, "
                "deviation_count, created_at"
                ") VALUES ("
                "$1, $2, $3, TO_NUMBER($4), $5, "
                "CASE WHEN $6 IS NULL OR $6 = '' THEN NULL ELSE TO_TIMESTAMP($6, 'YYYY-MM-DD HH24:MI:SS') END, "
                "$7, $8, "
                "$9, CASE WHEN $10 = '' THEN NULL ELSE $10 END, TO_NUMBER($11), "
                "TO_NUMBER($12), SYSTIMESTAMP)";

            params = {
                dlId, uploadId, issuerCountry, versionStr, hashAlgorithm,
                signingTime.empty() ? "" : stripTz(signingTime),
                dlDataHex, fingerprint,
                signerDn, signerCertificateId, sigValidStr,
                devCountStr
            };
        } else {
            // PostgreSQL
            std::string pgSigValid = signatureValid ? "TRUE" : "FALSE";

            query =
                "INSERT INTO deviation_list ("
                "id, upload_id, issuer_country, version, hash_algorithm, "
                "signing_time, dl_binary, fingerprint_sha256, "
                "signer_dn, signer_certificate_id, signature_valid, "
                "deviation_count, created_at"
                ") VALUES ("
                "$1, $2, $3, $4, $5, "
                "CASE WHEN $6 = '' THEN NULL ELSE $6::TIMESTAMP WITH TIME ZONE END, "
                "$7, $8, "
                "$9, CASE WHEN $10 = '' THEN NULL ELSE $10::UUID END, " + pgSigValid + ", "
                "$11, NOW()) "
                "ON CONFLICT (fingerprint_sha256) DO NOTHING";

            params = {
                dlId, uploadId, issuerCountry, versionStr, hashAlgorithm,
                signingTime,
                dlDataHex, fingerprint,
                signerDn, signerCertificateId,
                devCountStr
            };
        }

        queryExecutor_->executeCommand(query, params);
        spdlog::info("[DeviationListRepository] Saved DL: id={}, country={}, deviations={}",
                    dlId.substr(0, 8), issuerCountry, deviationCount);
        return dlId;

    } catch (const std::exception& e) {
        spdlog::error("[DeviationListRepository] save failed: {}", e.what());
        return "";
    }
}

std::string DeviationListRepository::saveDeviationEntry(
    const std::string& deviationListId,
    const icao::certificate_parser::DeviationEntry& entry,
    const std::string& matchedCertificateId) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string entryId = generateUuid();

        // Convert parameters to hex if present
        std::string paramsHex;
        if (!entry.defectParameters.empty()) {
            std::ostringstream hexStream;
            hexStream << "\\\\x";
            for (auto b : entry.defectParameters) {
                hexStream << std::hex << std::setw(2) << std::setfill('0')
                         << static_cast<int>(b);
            }
            paramsHex = hexStream.str();
        }

        std::string query;
        std::vector<std::string> params;

        if (dbType == "oracle") {
            query =
                "INSERT INTO deviation_entry ("
                "id, deviation_list_id, certificate_issuer_dn, certificate_serial_number, "
                "matched_certificate_id, defect_description, defect_type_oid, "
                "defect_category, defect_parameters, created_at"
                ") VALUES ("
                "$1, $2, $3, $4, "
                "CASE WHEN $5 = '' THEN NULL ELSE $5 END, $6, $7, "
                "$8, CASE WHEN $9 = '' THEN NULL ELSE $9 END, SYSTIMESTAMP)";

            params = {
                entryId, deviationListId,
                entry.certificateIssuerDn, entry.certificateSerialNumber,
                matchedCertificateId,
                entry.defectDescription, entry.defectTypeOid,
                entry.defectCategory, paramsHex
            };
        } else {
            // PostgreSQL
            query =
                "INSERT INTO deviation_entry ("
                "id, deviation_list_id, certificate_issuer_dn, certificate_serial_number, "
                "matched_certificate_id, defect_description, defect_type_oid, "
                "defect_category, defect_parameters, created_at"
                ") VALUES ("
                "$1, $2::UUID, $3, $4, "
                "CASE WHEN $5 = '' THEN NULL ELSE $5::UUID END, $6, $7, "
                "$8, CASE WHEN $9 = '' THEN NULL ELSE $9::BYTEA END, NOW())";

            params = {
                entryId, deviationListId,
                entry.certificateIssuerDn, entry.certificateSerialNumber,
                matchedCertificateId,
                entry.defectDescription, entry.defectTypeOid,
                entry.defectCategory, paramsHex
            };
        }

        queryExecutor_->executeCommand(query, params);
        spdlog::debug("[DeviationListRepository] Saved deviation entry: id={}, oid={}, category={}",
                     entryId.substr(0, 8), entry.defectTypeOid, entry.defectCategory);
        return entryId;

    } catch (const std::exception& e) {
        spdlog::error("[DeviationListRepository] saveDeviationEntry failed: {}", e.what());
        return "";
    }
}

Json::Value DeviationListRepository::findById(const std::string& dlId) {
    try {
        auto result = queryExecutor_->executeQuery(
            "SELECT * FROM deviation_list WHERE id = $1", {dlId});
        if (result.empty()) return Json::nullValue;
        return result[0];
    } catch (const std::exception& e) {
        spdlog::error("[DeviationListRepository] findById failed: {}", e.what());
        return Json::nullValue;
    }
}

Json::Value DeviationListRepository::findByUploadId(const std::string& uploadId) {
    try {
        return queryExecutor_->executeQuery(
            "SELECT * FROM deviation_list WHERE upload_id = $1", {uploadId});
    } catch (const std::exception& e) {
        spdlog::error("[DeviationListRepository] findByUploadId failed: {}", e.what());
        return Json::arrayValue;
    }
}

Json::Value DeviationListRepository::findDeviationByCertificate(
    const std::string& issuerDn,
    const std::string& serialNumber) {
    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        std::string query;
        if (dbType == "oracle") {
            query =
                "SELECT de.*, dl.issuer_country, dl.version "
                "FROM deviation_entry de "
                "JOIN deviation_list dl ON de.deviation_list_id = dl.id "
                "WHERE de.certificate_issuer_dn = $1 AND de.certificate_serial_number = $2";
        } else {
            query =
                "SELECT de.*, dl.issuer_country, dl.version "
                "FROM deviation_entry de "
                "JOIN deviation_list dl ON de.deviation_list_id = dl.id "
                "WHERE de.certificate_issuer_dn = $1 AND de.certificate_serial_number = $2";
        }

        return queryExecutor_->executeQuery(query, {issuerDn, serialNumber});
    } catch (const std::exception& e) {
        spdlog::error("[DeviationListRepository] findDeviationByCertificate failed: {}", e.what());
        return Json::arrayValue;
    }
}

std::string DeviationListRepository::generateUuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);

    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (ab >> 32) << '-';
    ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << '-';
    ss << std::setw(4) << (ab & 0xFFFF) << '-';
    ss << std::setw(4) << (cd >> 48) << '-';
    ss << std::setw(12) << (cd & 0x0000FFFFFFFFFFFFULL);

    return ss.str();
}

} // namespace repositories

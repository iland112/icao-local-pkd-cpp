#pragma once

#include <string>
#include <vector>
#include <json/json.h>

/**
 * @file crl_parser.h
 * @brief Standalone CRL binary parser for report/detail views
 *
 * Parses hex-encoded CRL binary (from DB) using OpenSSL to extract:
 * - CRL metadata (issuer, dates, signature algorithm)
 * - Revoked certificate list (serial, date, reason)
 *
 * Works consistently across PostgreSQL and Oracle (no revoked_certificate table dependency).
 *
 * @date 2026-02-18
 */

namespace crl {

struct RevokedCertificateInfo {
    std::string serialNumber;
    std::string revocationDate;
    std::string revocationReason;
};

struct CrlParsedInfo {
    std::string issuerDn;
    std::string thisUpdate;
    std::string nextUpdate;
    std::string signatureAlgorithm;
    int revokedCount = 0;
    std::vector<RevokedCertificateInfo> revokedCertificates;
    bool parsed = false;
};

/**
 * @brief Parse CRL binary and extract full info including revoked certificates
 * @param crlHex Hex-encoded CRL binary (with optional \\x prefix)
 * @return Parsed CRL info
 */
CrlParsedInfo parseCrlBinary(const std::string& crlHex);

/**
 * @brief Get just the revoked certificate count (lightweight)
 * @param crlHex Hex-encoded CRL binary
 * @return Number of revoked certificates, or -1 on parse failure
 */
int getRevokedCount(const std::string& crlHex);

} // namespace crl

/**
 * @file update_x509_metadata.cpp
 * @brief Update X.509 metadata for existing certificates in database
 *
 * This utility reads existing certificates from the database,
 * extracts X.509 metadata, and updates the certificate table.
 *
 * Usage:
 *   ./update_x509_metadata [--limit N] [--batch-size N]
 *
 * @date 2026-01-30
 */

#include <iostream>
#include <vector>
#include <string>
#include <libpq-fe.h>
#include <openssl/x509.h>
#include <spdlog/spdlog.h>
#include "../src/common/x509_metadata_extractor.h"

// Database connection string
std::string getConnString() {
    const char* host = std::getenv("DB_HOST");
    const char* port = std::getenv("DB_PORT");
    const char* dbname = std::getenv("DB_NAME");
    const char* user = std::getenv("DB_USER");
    const char* password = std::getenv("DB_PASSWORD");

    return "host=" + std::string(host ? host : "postgres") +
           " port=" + std::string(port ? port : "5432") +
           " dbname=" + std::string(dbname ? dbname : "localpkd") +
           " user=" + std::string(user ? user : "pkd") +
           " password=" + std::string(password ? password : "pkd_test_password_123");
}

// Convert vector to PostgreSQL array literal
std::string vecToArray(const std::vector<std::string>& vec) {
    if (vec.empty()) return "NULL";
    std::string result = "ARRAY[";
    for (size_t i = 0; i < vec.size(); i++) {
        result += "'" + vec[i] + "'";
        if (i < vec.size() - 1) result += ",";
    }
    result += "]";
    return result;
}

// Escape string for SQL
std::string escapeString(PGconn* conn, const std::string& str) {
    char* escaped = PQescapeLiteral(conn, str.c_str(), str.length());
    if (!escaped) return "NULL";
    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

// Optional string to SQL
std::string optToSql(PGconn* conn, const std::optional<std::string>& opt) {
    return opt.has_value() ? escapeString(conn, opt.value()) : "NULL";
}

// Optional int to SQL
std::string optIntToSql(const std::optional<int>& opt) {
    return opt.has_value() ? std::to_string(opt.value()) : "NULL";
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("X.509 Metadata Update Utility");

    // Parse command line arguments
    int limit = 0;  // 0 = unlimited
    int batchSize = 100;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--limit" && i + 1 < argc) {
            limit = std::atoi(argv[++i]);
        } else if (arg == "--batch-size" && i + 1 < argc) {
            batchSize = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [--limit N] [--batch-size N]\n";
            std::cout << "  --limit N       Process only N certificates (0 = unlimited)\n";
            std::cout << "  --batch-size N  Commit every N certificates (default: 100)\n";
            return 0;
        }
    }

    // Connect to database
    std::string connStr = getConnString();
    PGconn* conn = PQconnectdb(connStr.c_str());

    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("Database connection failed: {}", PQerrorMessage(conn));
        PQfinish(conn);
        return 1;
    }

    spdlog::info("Connected to database: {}", PQdb(conn));

    // Query certificates with NULL metadata
    std::string query =
        "SELECT id, fingerprint_sha256, certificate_data "
        "FROM certificate "
        "WHERE signature_algorithm IS NULL "
        "ORDER BY created_at DESC";

    if (limit > 0) {
        query += " LIMIT " + std::to_string(limit);
    }

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Query failed: {}", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return 1;
    }

    int totalCerts = PQntuples(res);
    spdlog::info("Found {} certificates with missing metadata", totalCerts);

    int processed = 0;
    int updated = 0;
    int errors = 0;

    // Process certificates
    for (int i = 0; i < totalCerts; i++) {
        std::string certId = PQgetvalue(res, i, 0);
        std::string fingerprint = PQgetvalue(res, i, 1);
        const char* certDataHex = PQgetvalue(res, i, 2);
        int certDataLen = PQgetlength(res, i, 2);

        processed++;

        // Parse bytea hex format (\x...)
        std::vector<uint8_t> derBytes;
        if (certDataLen > 2 && certDataHex[0] == '\\' && certDataHex[1] == 'x') {
            for (int j = 2; j < certDataLen; j += 2) {
                if (j + 1 < certDataLen) {
                    char hex[3] = {certDataHex[j], certDataHex[j + 1], 0};
                    derBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
                }
            }
        }

        if (derBytes.empty()) {
            spdlog::warn("[{}/{}] Failed to parse certificate data: {}",
                processed, totalCerts, fingerprint);
            errors++;
            continue;
        }

        // Parse X509 certificate
        const uint8_t* data = derBytes.data();
        X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));

        if (!cert) {
            spdlog::warn("[{}/{}] Failed to parse X509 certificate: {}",
                processed, totalCerts, fingerprint);
            errors++;
            continue;
        }

        // Extract metadata
        x509::CertificateMetadata metadata = x509::extractMetadata(cert);
        X509_free(cert);

        // Build UPDATE query
        std::string updateQuery =
            "UPDATE certificate SET "
            "version = " + std::to_string(metadata.version) + ", "
            "signature_algorithm = " + escapeString(conn, metadata.signatureAlgorithm) + ", "
            "signature_hash_algorithm = " + escapeString(conn, metadata.signatureHashAlgorithm) + ", "
            "public_key_algorithm = " + escapeString(conn, metadata.publicKeyAlgorithm) + ", "
            "public_key_size = " + std::to_string(metadata.publicKeySize) + ", "
            "public_key_curve = " + optToSql(conn, metadata.publicKeyCurve) + ", "
            "key_usage = " + vecToArray(metadata.keyUsage) + ", "
            "extended_key_usage = " + vecToArray(metadata.extendedKeyUsage) + ", "
            "is_ca = " + (metadata.isCA ? "TRUE" : "FALSE") + ", "
            "path_len_constraint = " + optIntToSql(metadata.pathLenConstraint) + ", "
            "subject_key_identifier = " + optToSql(conn, metadata.subjectKeyIdentifier) + ", "
            "authority_key_identifier = " + optToSql(conn, metadata.authorityKeyIdentifier) + ", "
            "crl_distribution_points = " + vecToArray(metadata.crlDistributionPoints) + ", "
            "ocsp_responder_url = " + optToSql(conn, metadata.ocspResponderUrl) + ", "
            "is_self_signed = " + (metadata.isSelfSigned ? "TRUE" : "FALSE") + " "
            "WHERE id = '" + certId + "'";

        PGresult* updateRes = PQexec(conn, updateQuery.c_str());
        if (PQresultStatus(updateRes) != PGRES_COMMAND_OK) {
            spdlog::error("[{}/{}] Update failed for {}: {}",
                processed, totalCerts, fingerprint, PQerrorMessage(conn));
            errors++;
        } else {
            updated++;
            if (processed % 100 == 0) {
                spdlog::info("[{}/{}] Updated {} certificates (errors: {})",
                    processed, totalCerts, updated, errors);
            }
        }
        PQclear(updateRes);

        // Commit every batch
        if (processed % batchSize == 0) {
            PQexec(conn, "COMMIT");
            PQexec(conn, "BEGIN");
        }
    }

    PQclear(res);
    PQexec(conn, "COMMIT");

    // Summary
    spdlog::info("=== Update Complete ===");
    spdlog::info("Total processed: {}", processed);
    spdlog::info("Successfully updated: {}", updated);
    spdlog::info("Errors: {}", errors);

    PQfinish(conn);
    return (errors > 0) ? 1 : 0;
}

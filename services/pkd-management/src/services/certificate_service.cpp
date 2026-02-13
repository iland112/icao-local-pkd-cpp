/**
 * @file certificate_service.cpp
 * @brief Application Service Implementation - Certificate Business Logic
 */

#include "certificate_service.h"
#include <spdlog/spdlog.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <zip.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace services {

// ============================================================
// Constructor
// ============================================================

CertificateService::CertificateService(
    std::shared_ptr<repositories::ICertificateRepository> repository
) : repository_(repository) {
    if (!repository_) {
        throw std::invalid_argument("Repository cannot be null");
    }
}

// ============================================================
// Public Use Cases
// ============================================================

domain::models::CertificateSearchResult CertificateService::searchCertificates(
    const domain::models::CertificateSearchCriteria& criteria
) {
    spdlog::info("Searching certificates - Country: {}, Type: {}, Limit: {}",
                 criteria.country.value_or("ALL"),
                 criteria.certType.has_value() ? "FILTERED" : "ALL",
                 criteria.limit);

    auto result = repository_->search(criteria);

    // Apply validity filter if specified (post-filter after LDAP search)
    if (criteria.validity.has_value()) {
        std::vector<domain::models::Certificate> filtered;
        for (const auto& cert : result.certificates) {
            if (cert.getValidityStatus() == criteria.validity.value()) {
                filtered.push_back(cert);
            }
        }
        result.certificates = std::move(filtered);
        spdlog::debug("Applied validity filter, remaining: {}", result.certificates.size());
    }

    return result;
}

domain::models::Certificate CertificateService::getCertificateDetail(
    const std::string& dn
) {
    spdlog::info("Getting certificate detail for DN: {}", dn);
    return repository_->getByDn(dn);
}

ExportResult CertificateService::exportCertificateFile(
    const std::string& dn,
    ExportFormat format
) {
    spdlog::info("Exporting certificate - DN: {}, Format: {}",
                 dn, format == ExportFormat::PEM ? "PEM" : "DER");

    ExportResult result;
    result.success = false;

    try {
        // Get certificate binary data
        std::vector<uint8_t> derData = repository_->getCertificateBinary(dn);

        // Convert to PEM if requested
        if (format == ExportFormat::PEM) {
            domain::models::Certificate cert = repository_->getByDn(dn);
            result.data = convertDerToPem(derData, cert.getCertType());
        } else {
            result.data = derData;
        }

        // Generate filename
        result.filename = generateFilenameFromDn(dn, format);
        result.contentType = getContentType(format, false);
        result.success = true;

        spdlog::info("Certificate exported successfully - Size: {} bytes", result.data.size());
    } catch (const std::exception& e) {
        result.errorMessage = e.what();
        spdlog::error("Failed to export certificate: {}", e.what());
    }

    return result;
}

ExportResult CertificateService::exportCountryCertificates(
    const std::string& country,
    ExportFormat format
) {
    spdlog::info("Exporting country certificates - Country: {}, Format: {}",
                 country, format == ExportFormat::PEM ? "PEM" : "DER");

    ExportResult result;
    result.success = false;

    try {
        // Get all DNs for the country
        std::vector<std::string> dns = repository_->getDnsByCountryAndType(
            country,
            std::nullopt  // All types
        );

        if (dns.empty()) {
            result.errorMessage = "No certificates found for country: " + country;
            spdlog::warn(result.errorMessage);
            return result;
        }

        spdlog::info("Found {} certificates for country {}", dns.size(), country);

        // Create ZIP archive
        result.data = createZipArchive(dns, format);
        result.filename = country + "_certificates.zip";
        result.contentType = getContentType(format, true);
        result.success = true;

        spdlog::info("Country export completed - ZIP size: {} bytes", result.data.size());
    } catch (const std::exception& e) {
        result.errorMessage = e.what();
        spdlog::error("Failed to export country certificates: {}", e.what());
    }

    return result;
}

// ============================================================
// Private Helper Methods
// ============================================================

std::vector<uint8_t> CertificateService::convertDerToPem(
    const std::vector<uint8_t>& derData,
    domain::models::CertificateType certType
) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        throw std::runtime_error("Failed to create BIO for PEM conversion");
    }

    const unsigned char* data = derData.data();
    bool success = false;

    if (certType == domain::models::CertificateType::CRL) {
        // Parse as CRL
        X509_CRL* crl = d2i_X509_CRL(nullptr, &data, derData.size());
        if (crl) {
            success = PEM_write_bio_X509_CRL(bio, crl);
            X509_CRL_free(crl);
        }
    } else {
        // Parse as X.509 certificate (CSCA, DSC, DSC_NC, ML)
        X509* x509 = d2i_X509(nullptr, &data, derData.size());
        if (x509) {
            success = PEM_write_bio_X509(bio, x509);
            X509_free(x509);
        }
    }

    if (!success) {
        BIO_free(bio);
        throw std::runtime_error("Failed to convert DER to PEM");
    }

    // Read PEM data from BIO
    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);

    std::vector<uint8_t> pemData(
        reinterpret_cast<const uint8_t*>(mem->data),
        reinterpret_cast<const uint8_t*>(mem->data) + mem->length
    );

    BIO_free(bio);
    return pemData;
}

std::string CertificateService::generateCertificateFilename(
    const domain::models::Certificate& cert,
    ExportFormat format
) {
    std::ostringstream filename;

    // Format: {COUNTRY}_{TYPE}_{SERIAL}.{ext}
    filename << cert.getCountry() << "_"
             << cert.getCertTypeString() << "_";

    // Truncate serial number if too long
    std::string sn = cert.getSn();
    if (sn.length() > 16) {
        sn = sn.substr(0, 16);
    }
    filename << sn;

    // Add extension
    filename << getFileExtension(format, cert.getCertType());

    return filename.str();
}

std::string CertificateService::generateFilenameFromDn(
    const std::string& dn,
    ExportFormat format
) {
    try {
        domain::models::Certificate cert = repository_->getByDn(dn);
        return generateCertificateFilename(cert, format);
    } catch (...) {
        // Fallback: use DN-based naming
        std::string safeDn = dn;
        std::replace(safeDn.begin(), safeDn.end(), ',', '_');
        std::replace(safeDn.begin(), safeDn.end(), '=', '_');
        return safeDn.substr(0, 64) + (format == ExportFormat::PEM ? ".pem" : ".der");
    }
}

std::vector<uint8_t> CertificateService::createZipArchive(
    const std::vector<std::string>& dns,
    ExportFormat format
) {
    // Use temporary file for ZIP creation (more reliable than buffer source)
    char tmpFilename[] = "/tmp/icao-export-XXXXXX";
    int tmpFd = mkstemp(tmpFilename);
    if (tmpFd == -1) {
        throw std::runtime_error("Failed to create temporary file");
    }
    close(tmpFd);  // libzip will reopen it

    // Create ZIP archive in temporary file
    int error = 0;
    zip_t* archive = zip_open(tmpFilename, ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!archive) {
        unlink(tmpFilename);
        throw std::runtime_error("Failed to create ZIP archive: " + std::to_string(error));
    }

    // Add each certificate to ZIP
    int addedCount = 0;
    for (const auto& dn : dns) {
        try {
            std::vector<uint8_t> certData = repository_->getCertificateBinary(dn);

            // Convert to PEM if requested
            if (format == ExportFormat::PEM) {
                domain::models::Certificate cert = repository_->getByDn(dn);
                certData = convertDerToPem(certData, cert.getCertType());
            }

            // Generate filename
            std::string filename = generateFilenameFromDn(dn, format);

            // Copy data to heap (ZIP will own and free it)
            void* bufferCopy = malloc(certData.size());
            if (!bufferCopy) {
                spdlog::warn("Failed to allocate memory for: {}", filename);
                continue;
            }
            memcpy(bufferCopy, certData.data(), certData.size());

            // Add to ZIP with ownership transfer
            zip_source_t* fileSrc = zip_source_buffer(
                archive,
                bufferCopy,
                certData.size(),
                1  // Free buffer when done
            );

            if (fileSrc) {
                zip_int64_t index = zip_file_add(
                    archive,
                    filename.c_str(),
                    fileSrc,
                    ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8
                );

                if (index >= 0) {
                    addedCount++;
                } else {
                    zip_source_free(fileSrc);
                    spdlog::warn("Failed to add file to ZIP: {}", filename);
                }
            } else {
                free(bufferCopy);  // Free if source creation failed
            }
        } catch (const std::exception& e) {
            spdlog::warn("Skipping certificate due to error: {} - {}", dn, e.what());
        }
    }

    if (addedCount == 0) {
        zip_discard(archive);
        unlink(tmpFilename);
        throw std::runtime_error("No certificates added to ZIP archive");
    }

    // Close archive to finalize ZIP data
    if (zip_close(archive) != 0) {
        zip_discard(archive);
        unlink(tmpFilename);
        throw std::runtime_error("Failed to close ZIP archive");
    }

    // Read ZIP file into memory
    FILE* f = fopen(tmpFilename, "rb");
    if (!f) {
        unlink(tmpFilename);
        throw std::runtime_error("Failed to open temporary ZIP file");
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read entire file
    std::vector<uint8_t> zipData(fileSize);
    size_t bytesRead = fread(zipData.data(), 1, fileSize, f);
    fclose(f);

    // Clean up temporary file
    unlink(tmpFilename);

    if (bytesRead != static_cast<size_t>(fileSize)) {
        throw std::runtime_error("Failed to read complete ZIP data");
    }

    spdlog::info("ZIP archive created - {} certificates added, {} bytes", addedCount, zipData.size());
    return zipData;
}

std::string CertificateService::getContentType(ExportFormat format, bool isZip) {
    if (isZip) {
        return "application/zip";
    }

    switch (format) {
        case ExportFormat::DER:
            return "application/x-x509-ca-cert";
        case ExportFormat::PEM:
            return "application/x-pem-file";
        default:
            return "application/octet-stream";
    }
}

std::string CertificateService::getFileExtension(
    ExportFormat format,
    domain::models::CertificateType certType
) {
    if (format == ExportFormat::PEM) {
        return ".pem";
    }

    // DER format extensions
    switch (certType) {
        case domain::models::CertificateType::CSCA:
        case domain::models::CertificateType::DSC:
        case domain::models::CertificateType::DSC_NC:
        case domain::models::CertificateType::ML:
            return ".crt";
        case domain::models::CertificateType::CRL:
            return ".crl";
        default:
            return ".der";
    }
}

// ============================================================
// Free Function: Export All LDAP-Stored Data as DIT ZIP
// ============================================================

namespace {

// Sanitize string for filesystem-safe filename
std::string sanitizeForFilename(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            result += c;
        } else if (c == ' ' || c == '/' || c == '\\' || c == ',' || c == '=') {
            result += '_';
        }
    }
    // Truncate to reasonable length
    if (result.size() > 60) result.resize(60);
    return result;
}

// Extract CN from subject DN (supports both /C=xx/CN=name and CN=name,C=xx formats)
std::string extractCnFromDn(const std::string& dn) {
    // Try /CN= format first
    size_t pos = dn.find("/CN=");
    if (pos != std::string::npos) {
        pos += 4;
        size_t end = dn.find('/', pos);
        return (end != std::string::npos) ? dn.substr(pos, end - pos) : dn.substr(pos);
    }
    // Try CN= at beginning or after comma
    pos = dn.find("CN=");
    if (pos != std::string::npos) {
        pos += 3;
        size_t end = dn.find(',', pos);
        return (end != std::string::npos) ? dn.substr(pos, end - pos) : dn.substr(pos);
    }
    return "";
}

// Decode hex string to bytes
std::vector<uint8_t> decodeHexString(const std::string& hexStr) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hexStr.size() / 2);
    for (size_t i = 0; i + 1 < hexStr.size(); i += 2) {
        char h[3] = {hexStr[i], hexStr[i + 1], 0};
        bytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
    }
    return bytes;
}

// Parse hex-encoded bytea to DER binary
// Handles double-encoded data: BYTEA contains text "\\x3082..." stored as bytes
// PostgreSQL returns: \x5c7833303832... (hex of the text bytes)
std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::string cleanHex = hex;

    // Strip \x prefix from PostgreSQL hex format
    if (cleanHex.size() >= 2 && cleanHex[0] == '\\' && cleanHex[1] == 'x') {
        cleanHex = cleanHex.substr(2);
    }

    // First decode: hex string → bytes
    std::vector<uint8_t> firstPass = decodeHexString(cleanHex);

    // Check if result is still hex-encoded text (starts with \x or 0x30)
    if (firstPass.size() >= 2 && firstPass[0] == '\\' && firstPass[1] == 'x') {
        // Double-encoded: the bytes are ASCII text "\\x3082..."
        // Convert bytes back to string, strip \\x prefix, decode again
        std::string innerHex(firstPass.begin() + 2, firstPass.end());
        return decodeHexString(innerHex);
    }

    // Check if first byte is a valid ASN.1 tag (0x30 = SEQUENCE)
    if (!firstPass.empty() && firstPass[0] == 0x30) {
        return firstPass; // Already proper DER binary
    }

    return firstPass;
}

// Convert DER cert to PEM
std::vector<uint8_t> derCertToPem(const std::vector<uint8_t>& derData) {
    const unsigned char* data = derData.data();
    X509* x509 = d2i_X509(nullptr, &data, derData.size());
    if (!x509) return derData; // Fallback: return DER as-is

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, x509);
    X509_free(x509);

    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    std::vector<uint8_t> pem(
        reinterpret_cast<const uint8_t*>(mem->data),
        reinterpret_cast<const uint8_t*>(mem->data) + mem->length
    );
    BIO_free(bio);
    return pem;
}

// Convert DER CRL to PEM
std::vector<uint8_t> derCrlToPem(const std::vector<uint8_t>& derData) {
    const unsigned char* data = derData.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, derData.size());
    if (!crl) return derData;

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509_CRL(bio, crl);
    X509_CRL_free(crl);

    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    std::vector<uint8_t> pem(
        reinterpret_cast<const uint8_t*>(mem->data),
        reinterpret_cast<const uint8_t*>(mem->data) + mem->length
    );
    BIO_free(bio);
    return pem;
}

// Add binary data to ZIP archive
bool addToZip(zip_t* archive, const std::string& path, const std::vector<uint8_t>& data) {
    void* buf = malloc(data.size());
    if (!buf) return false;
    memcpy(buf, data.data(), data.size());

    zip_source_t* src = zip_source_buffer(archive, buf, data.size(), 1);
    if (!src) {
        free(buf);
        return false;
    }

    zip_int64_t idx = zip_file_add(archive, path.c_str(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_source_free(src);
        return false;
    }
    return true;
}

} // anonymous namespace

ExportResult exportAllCertificatesFromDb(
    repositories::CertificateRepository* certRepo,
    repositories::CrlRepository* crlRepo,
    common::IQueryExecutor* queryExecutor,
    ExportFormat format
) {
    ExportResult result;
    result.success = false;

    if (!certRepo || !crlRepo || !queryExecutor) {
        result.errorMessage = "Missing repository dependencies";
        return result;
    }

    spdlog::info("Starting full PKD export (format={})", format == ExportFormat::PEM ? "PEM" : "DER");

    try {
        // Create temporary ZIP file
        char tmpFilename[] = "/tmp/icao-pkd-export-XXXXXX";
        int tmpFd = mkstemp(tmpFilename);
        if (tmpFd == -1) {
            result.errorMessage = "Failed to create temporary file";
            return result;
        }
        close(tmpFd);

        int error = 0;
        zip_t* archive = zip_open(tmpFilename, ZIP_CREATE | ZIP_TRUNCATE, &error);
        if (!archive) {
            unlink(tmpFilename);
            result.errorMessage = "Failed to create ZIP archive";
            return result;
        }

        int addedCount = 0;

        // ---- 1. Certificates (CSCA, DSC, MLSC, DSC_NC) ----
        Json::Value certs = certRepo->findAllForExport();
        spdlog::info("Export: {} certificates to process", certs.size());

        for (const auto& row : certs) {
            try {
                std::string certType = row.get("certificate_type", "").asString();
                std::string country = row.get("country_code", "").asString();
                std::string subjectDn = row.get("subject_dn", "").asString();
                std::string fingerprint = row.get("fingerprint_sha256", "").asString();
                std::string certDataHex = row.get("certificate_data", "").asString();

                if (certDataHex.empty() || country.empty()) continue;

                // Parse hex to binary
                std::vector<uint8_t> derData = hexToBytes(certDataHex);
                if (derData.empty()) continue;

                // Determine folder path based on cert type
                std::string folder;
                if (certType == "DSC_NC") {
                    folder = "nc-data/" + country + "/dsc/";
                } else {
                    std::string typeFolder = "csca";
                    if (certType == "DSC") typeFolder = "dsc";
                    else if (certType == "MLSC") typeFolder = "mlsc";
                    else if (certType == "CSCA") typeFolder = "csca";
                    folder = "data/" + country + "/" + typeFolder + "/";
                }

                // Generate filename: CN_fingerprint8.ext
                std::string cn = extractCnFromDn(subjectDn);
                std::string safeName = cn.empty() ? certType : sanitizeForFilename(cn);
                std::string fp8 = fingerprint.substr(0, 8);
                std::string ext = (format == ExportFormat::PEM) ? ".pem" : ".der";
                std::string filePath = folder + safeName + "_" + fp8 + ext;

                // Convert to PEM if requested
                std::vector<uint8_t> fileData = (format == ExportFormat::PEM) ? derCertToPem(derData) : derData;

                if (addToZip(archive, filePath, fileData)) {
                    addedCount++;
                }
            } catch (const std::exception& e) {
                spdlog::warn("Export: skipping cert: {}", e.what());
            }
        }

        spdlog::info("Export: {} certificates added to ZIP", addedCount);

        // ---- 2. CRLs ----
        Json::Value crls = crlRepo->findAllForExport();
        int crlCount = 0;
        spdlog::info("Export: {} CRLs to process", crls.size());

        for (const auto& row : crls) {
            try {
                std::string country = row.get("country_code", "").asString();
                std::string crlDataHex = row.get("crl_binary", "").asString();
                std::string fingerprint = row.get("fingerprint_sha256", "").asString();

                if (crlDataHex.empty() || country.empty()) continue;

                std::vector<uint8_t> derData = hexToBytes(crlDataHex);
                if (derData.empty()) continue;

                std::string fp8 = fingerprint.substr(0, 8);
                std::string ext = (format == ExportFormat::PEM) ? ".pem" : ".crl";
                std::string filePath = "data/" + country + "/crl/" + country + "_crl_" + fp8 + ext;

                std::vector<uint8_t> fileData = (format == ExportFormat::PEM) ? derCrlToPem(derData) : derData;

                if (addToZip(archive, filePath, fileData)) {
                    crlCount++;
                    addedCount++;
                }
            } catch (const std::exception& e) {
                spdlog::warn("Export: skipping CRL: {}", e.what());
            }
        }

        spdlog::info("Export: {} CRLs added to ZIP", crlCount);

        // ---- 3. Master Lists ----
        std::string dbType = queryExecutor->getDatabaseType();
        std::string storedFlag = (dbType == "oracle") ? "1" : "TRUE";
        std::string mlQuery =
            "SELECT signer_country, ml_binary, fingerprint_sha256 "
            "FROM master_list WHERE stored_in_ldap = " + storedFlag + " "
            "ORDER BY signer_country";

        Json::Value mls = queryExecutor->executeQuery(mlQuery);
        int mlCount = 0;
        spdlog::info("Export: {} Master Lists to process", mls.size());

        for (const auto& row : mls) {
            try {
                std::string country = row.get("signer_country", "").asString();
                std::string mlDataHex = row.get("ml_binary", "").asString();
                std::string fingerprint = row.get("fingerprint_sha256", "").asString();

                if (mlDataHex.empty() || country.empty()) continue;

                std::vector<uint8_t> binaryData = hexToBytes(mlDataHex);
                if (binaryData.empty()) continue;

                std::string fp8 = fingerprint.substr(0, 8);
                // Master Lists are always CMS SignedData binary - no PEM conversion
                std::string filePath = "data/" + country + "/ml/" + country + "_ml_" + fp8 + ".cms";

                if (addToZip(archive, filePath, binaryData)) {
                    mlCount++;
                    addedCount++;
                }
            } catch (const std::exception& e) {
                spdlog::warn("Export: skipping ML: {}", e.what());
            }
        }

        spdlog::info("Export: {} Master Lists added to ZIP", mlCount);

        // ---- Finalize ZIP ----
        if (addedCount == 0) {
            zip_discard(archive);
            unlink(tmpFilename);
            result.errorMessage = "No data found for export";
            return result;
        }

        if (zip_close(archive) != 0) {
            zip_discard(archive);
            unlink(tmpFilename);
            result.errorMessage = "Failed to finalize ZIP archive";
            return result;
        }

        // Read ZIP into memory
        FILE* f = fopen(tmpFilename, "rb");
        if (!f) {
            unlink(tmpFilename);
            result.errorMessage = "Failed to read ZIP file";
            return result;
        }

        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        result.data.resize(fileSize);
        size_t bytesRead = fread(result.data.data(), 1, fileSize, f);
        fclose(f);
        unlink(tmpFilename);

        if (bytesRead != static_cast<size_t>(fileSize)) {
            result.errorMessage = "Failed to read complete ZIP data";
            return result;
        }

        // Generate filename with timestamp
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        std::ostringstream ts;
        ts << std::put_time(tm, "%Y%m%d-%H%M%S");

        result.filename = "ICAO-PKD-Export-" + ts.str() + ".zip";
        result.contentType = "application/zip";
        result.success = true;

        spdlog::info("Full PKD export completed: {} files ({} certs, {} CRLs, {} MLs), ZIP size: {} bytes",
                     addedCount, addedCount - crlCount - mlCount, crlCount, mlCount, result.data.size());

    } catch (const std::exception& e) {
        result.errorMessage = e.what();
        spdlog::error("Full PKD export failed: {}", e.what());
    }

    return result;
}

} // namespace services

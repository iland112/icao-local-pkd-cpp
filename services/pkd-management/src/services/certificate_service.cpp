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

} // namespace services

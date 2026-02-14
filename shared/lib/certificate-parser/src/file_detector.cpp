/**
 * @file file_detector.cpp
 * @brief Implementation of certificate file format detector
 */

#include "file_detector.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace icao {
namespace certificate_parser {

FileFormat FileDetector::detectFormat(
    const std::string& filename,
    const std::vector<uint8_t>& content
) {
    // Strategy 1: Try extension-based detection first (fast)
    FileFormat format = detectByExtension(filename);
    if (format != FileFormat::UNKNOWN) {
        return format;
    }

    // Strategy 2: Fall back to content-based detection
    return detectByContent(content);
}

std::string FileDetector::formatToString(FileFormat format) {
    switch (format) {
        case FileFormat::PEM:    return "PEM";
        case FileFormat::DER:    return "DER";
        case FileFormat::CER:    return "CER";
        case FileFormat::BIN:    return "BIN";
        case FileFormat::DL:     return "DL";
        case FileFormat::LDIF:   return "LDIF";
        case FileFormat::ML:     return "ML";
        case FileFormat::P7B:    return "P7B";
        case FileFormat::CRL:    return "CRL";
        case FileFormat::UNKNOWN:
        default:                 return "UNKNOWN";
    }
}

FileFormat FileDetector::stringToFormat(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    if (upper == "PEM")    return FileFormat::PEM;
    if (upper == "DER")    return FileFormat::DER;
    if (upper == "CER")    return FileFormat::CER;
    if (upper == "BIN")    return FileFormat::BIN;
    if (upper == "DL")     return FileFormat::DL;
    if (upper == "LDIF")   return FileFormat::LDIF;
    if (upper == "ML")     return FileFormat::ML;
    if (upper == "P7B")    return FileFormat::P7B;
    if (upper == "CRL")    return FileFormat::CRL;

    return FileFormat::UNKNOWN;
}

FileFormat FileDetector::detectByExtension(const std::string& filename) {
    std::string ext = getExtension(filename);

    if (ext == ".pem" || ext == ".crt") {
        return FileFormat::PEM;
    }
    if (ext == ".der") {
        return FileFormat::DER;
    }
    if (ext == ".cer") {
        return FileFormat::CER;
    }
    if (ext == ".bin") {
        return FileFormat::BIN;
    }
    if (ext == ".dvl" || ext == ".dl") {
        return FileFormat::DL;
    }
    if (ext == ".ldif") {
        return FileFormat::LDIF;
    }
    if (ext == ".ml") {
        return FileFormat::ML;
    }
    if (ext == ".p7b" || ext == ".p7c") {
        return FileFormat::P7B;
    }
    if (ext == ".crl") {
        return FileFormat::CRL;
    }

    return FileFormat::UNKNOWN;
}

FileFormat FileDetector::detectByContent(const std::vector<uint8_t>& content) {
    if (content.empty()) {
        return FileFormat::UNKNOWN;
    }

    // Check PEM (text-based, starts with "-----BEGIN")
    if (isPEM(content)) {
        return FileFormat::PEM;
    }

    // Check LDIF (text-based, starts with "dn:" or "version:")
    if (isLDIF(content)) {
        return FileFormat::LDIF;
    }

    // Check CRL (PEM or DER) - before generic DER check
    if (isCRL(content)) {
        return FileFormat::CRL;
    }

    // Check Document List / Deviation List (PKCS#7 with DL OID)
    if (isDL(content)) {
        return FileFormat::DL;
    }

    // Check Master List (PKCS#7 with ML OID)
    if (isMasterList(content)) {
        return FileFormat::ML;
    }

    // Check generic PKCS#7 bundle (no ICAO OID)
    if (isP7B(content)) {
        return FileFormat::P7B;
    }

    // Check DER (binary ASN.1)
    if (isDER(content)) {
        // Could be DER, CER, or BIN - default to DER
        return FileFormat::DER;
    }

    return FileFormat::UNKNOWN;
}

bool FileDetector::isPEM(const std::vector<uint8_t>& content) {
    if (content.size() < 11) {  // "-----BEGIN " = 11 chars
        return false;
    }

    // Check for PEM header
    const char* pemHeader = "-----BEGIN ";
    return std::memcmp(content.data(), pemHeader, 11) == 0;
}

bool FileDetector::isDER(const std::vector<uint8_t>& content) {
    if (content.size() < 2) {
        return false;
    }

    // DER-encoded ASN.1 starts with SEQUENCE tag (0x30)
    if (content[0] != 0x30) {
        return false;
    }

    // Check length encoding (0x81, 0x82, 0x83, 0x84 for long form)
    uint8_t lengthByte = content[1];
    if (lengthByte == 0x81 || lengthByte == 0x82 ||
        lengthByte == 0x83 || lengthByte == 0x84) {
        return true;
    }

    // Short form length (0-127)
    if (lengthByte <= 0x7F) {
        return true;
    }

    return false;
}

bool FileDetector::isDL(const std::vector<uint8_t>& content) {
    if (content.size() < 50) {
        return false;
    }

    // Check for PKCS#7 SignedData structure
    if (!isDER(content)) {
        return false;
    }

    // Look for ICAO deviationList OID: 2.23.136.1.1.7
    // DER encoding: 06 06 67 81 08 01 01 07
    const uint8_t dlOid[] = {0x06, 0x06, 0x67, 0x81, 0x08, 0x01, 0x01, 0x07};

    // Search for OID in first 1KB (should be near the beginning)
    size_t searchLimit = std::min(content.size(), size_t(1024));
    for (size_t i = 0; i < searchLimit - sizeof(dlOid); ++i) {
        if (std::memcmp(content.data() + i, dlOid, sizeof(dlOid)) == 0) {
            return true;
        }
    }

    return false;
}

bool FileDetector::isMasterList(const std::vector<uint8_t>& content) {
    if (content.size() < 50) {
        return false;
    }

    // Check for PKCS#7 SignedData structure
    if (!isDER(content)) {
        return false;
    }

    // Look for ICAO cscaMasterList OID: 2.23.136.1.1.2
    // DER encoding: 06 06 67 81 08 01 01 02
    const uint8_t mlOid[] = {0x06, 0x06, 0x67, 0x81, 0x08, 0x01, 0x01, 0x02};

    // Search for OID in first 1KB
    size_t searchLimit = std::min(content.size(), size_t(1024));
    for (size_t i = 0; i < searchLimit - sizeof(mlOid); ++i) {
        if (std::memcmp(content.data() + i, mlOid, sizeof(mlOid)) == 0) {
            return true;
        }
    }

    return false;
}

bool FileDetector::isP7B(const std::vector<uint8_t>& content) {
    if (content.size() < 50) {
        return false;
    }

    // Must be DER-encoded
    if (!isDER(content)) {
        return false;
    }

    // Look for PKCS#7 SignedData OID: 1.2.840.113549.1.7.2
    // DER encoding: 06 09 2A 86 48 86 F7 0D 01 07 02
    const uint8_t signedDataOid[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02};

    size_t searchLimit = std::min(content.size(), size_t(256));
    for (size_t i = 0; i < searchLimit - sizeof(signedDataOid); ++i) {
        if (std::memcmp(content.data() + i, signedDataOid, sizeof(signedDataOid)) == 0) {
            // Found SignedData OID - but NOT DL or ML (already checked before this)
            return true;
        }
    }

    return false;
}

bool FileDetector::isCRL(const std::vector<uint8_t>& content) {
    if (content.size() < 10) {
        return false;
    }

    // Check PEM-encoded CRL
    const char* crlPemHeader = "-----BEGIN X509 CRL-----";
    if (content.size() >= 24 &&
        std::memcmp(content.data(), crlPemHeader, 24) == 0) {
        return true;
    }

    // DER-encoded CRL: starts with SEQUENCE tag (0x30), then contains
    // a TBSCertList starting with another SEQUENCE that has a version INTEGER
    // CRL structure: SEQUENCE { tbsCertList, signatureAlgorithm, signatureValue }
    // tbsCertList: SEQUENCE { version (optional), signature, issuer, thisUpdate, ... }
    // Key distinguisher from certificates: no explicit version tag [0] like X.509 certs
    // Instead, CRLs typically have version INTEGER directly or algorithm OID early
    if (!isDER(content)) {
        return false;
    }

    // For DER-encoded data, we can't easily distinguish CRL from certificate
    // by content alone without full ASN.1 parsing. Return false here -
    // DER CRL files should use .crl extension for detection.
    return false;
}

bool FileDetector::isLDIF(const std::vector<uint8_t>& content) {
    if (content.size() < 3) {
        return false;
    }

    // Check for LDIF headers: "dn:" or "version:"
    const char* dnHeader = "dn:";
    const char* versionHeader = "version:";

    if (content.size() >= 3) {
        if (std::memcmp(content.data(), dnHeader, 3) == 0) {
            return true;
        }
    }

    if (content.size() >= 8) {
        if (std::memcmp(content.data(), versionHeader, 8) == 0) {
            return true;
        }
    }

    return false;
}

std::string FileDetector::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string FileDetector::getExtension(const std::string& filename) {
    size_t dotPos = filename.find_last_of('.');
    if (dotPos == std::string::npos || dotPos == filename.length() - 1) {
        return "";
    }

    std::string ext = filename.substr(dotPos);
    return toLower(ext);
}

} // namespace certificate_parser
} // namespace icao

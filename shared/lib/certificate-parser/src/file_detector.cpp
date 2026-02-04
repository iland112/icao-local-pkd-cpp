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
        case FileFormat::DVL:    return "DVL";
        case FileFormat::LDIF:   return "LDIF";
        case FileFormat::ML:     return "ML";
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
    if (upper == "DVL")    return FileFormat::DVL;
    if (upper == "LDIF")   return FileFormat::LDIF;
    if (upper == "ML")     return FileFormat::ML;

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
    if (ext == ".dvl") {
        return FileFormat::DVL;
    }
    if (ext == ".ldif") {
        return FileFormat::LDIF;
    }
    if (ext == ".ml") {
        return FileFormat::ML;
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

    // Check Deviation List (PKCS#7 with DVL OID)
    if (isDVL(content)) {
        return FileFormat::DVL;
    }

    // Check Master List (PKCS#7 with ML OID)
    if (isMasterList(content)) {
        return FileFormat::ML;
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

bool FileDetector::isDVL(const std::vector<uint8_t>& content) {
    if (content.size() < 50) {
        return false;
    }

    // Check for PKCS#7 SignedData structure
    if (!isDER(content)) {
        return false;
    }

    // Look for ICAO deviationList OID: 2.23.136.1.1.7
    // DER encoding: 06 06 67 81 08 01 01 07
    const uint8_t dvlOid[] = {0x06, 0x06, 0x67, 0x81, 0x08, 0x01, 0x01, 0x07};

    // Search for OID in first 1KB (should be near the beginning)
    size_t searchLimit = std::min(content.size(), size_t(1024));
    for (size_t i = 0; i < searchLimit - sizeof(dvlOid); ++i) {
        if (std::memcmp(content.data() + i, dvlOid, sizeof(dvlOid)) == 0) {
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

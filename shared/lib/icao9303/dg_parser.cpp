/**
 * @file data_group_parser_service.cpp
 * @brief Implementation of DgParser
 */

#include "dg_parser.h"
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace icao {

// Helper function for Base64 encoding
static std::string base64Encode(const std::vector<uint8_t>& data) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);  // No newlines
    bio = BIO_push(b64, bio);

    BIO_write(bio, data.data(), static_cast<int>(data.size()));
    BIO_flush(bio);

    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    return result;
}

DgParser::DgParser() {
    spdlog::debug("DgParser initialized");
}

// ==========================================================================
// Public API Methods
// ==========================================================================

Json::Value DgParser::parseDg1(const std::vector<uint8_t>& dg1Data) {
    spdlog::debug("Parsing DG1 ({} bytes)", dg1Data.size());

    Json::Value result;

    // Try to find MRZ data in DG1 (ICAO 9303 ASN.1 structure)
    // DG1 structure: Tag 0x61, Length, Tag 0x5F1F, Length, MRZ data
    std::string mrzData;
    for (size_t i = 0; i < dg1Data.size() - 2; i++) {
        if (dg1Data[i] == 0x5F && dg1Data[i+1] == 0x1F) {
            // Found MRZ tag
            i += 2;
            size_t mrzLen = dg1Data[i++];
            if (mrzLen > 0x80) {
                int numBytes = mrzLen & 0x7F;
                mrzLen = 0;
                for (int j = 0; j < numBytes && i < dg1Data.size(); j++) {
                    mrzLen = (mrzLen << 8) | dg1Data[i++];
                }
            }
            if (i + mrzLen <= dg1Data.size()) {
                mrzData = std::string(reinterpret_cast<const char*>(&dg1Data[i]), mrzLen);
            }
            break;
        }
    }

    if (mrzData.empty()) {
        result["success"] = false;
        result["error"] = "Failed to extract MRZ from DG1";
        return result;
    }

    // Parse MRZ based on length
    if (mrzData.length() >= 88) {
        return parseMrzTd3(mrzData);
    } else if (mrzData.length() >= 72) {
        return parseMrzTd2(mrzData);
    } else if (mrzData.length() >= 30) {
        return parseMrzTd1(mrzData);
    } else {
        result["success"] = false;
        result["error"] = "MRZ data too short or invalid format (length: " +
            std::to_string(mrzData.length()) + ")";
        return result;
    }
}

Json::Value DgParser::parseMrzText(const std::string& mrzText) {
    spdlog::debug("Parsing MRZ text");

    // Remove newlines and spaces
    std::string cleanedMrz = mrzText;
    cleanedMrz.erase(std::remove(cleanedMrz.begin(), cleanedMrz.end(), '\n'), cleanedMrz.end());
    cleanedMrz.erase(std::remove(cleanedMrz.begin(), cleanedMrz.end(), '\r'), cleanedMrz.end());

    Json::Value result;

    // Parse based on length
    if (cleanedMrz.length() >= 88) {
        return parseMrzTd3(cleanedMrz);
    } else if (cleanedMrz.length() >= 72) {
        return parseMrzTd2(cleanedMrz);
    } else if (cleanedMrz.length() >= 30) {
        return parseMrzTd1(cleanedMrz);
    } else {
        result["success"] = false;
        result["error"] = "MRZ text too short or invalid format (length: " +
            std::to_string(cleanedMrz.length()) + ")";
        return result;
    }
}

Json::Value DgParser::parseDg2(const std::vector<uint8_t>& dg2Data) {
    spdlog::debug("Parsing DG2 ({} bytes)", dg2Data.size());

    Json::Value result;
    result["success"] = true;
    result["dg2Size"] = static_cast<int>(dg2Data.size());

    // DG2 contains biometric template (facial images) per ICAO Doc 9303 Part 10
    // Structure: Tag 0x7F60 (Biometric Information Template)
    //            Tag 0x5F2E (Biometric Data Block - contains JPEG/JP2 image)

    std::string imageFormat = "UNKNOWN";
    std::vector<uint8_t> imageData;

    // Search for JPEG or JPEG2000 signature within DG2
    bool foundImage = false;
    for (size_t i = 0; i < dg2Data.size() - 3; i++) {
        // JPEG signature: 0xFFD8FF
        if (dg2Data[i] == 0xFF && dg2Data[i+1] == 0xD8 && dg2Data[i+2] == 0xFF) {
            imageFormat = "JPEG";
            // Find JPEG end marker (0xFFD9)
            for (size_t j = i + 3; j < dg2Data.size() - 1; j++) {
                if (dg2Data[j] == 0xFF && dg2Data[j+1] == 0xD9) {
                    // Extract JPEG data
                    imageData.assign(dg2Data.begin() + i, dg2Data.begin() + j + 2);
                    foundImage = true;
                    break;
                }
            }
            if (foundImage) break;
        }
        // JPEG2000 signature: 0x0000000C 0x6A502020
        else if (i < dg2Data.size() - 8 &&
                 dg2Data[i] == 0x00 && dg2Data[i+1] == 0x00 &&
                 dg2Data[i+2] == 0x00 && dg2Data[i+3] == 0x0C) {
            imageFormat = "JPEG2000";
            // JPEG2000 doesn't have a simple end marker, use remaining data
            imageData.assign(dg2Data.begin() + i, dg2Data.end());
            foundImage = true;
            break;
        }
    }

    if (!foundImage || imageData.empty()) {
        result["success"] = false;
        result["error"] = "No valid face image found in DG2 data";
        result["message"] = "Could not extract JPEG/JPEG2000 image from biometric template";
        return result;
    }

    // Convert image data to Base64 for data URL
    std::string base64Image = base64Encode(imageData);
    std::string mimeType = (imageFormat == "JPEG") ? "image/jpeg" : "image/jp2";
    std::string imageDataUrl = "data:" + mimeType + ";base64," + base64Image;

    // Build faceImages array (ICAO Doc 9303 allows multiple face images)
    Json::Value faceImage;
    faceImage["imageDataUrl"] = imageDataUrl;
    faceImage["imageFormat"] = imageFormat;
    faceImage["imageSize"] = static_cast<int>(imageData.size());
    faceImage["imageType"] = "ICAO Face";  // As per CBEFF format

    Json::Value faceImages = Json::arrayValue;
    faceImages.append(faceImage);

    result["faceImages"] = faceImages;
    result["faceCount"] = 1;
    result["message"] = "Face image extracted successfully from DG2";
    result["imageFormat"] = imageFormat;  // Keep for backward compatibility

    spdlog::info("DG2 parsed: {} image extracted ({} bytes)", imageFormat, imageData.size());

    return result;
}

bool DgParser::verifyDataGroupHash(
    const std::vector<uint8_t>& dgData,
    const std::string& expectedHash,
    const std::string& hashAlgorithm)
{
    std::string actualHash = computeHash(dgData, hashAlgorithm);
    return actualHash == expectedHash;
}

std::string DgParser::computeHash(
    const std::vector<uint8_t>& data,
    const std::string& algorithm)
{
    const EVP_MD* md = nullptr;

    if (algorithm == "SHA-1" || algorithm == "SHA1") {
        md = EVP_sha1();
    } else if (algorithm == "SHA-256" || algorithm == "SHA256") {
        md = EVP_sha256();
    } else if (algorithm == "SHA-384" || algorithm == "SHA384") {
        md = EVP_sha384();
    } else if (algorithm == "SHA-512" || algorithm == "SHA512") {
        md = EVP_sha512();
    } else {
        spdlog::error("Unsupported hash algorithm: {}", algorithm);
        return "";
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash, &hashLen);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < hashLen; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }

    return oss.str();
}

// ==========================================================================
// Private Helper Methods
// ==========================================================================

std::string DgParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return str.substr(start, end - start + 1);
}

std::string DgParser::convertMrzDate(const std::string& yymmdd) {
    if (yymmdd.length() != 6) return yymmdd;

    int year = std::stoi(yymmdd.substr(0, 2));
    std::string month = yymmdd.substr(2, 2);
    std::string day = yymmdd.substr(4, 2);

    // ICAO 9303 rule: years 00-99 map to 1900-2099
    // For birth dates: assume 00-23 = 2000-2023, 24-99 = 1924-1999
    int fullYear = (year <= 23) ? 2000 + year : 1900 + year;

    return std::to_string(fullYear) + "-" + month + "-" + day;
}

std::string DgParser::convertMrzExpiryDate(const std::string& yymmdd) {
    if (yymmdd.length() != 6) return yymmdd;

    int year = std::stoi(yymmdd.substr(0, 2));
    std::string month = yymmdd.substr(2, 2);
    std::string day = yymmdd.substr(4, 2);

    // For expiry dates: assume 00-49 = 2000-2049, 50-99 = 1950-1999
    int fullYear = (year <= 49) ? 2000 + year : 1900 + year;

    return std::to_string(fullYear) + "-" + month + "-" + day;
}

std::string DgParser::cleanMrzField(const std::string& field) {
    std::string result = field;
    // Remove trailing < characters
    while (!result.empty() && result.back() == '<') {
        result.pop_back();
    }
    return result;
}

// ==========================================================================
// MRZ Format Parsing Methods
// ==========================================================================

Json::Value DgParser::parseMrzTd3(const std::string& mrzData) {
    // TD3 format (passport): 2 lines x 44 characters
    Json::Value result;

    std::string line1 = mrzData.substr(0, 44);
    std::string line2 = mrzData.substr(44, 44);

    // Java compatible: mrzLine1, mrzLine2, mrzFull
    result["mrzLine1"] = line1;
    result["mrzLine2"] = line2;
    result["mrzFull"] = mrzData;

    // Document type (first 2 characters)
    result["documentType"] = cleanMrzField(line1.substr(0, 2));
    result["issuingCountry"] = line1.substr(2, 3);

    // Parse name (surname<<givennames)
    size_t nameStart = 5;
    size_t nameEnd = line1.find("<<", nameStart);
    std::string surname, givenNames;

    if (nameEnd != std::string::npos) {
        surname = line1.substr(nameStart, nameEnd - nameStart);
        std::replace(surname.begin(), surname.end(), '<', ' ');
        surname = trim(surname);

        std::string givenPart = line1.substr(nameEnd + 2);
        std::replace(givenPart.begin(), givenPart.end(), '<', ' ');
        givenNames = trim(givenPart);
    } else {
        // No << separator found, try single < as separator
        nameEnd = line1.find('<', nameStart);
        if (nameEnd != std::string::npos) {
            surname = line1.substr(nameStart, nameEnd - nameStart);
            surname = trim(surname);
        }
    }

    result["surname"] = surname;
    result["givenNames"] = givenNames;

    // Java compatible: fullName field
    if (!surname.empty() && !givenNames.empty()) {
        result["fullName"] = surname + " " + givenNames;
    } else if (!surname.empty()) {
        result["fullName"] = surname;
    } else {
        result["fullName"] = givenNames;
    }

    // Line 2 parsing
    std::string docNum = cleanMrzField(line2.substr(0, 9));
    result["documentNumber"] = docNum;
    result["documentNumberCheckDigit"] = line2.substr(9, 1);

    result["nationality"] = line2.substr(10, 3);

    // Date of birth with YYYY-MM-DD format (Java compatible)
    std::string dobRaw = line2.substr(13, 6);
    result["dateOfBirth"] = convertMrzDate(dobRaw);
    result["dateOfBirthRaw"] = dobRaw;
    result["dateOfBirthCheckDigit"] = line2.substr(19, 1);

    // Sex
    result["sex"] = line2.substr(20, 1);

    // Date of expiry with YYYY-MM-DD format (Java compatible)
    std::string expiryRaw = line2.substr(21, 6);
    result["dateOfExpiry"] = convertMrzExpiryDate(expiryRaw);
    result["dateOfExpiryRaw"] = expiryRaw;
    result["dateOfExpiryCheckDigit"] = line2.substr(27, 1);

    // Optional data and composite check digit
    result["optionalData1"] = cleanMrzField(line2.substr(28, 14));
    result["compositeCheckDigit"] = line2.substr(43, 1);

    result["success"] = true;
    return result;
}

Json::Value DgParser::parseMrzTd2(const std::string& mrzData) {
    // TD2 format: 2 lines x 36 characters
    Json::Value result;

    std::string line1 = mrzData.substr(0, 36);
    std::string line2 = mrzData.substr(36, 36);

    result["mrzLine1"] = line1;
    result["mrzLine2"] = line2;
    result["mrzFull"] = mrzData;
    result["documentType"] = cleanMrzField(line1.substr(0, 2));
    result["issuingCountry"] = line1.substr(2, 3);

    // Name parsing
    size_t nameStart = 5;
    size_t nameEnd = line1.find("<<", nameStart);
    std::string surname, givenNames;

    if (nameEnd != std::string::npos) {
        surname = line1.substr(nameStart, nameEnd - nameStart);
        std::replace(surname.begin(), surname.end(), '<', ' ');
        surname = trim(surname);

        std::string givenPart = line1.substr(nameEnd + 2);
        std::replace(givenPart.begin(), givenPart.end(), '<', ' ');
        givenNames = trim(givenPart);
    }

    result["surname"] = surname;
    result["givenNames"] = givenNames;
    result["fullName"] = !surname.empty() ? (surname + " " + givenNames) : givenNames;

    // Line 2
    result["documentNumber"] = cleanMrzField(line2.substr(0, 9));
    result["nationality"] = line2.substr(10, 3);
    result["dateOfBirth"] = convertMrzDate(line2.substr(13, 6));
    result["dateOfBirthRaw"] = line2.substr(13, 6);
    result["sex"] = line2.substr(20, 1);
    result["dateOfExpiry"] = convertMrzExpiryDate(line2.substr(21, 6));
    result["dateOfExpiryRaw"] = line2.substr(21, 6);

    result["success"] = true;
    return result;
}

Json::Value DgParser::parseMrzTd1(const std::string& mrzData) {
    // TD1 format: 3 lines x 30 characters (ID cards)
    Json::Value result;

    result["mrzFull"] = mrzData;
    result["documentType"] = cleanMrzField(mrzData.substr(0, 2));
    result["issuingCountry"] = mrzData.substr(2, 3);
    result["documentNumber"] = cleanMrzField(mrzData.substr(5, 9));

    // For TD1, birth date is at different position
    if (mrzData.length() >= 60) {
        result["dateOfBirth"] = convertMrzDate(mrzData.substr(30, 6));
        result["dateOfBirthRaw"] = mrzData.substr(30, 6);
        result["sex"] = mrzData.substr(37, 1);
        result["dateOfExpiry"] = convertMrzExpiryDate(mrzData.substr(38, 6));
        result["dateOfExpiryRaw"] = mrzData.substr(38, 6);
        result["nationality"] = mrzData.substr(45, 3);
    }

    result["success"] = true;
    return result;
}

} // namespace icao

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
#include <cstring>

#ifdef HAS_OPENJPEG
#include <openjpeg.h>
#include <jpeglib.h>
#endif

namespace icao {

// ==========================================================================
// JPEG2000 → JPEG Conversion (when OpenJPEG + libjpeg available)
// ==========================================================================

#ifdef HAS_OPENJPEG

// OpenJPEG memory stream helper
struct MemStreamState {
    const uint8_t* data;
    OPJ_SIZE_T size;
    OPJ_SIZE_T offset;
};

static OPJ_SIZE_T memStreamRead(void* buffer, OPJ_SIZE_T nbBytes, void* userData) {
    auto* s = static_cast<MemStreamState*>(userData);
    if (s->offset >= s->size) return static_cast<OPJ_SIZE_T>(-1);
    OPJ_SIZE_T avail = s->size - s->offset;
    OPJ_SIZE_T toRead = (nbBytes < avail) ? nbBytes : avail;
    std::memcpy(buffer, s->data + s->offset, toRead);
    s->offset += toRead;
    return toRead;
}

static OPJ_OFF_T memStreamSkip(OPJ_OFF_T nbBytes, void* userData) {
    auto* s = static_cast<MemStreamState*>(userData);
    if (nbBytes < 0) {
        OPJ_SIZE_T back = static_cast<OPJ_SIZE_T>(-nbBytes);
        s->offset = (back > s->offset) ? 0 : s->offset - back;
    } else {
        s->offset += static_cast<OPJ_SIZE_T>(nbBytes);
        if (s->offset > s->size) s->offset = s->size;
    }
    return static_cast<OPJ_OFF_T>(s->offset);
}

static OPJ_BOOL memStreamSeek(OPJ_OFF_T nbBytes, void* userData) {
    auto* s = static_cast<MemStreamState*>(userData);
    if (nbBytes < 0 || static_cast<OPJ_SIZE_T>(nbBytes) > s->size) return OPJ_FALSE;
    s->offset = static_cast<OPJ_SIZE_T>(nbBytes);
    return OPJ_TRUE;
}

/**
 * @brief Convert JPEG2000 image data to JPEG format
 * Browsers do not support JPEG2000 natively, so server-side conversion is needed.
 * @param jp2Data Raw JPEG2000 data (JP2 container or J2K codestream)
 * @return JPEG data, or empty vector on failure
 */
static std::vector<uint8_t> convertJp2ToJpeg(const std::vector<uint8_t>& jp2Data) {
    // Detect codec type: JP2 container vs J2K codestream
    OPJ_CODEC_FORMAT codecFormat = OPJ_CODEC_JP2;
    if (jp2Data.size() >= 4 && jp2Data[0] == 0xFF && jp2Data[1] == 0x4F &&
        jp2Data[2] == 0xFF && jp2Data[3] == 0x51) {
        codecFormat = OPJ_CODEC_J2K;
    }

    opj_codec_t* codec = opj_create_decompress(codecFormat);
    if (!codec) {
        spdlog::error("[DG2] Failed to create JPEG2000 decoder");
        return {};
    }

    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);
    if (!opj_setup_decoder(codec, &params)) {
        opj_destroy_codec(codec);
        spdlog::error("[DG2] Failed to setup JPEG2000 decoder");
        return {};
    }

    // Create memory stream
    MemStreamState ms{jp2Data.data(), jp2Data.size(), 0};
    opj_stream_t* stream = opj_stream_create(jp2Data.size(), OPJ_TRUE);
    opj_stream_set_user_data(stream, &ms, nullptr);
    opj_stream_set_user_data_length(stream, jp2Data.size());
    opj_stream_set_read_function(stream, memStreamRead);
    opj_stream_set_skip_function(stream, memStreamSkip);
    opj_stream_set_seek_function(stream, memStreamSeek);

    // Decode
    opj_image_t* image = nullptr;
    if (!opj_read_header(stream, codec, &image)) {
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        spdlog::error("[DG2] Failed to read JPEG2000 header");
        return {};
    }

    if (!opj_decode(codec, stream, image)) {
        opj_image_destroy(image);
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        spdlog::error("[DG2] Failed to decode JPEG2000 image");
        return {};
    }

    opj_stream_destroy(stream);
    opj_destroy_codec(codec);

    int width = static_cast<int>(image->comps[0].w);
    int height = static_cast<int>(image->comps[0].h);
    int numComps = static_cast<int>(image->numcomps);

    spdlog::debug("[DG2] JPEG2000 decoded: {}x{}, {} components, color_space={}",
        width, height, numComps, static_cast<int>(image->color_space));

    // Build RGB buffer
    std::vector<uint8_t> rgbBuf(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            int off = idx * 3;

            if (numComps >= 3) {
                int r = image->comps[0].data[idx];
                int g = image->comps[1].data[idx];
                int b = image->comps[2].data[idx];

                // Adjust precision to 8 bits
                int prec = static_cast<int>(image->comps[0].prec);
                if (prec > 8) { r >>= (prec - 8); g >>= (prec - 8); b >>= (prec - 8); }
                else if (prec < 8) { r <<= (8 - prec); g <<= (8 - prec); b <<= (8 - prec); }

                rgbBuf[off]     = static_cast<uint8_t>(std::max(0, std::min(255, r)));
                rgbBuf[off + 1] = static_cast<uint8_t>(std::max(0, std::min(255, g)));
                rgbBuf[off + 2] = static_cast<uint8_t>(std::max(0, std::min(255, b)));
            } else {
                // Grayscale
                int v = image->comps[0].data[idx];
                int prec = static_cast<int>(image->comps[0].prec);
                if (prec > 8) v >>= (prec - 8);
                else if (prec < 8) v <<= (8 - prec);
                uint8_t val = static_cast<uint8_t>(std::max(0, std::min(255, v)));
                rgbBuf[off] = rgbBuf[off + 1] = rgbBuf[off + 2] = val;
            }
        }
    }
    opj_image_destroy(image);

    // Encode as JPEG using libjpeg
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;
    jpeg_mem_dest(&cinfo, &jpegBuf, &jpegSize);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW rowPtr[1];
    while (cinfo.next_scanline < cinfo.image_height) {
        rowPtr[0] = &rgbBuf[cinfo.next_scanline * width * 3];
        jpeg_write_scanlines(&cinfo, rowPtr, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    std::vector<uint8_t> result(jpegBuf, jpegBuf + jpegSize);
    free(jpegBuf);

    spdlog::info("[DG2] JPEG2000 → JPEG converted: {}x{}, {} bytes → {} bytes",
        width, height, jp2Data.size(), result.size());

    return result;
}

#endif // HAS_OPENJPEG

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

    // For JPEG2000: convert to JPEG since browsers don't support JP2 natively
    std::string mimeType;
    std::vector<uint8_t> displayData;

    if (imageFormat == "JPEG") {
        mimeType = "image/jpeg";
        displayData = imageData;
    } else {
#ifdef HAS_OPENJPEG
        // Convert JPEG2000 → JPEG for browser compatibility
        displayData = convertJp2ToJpeg(imageData);
        if (!displayData.empty()) {
            mimeType = "image/jpeg";
            spdlog::info("DG2: JPEG2000 converted to JPEG for browser display");
        } else {
            spdlog::warn("DG2: JPEG2000 conversion failed, returning raw JP2 data");
            mimeType = "image/jp2";
            displayData = imageData;
        }
#else
        spdlog::warn("DG2: JPEG2000 image detected but OpenJPEG not available for conversion");
        mimeType = "image/jp2";
        displayData = imageData;
#endif
    }

    // Convert image data to Base64 for data URL
    std::string base64Image = base64Encode(displayData);
    std::string imageDataUrl = "data:" + mimeType + ";base64," + base64Image;

    // Build faceImages array (ICAO Doc 9303 allows multiple face images)
    Json::Value faceImage;
    faceImage["imageDataUrl"] = imageDataUrl;
    faceImage["imageFormat"] = imageFormat;  // Original format (JPEG2000)
    faceImage["imageSize"] = static_cast<int>(imageData.size());  // Original size
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

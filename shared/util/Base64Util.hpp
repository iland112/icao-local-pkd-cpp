#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace shared::util {

/**
 * Base64 encoding/decoding utility using OpenSSL.
 */
class Base64Util {
public:
    /**
     * Encode binary data to Base64 string.
     */
    static std::string encode(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return "";
        }

        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* mem = BIO_new(BIO_s_mem());
        b64 = BIO_push(b64, mem);

        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(b64, data.data(), static_cast<int>(data.size()));
        BIO_flush(b64);

        BUF_MEM* bufferPtr;
        BIO_get_mem_ptr(b64, &bufferPtr);

        std::string result(bufferPtr->data, bufferPtr->length);
        BIO_free_all(b64);

        return result;
    }

    /**
     * Encode raw bytes to Base64 string.
     */
    static std::string encode(const uint8_t* data, size_t length) {
        return encode(std::vector<uint8_t>(data, data + length));
    }

    /**
     * Decode Base64 string to binary data.
     */
    static std::vector<uint8_t> decode(const std::string& encoded) {
        if (encoded.empty()) {
            return {};
        }

        // Calculate expected decoded length
        size_t decodedLength = (encoded.length() * 3) / 4;

        std::vector<uint8_t> result(decodedLength);

        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* mem = BIO_new_mem_buf(encoded.c_str(), static_cast<int>(encoded.length()));
        mem = BIO_push(b64, mem);

        BIO_set_flags(mem, BIO_FLAGS_BASE64_NO_NL);
        int actualLength = BIO_read(mem, result.data(), static_cast<int>(result.size()));
        BIO_free_all(mem);

        if (actualLength < 0) {
            throw std::runtime_error("Base64 decoding failed");
        }

        result.resize(static_cast<size_t>(actualLength));
        return result;
    }

    /**
     * Check if a string is valid Base64.
     */
    static bool isValidBase64(const std::string& str) {
        if (str.empty()) {
            return true;
        }

        static const std::string base64Chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

        for (char c : str) {
            if (base64Chars.find(c) == std::string::npos && c != '\n' && c != '\r') {
                return false;
            }
        }

        return true;
    }

    /**
     * Convert binary data to hex string.
     */
    static std::string toHex(const std::vector<uint8_t>& data) {
        static const char hexChars[] = "0123456789abcdef";
        std::string result;
        result.reserve(data.size() * 2);

        for (uint8_t byte : data) {
            result.push_back(hexChars[(byte >> 4) & 0x0F]);
            result.push_back(hexChars[byte & 0x0F]);
        }

        return result;
    }

    /**
     * Convert hex string to binary data.
     */
    static std::vector<uint8_t> fromHex(const std::string& hex) {
        if (hex.length() % 2 != 0) {
            throw std::runtime_error("Invalid hex string length");
        }

        std::vector<uint8_t> result;
        result.reserve(hex.length() / 2);

        for (size_t i = 0; i < hex.length(); i += 2) {
            uint8_t byte = 0;
            for (int j = 0; j < 2; ++j) {
                char c = hex[i + j];
                byte <<= 4;
                if (c >= '0' && c <= '9') {
                    byte |= (c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    byte |= (c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                    byte |= (c - 'A' + 10);
                } else {
                    throw std::runtime_error("Invalid hex character");
                }
            }
            result.push_back(byte);
        }

        return result;
    }
};

} // namespace shared::util

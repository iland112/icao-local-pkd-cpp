/**
 * @file FileFormat.hpp
 * @brief Enum for supported file formats
 */

#pragma once

#include <string>
#include <stdexcept>

namespace fileupload::domain::model {

/**
 * @brief Supported file formats for PKD uploads
 */
enum class FileFormat {
    LDIF,       // LDAP Data Interchange Format
    ML          // Master List (CMS SignedData)
};

/**
 * @brief Convert FileFormat to string
 */
inline std::string toString(FileFormat format) {
    switch (format) {
        case FileFormat::LDIF: return "LDIF";
        case FileFormat::ML: return "ML";
        default: throw std::invalid_argument("Unknown FileFormat");
    }
}

/**
 * @brief Parse string to FileFormat
 */
inline FileFormat parseFileFormat(const std::string& str) {
    if (str == "LDIF" || str == "ldif") return FileFormat::LDIF;
    if (str == "ML" || str == "ml") return FileFormat::ML;
    throw std::invalid_argument("Unknown file format: " + str);
}

/**
 * @brief Detect file format from filename extension
 */
inline FileFormat detectFileFormat(const std::string& filename) {
    size_t dotPos = filename.rfind('.');
    if (dotPos == std::string::npos) {
        throw std::invalid_argument("Cannot detect file format: no extension");
    }

    std::string ext = filename.substr(dotPos + 1);
    // Convert to lowercase
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (ext == "ldif") return FileFormat::LDIF;
    if (ext == "ml" || ext == "cms" || ext == "p7b" || ext == "p7c") return FileFormat::ML;

    throw std::invalid_argument("Unsupported file extension: " + ext);
}

} // namespace fileupload::domain::model

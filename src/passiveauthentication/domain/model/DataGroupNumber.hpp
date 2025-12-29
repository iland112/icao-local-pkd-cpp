#pragma once

#include <string>
#include <stdexcept>

namespace pa::domain::model {

/**
 * Data Group Number (DG1 ~ DG16) as per ICAO 9303 specification.
 *
 * LDS (Logical Data Structure) defines 16 data groups:
 * - DG1: MRZ (Machine Readable Zone)
 * - DG2: Encoded Face
 * - DG3: Encoded Fingerprints
 * - DG4: Encoded Iris
 * - DG5-DG16: Additional biometric and other data
 * - DG15: Active Authentication Public Key
 */
enum class DataGroupNumber {
    DG1 = 1,
    DG2 = 2,
    DG3 = 3,
    DG4 = 4,
    DG5 = 5,
    DG6 = 6,
    DG7 = 7,
    DG8 = 8,
    DG9 = 9,
    DG10 = 10,
    DG11 = 11,
    DG12 = 12,
    DG13 = 13,
    DG14 = 14,
    DG15 = 15,
    DG16 = 16
};

/**
 * Get integer value from DataGroupNumber.
 */
inline int toInt(DataGroupNumber dgn) {
    return static_cast<int>(dgn);
}

/**
 * Get DataGroupNumber from integer value.
 *
 * @param value integer value (1-16)
 * @return corresponding DataGroupNumber
 * @throws std::invalid_argument if value is out of range
 */
inline DataGroupNumber dataGroupNumberFromInt(int value) {
    if (value < 1 || value > 16) {
        throw std::invalid_argument(
            "Invalid Data Group Number: " + std::to_string(value) + ". Must be between 1 and 16."
        );
    }
    return static_cast<DataGroupNumber>(value);
}

/**
 * Get string representation of DataGroupNumber.
 */
inline std::string toString(DataGroupNumber dgn) {
    return "DG" + std::to_string(toInt(dgn));
}

/**
 * Get DataGroupNumber from string representation (e.g., "DG1", "DG15").
 *
 * @param str string representation
 * @return corresponding DataGroupNumber
 * @throws std::invalid_argument if string format is invalid
 */
inline DataGroupNumber dataGroupNumberFromString(const std::string& str) {
    if (str.length() < 3 || str.substr(0, 2) != "DG") {
        throw std::invalid_argument(
            "Invalid Data Group format: " + str + ". Expected format: DG1~DG16"
        );
    }

    try {
        int number = std::stoi(str.substr(2));
        return dataGroupNumberFromInt(number);
    } catch (const std::exception&) {
        throw std::invalid_argument(
            "Invalid Data Group format: " + str + ". Expected format: DG1~DG16"
        );
    }
}

} // namespace pa::domain::model

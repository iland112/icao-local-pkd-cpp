/**
 * @file ldif_parser.h
 * @brief LDIF Structure Parser Utilities
 *
 * Utilities for parsing LDIF files and extracting structure information
 * for visualization purposes (Upload Detail Dialog).
 *
 * This is separate from the main LDIF processing logic (ldif_processor.h)
 * which handles certificate extraction and database storage.
 */

#ifndef LDIF_PARSER_H
#define LDIF_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <utility>

namespace icao {
namespace ldif {

/**
 * @brief LDIF Attribute with binary detection
 */
struct LdifAttribute {
    std::string name;       // Attribute name (e.g., "cn", "userCertificate;binary")
    std::string value;      // Attribute value (or "[Binary Data: XXX bytes]" for binary)
    bool isBinary;          // True if this is a binary attribute (base64 encoded)
    size_t binarySize;      // Size in bytes (after base64 decoding)
};

/**
 * @brief LDIF Entry structure for visualization
 *
 * Note: This is different from ::LdifEntry (defined in common.h)
 * which is used for certificate processing.
 */
struct LdifEntryStructure {
    std::string dn;                          // Distinguished Name
    std::vector<LdifAttribute> attributes;   // All attributes with values
    std::string objectClass;                 // Primary objectClass (e.g., "pkdCertificate")
    int lineNumber;                          // Line number in LDIF file
};

/**
 * @brief Complete LDIF structure with statistics
 */
struct LdifStructure {
    std::vector<LdifEntryStructure> entries;     // Parsed entries (limited by maxEntries)
    int totalEntries;                             // Total entries in file
    int totalAttributes;                          // Total attributes across all entries
    std::map<std::string, int> objectClassCounts; // Count of entries by objectClass
    bool truncated;                               // True if totalEntries > entries.size()
};

/**
 * @brief LDIF Structure Parser
 *
 * Parses LDIF files to extract structure information for visualization.
 * Does NOT process certificates or save to database.
 */
class LdifParser {
public:
    /**
     * @brief Parse LDIF file and extract structure
     *
     * @param filePath Path to the LDIF file
     * @param maxEntries Maximum number of entries to parse (default: 100)
     * @return LdifStructure with parsed entries and statistics
     * @throws std::runtime_error if file cannot be read or parsed
     */
    static LdifStructure parse(const std::string& filePath, int maxEntries = 100);

    /**
     * @brief Detect if attribute value is binary (base64 encoded)
     *
     * LDIF binary attributes are indicated by "::" syntax:
     *   userCertificate;binary:: MIIBogYJKoZIhvcNAQcC...
     *
     * @param value Attribute value string
     * @return Pair of (isBinary, binarySize)
     */
    static std::pair<bool, size_t> parseBinaryAttribute(const std::string& value);

    /**
     * @brief Extract DN components for hierarchy display
     *
     * Example: "cn=CSCA,o=csca,c=FR,dc=data,dc=download,..."
     * Returns: ["cn=CSCA", "o=csca", "c=FR", "dc=data", ...]
     *
     * @param dn Distinguished Name
     * @return Vector of DN components
     */
    static std::vector<std::string> extractDnComponents(const std::string& dn);

    /**
     * @brief Calculate base64 decoded size
     *
     * @param base64Length Length of base64 encoded string
     * @return Decoded size in bytes (approximate)
     */
    static size_t calculateDecodedSize(size_t base64Length);

private:
    /**
     * @brief Parse single LDIF entry from content
     *
     * @param content LDIF file content
     * @param startPos Starting position in content
     * @param lineNumber Starting line number
     * @param entry Output LdifEntryStructure
     * @return Position after this entry (or std::string::npos if end of file)
     */
    static size_t parseEntry(
        const std::string& content,
        size_t startPos,
        int& lineNumber,
        LdifEntryStructure& entry
    );

    /**
     * @brief Count total entries in LDIF file (fast scan)
     *
     * @param content LDIF file content
     * @return Total number of entries
     */
    static int countEntries(const std::string& content);

    /**
     * @brief Extract primary objectClass from entry
     *
     * @param entry LDIF entry with attributes
     * @return Primary objectClass (e.g., "pkdCertificate", "pkdMasterList")
     */
    static std::string extractObjectClass(const LdifEntryStructure& entry);
};

}  // namespace ldif
}  // namespace icao

#endif  // LDIF_PARSER_H

/**
 * @file asn1_parser.h
 * @brief ASN.1 Structure Parser Utilities
 *
 * Utilities for parsing ASN.1/DER structures and generating
 * tree view with TLV (Tag-Length-Value) information.
 */

#ifndef ASN1_PARSER_H
#define ASN1_PARSER_H

#include <string>
#include <vector>
#include <json/json.h>

namespace icao {
namespace asn1 {

/**
 * ASN.1 Node representing a single TLV element
 */
struct Asn1Node {
    int offset;           // Byte offset in file
    int depth;            // Nesting depth
    int length;           // Length of value
    int headerLength;     // Length of tag + length fields
    std::string tag;      // Tag name (e.g., "SEQUENCE", "INTEGER", "OBJECT IDENTIFIER")
    std::string value;    // Value (if primitive type)
    std::vector<Asn1Node> children;  // Child nodes (for constructed types)
};

/**
 * Parse ASN.1/DER file using OpenSSL and generate tree structure
 *
 * @param filePath Path to the ASN.1/DER file
 * @param maxLines Maximum number of lines to parse (0 = unlimited, default 100)
 * @return Json::Value containing ASN.1 tree structure with TLV info
 */
Json::Value parseAsn1Structure(const std::string& filePath, int maxLines = 100);

/**
 * Execute OpenSSL asn1parse command and capture output
 *
 * @param filePath Path to the ASN.1/DER file
 * @param maxLines Maximum number of lines to return (0 = unlimited)
 * @return Raw output from OpenSSL asn1parse
 */
std::string executeAsn1Parse(const std::string& filePath, int maxLines = 100);

/**
 * Parse OpenSSL asn1parse output into structured JSON
 *
 * @param asn1ParseOutput Raw output from OpenSSL asn1parse -i
 * @return Json::Value with parsed ASN.1 structure
 */
Json::Value parseAsn1Output(const std::string& asn1ParseOutput);

} // namespace asn1
} // namespace icao

#endif // ASN1_PARSER_H

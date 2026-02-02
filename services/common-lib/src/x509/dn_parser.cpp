/**
 * @file dn_parser.cpp
 * @brief DN Parser implementation (stub)
 */

#include "icao/x509/dn_parser.h"

namespace icao {
namespace x509 {

std::optional<std::string> x509NameToString(X509_NAME* name, DnFormat format) {
    // TODO: Implement in Phase 2
    return std::nullopt;
}

bool compareX509Names(X509_NAME* name1, X509_NAME* name2) {
    // TODO: Implement in Phase 2
    return false;
}

std::optional<std::string> normalizeDnForComparison(const std::string& dn) {
    // TODO: Implement in Phase 2
    return std::nullopt;
}

X509_NAME* parseDnString(const std::string& dn) {
    // TODO: Implement in Phase 2
    return nullptr;
}

std::optional<std::string> getSubjectDn(X509* cert, DnFormat format) {
    // TODO: Implement in Phase 2
    return std::nullopt;
}

std::optional<std::string> getIssuerDn(X509* cert, DnFormat format) {
    // TODO: Implement in Phase 2
    return std::nullopt;
}

bool isSelfSigned(X509* cert) {
    // TODO: Implement in Phase 2
    return false;
}

} // namespace x509
} // namespace icao

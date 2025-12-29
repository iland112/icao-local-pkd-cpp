/**
 * @file LdapEntryType.hpp
 * @brief LDAP Entry Type Enumeration
 */

#pragma once

#include <string>

namespace ldapintegration::domain::model {

/**
 * @brief LDAP entry type enumeration
 */
enum class LdapEntryType {
    CSCA,           // Country Signing CA Certificate
    DSC,            // Document Signer Certificate
    DSC_NC,         // Document Signer Certificate (Non-Conformant)
    CRL,            // Certificate Revocation List
    MASTER_LIST     // Master List
};

/**
 * @brief Convert LdapEntryType to string
 */
inline std::string toString(LdapEntryType type) {
    switch (type) {
        case LdapEntryType::CSCA:        return "CSCA";
        case LdapEntryType::DSC:         return "DSC";
        case LdapEntryType::DSC_NC:      return "DSC_NC";
        case LdapEntryType::CRL:         return "CRL";
        case LdapEntryType::MASTER_LIST: return "MASTER_LIST";
        default:                         return "UNKNOWN";
    }
}

/**
 * @brief Get LDAP OU path for entry type
 */
inline std::string getOuPath(LdapEntryType type, const std::string& baseDn) {
    switch (type) {
        case LdapEntryType::CSCA:
            return "o=csca,dc=data,dc=download,dc=pkd," + baseDn;
        case LdapEntryType::DSC:
            return "o=dsc,dc=data,dc=download,dc=pkd," + baseDn;
        case LdapEntryType::DSC_NC:
            return "o=dsc,dc=nc-data,dc=download,dc=pkd," + baseDn;
        case LdapEntryType::CRL:
            return "o=crl,dc=data,dc=download,dc=pkd," + baseDn;
        case LdapEntryType::MASTER_LIST:
            return "o=ml,dc=data,dc=download,dc=pkd," + baseDn;
        default:
            return baseDn;
    }
}

} // namespace ldapintegration::domain::model

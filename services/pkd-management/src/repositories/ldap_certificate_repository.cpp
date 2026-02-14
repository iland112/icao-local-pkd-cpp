/**
 * @file ldap_certificate_repository.cpp
 * @brief LDAP Certificate Repository Implementation
 *
 * Clean Architecture: Infrastructure Layer
 * Handles LDAP operations and transforms data into domain entities.
 *
 * Migrated to use LdapConnectionPool for thread-safe connection management.
 */

#include "ldap_certificate_repository.h"
#include "../common/ldap_utils.h"
#include "../common/x509_metadata_extractor.h"
#include "icao/x509/dn_parser.h"      // Shared DN Parser
#include "icao/x509/dn_components.h"  // Shared DN Components
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace repositories {

// --- Constructor ---

LdapCertificateRepository::LdapCertificateRepository(
    common::LdapConnectionPool* ldapPool,
    const std::string& baseDn
)
    : ldapPool_(ldapPool)
    , baseDn_(baseDn)
{
    if (!ldapPool_) {
        throw std::runtime_error("LdapCertificateRepository: ldapPool cannot be null");
    }
    spdlog::info("[LdapCertificateRepository] Initialized with connection pool (baseDn={})", baseDn_);
}

// --- Public Interface Methods ---

domain::models::CertificateSearchResult LdapCertificateRepository::search(
    const domain::models::CertificateSearchCriteria& criteria
) {
    // Acquire LDAP connection from pool (RAII - automatically released)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        throw std::runtime_error("Failed to acquire LDAP connection from pool");
    }

    if (!criteria.isValid()) {
        throw std::runtime_error("Invalid search criteria");
    }

    spdlog::debug("[LdapCertificateRepository] Search criteria - Country: {}, CertType: {}, Limit: {}, Offset: {}",
        criteria.country.value_or("ALL"),
        criteria.certType.has_value() ? "SPECIFIED" : "ALL",
        criteria.limit,
        criteria.offset
    );

    // Build search parameters
    std::string baseDn = getSearchBaseDn(criteria.country, criteria.certType);
    std::string filter = buildSearchFilter(criteria);

    spdlog::debug("[LdapCertificateRepository] Search - BaseDN: {}, Filter: {}", baseDn, filter);

    // Attributes to retrieve (including DSC_NC specific attributes)
    const char* attrs[] = {
        "cn", "serialNumber", "c", "o", "userCertificate;binary",
        "cACertificate;binary", "certificateRevocationList;binary",
        "pkdConformanceCode", "pkdConformanceText", "pkdVersion", nullptr
    };

    // Execute LDAP search
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(
        conn.get(),  // Use connection from pool
        baseDn.c_str(),
        LDAP_SCOPE_SUBTREE,
        filter.c_str(),
        const_cast<char**>(attrs),
        0,
        nullptr,
        nullptr,
        nullptr,
        0,
        &result
    );

    if (rc != LDAP_SUCCESS) {
        std::string error = "LDAP search failed: " + std::string(ldap_err2string(rc));
        spdlog::error("[LdapCertificateRepository] {}", error);
        if (result) {
            ldap_msgfree(result);
        }
        throw std::runtime_error(error);
    }

    // Count total entries (before pagination)
    int totalCount = ldap_count_entries(conn.get(), result);
    spdlog::debug("[LdapCertificateRepository] Total entries found: {}", totalCount);

    // Prepare result
    domain::models::CertificateSearchResult searchResult;
    searchResult.total = totalCount;
    searchResult.limit = criteria.limit;
    searchResult.offset = criteria.offset;

    // Initialize statistics
    searchResult.stats.total = 0;
    searchResult.stats.valid = 0;
    searchResult.stats.expired = 0;
    searchResult.stats.notYetValid = 0;
    searchResult.stats.unknown = 0;

    // Post-filtering: When certType is specified without country, we search from dataTree
    // and need to filter by DN to match the requested type
    bool needsTypeFiltering = criteria.certType.has_value() &&
                              (!criteria.country.has_value() || criteria.country->empty());
    bool needsValidityFiltering = criteria.validity.has_value();

    // Efficient pagination: iterate through results and apply offset/limit
    int currentIndex = 0;
    int collected = 0;
    int matchedCount = 0; // Count of entries matching all criteria (including filters)

    // Note: When filtering is needed, we must iterate through ALL entries to get accurate total count
    for (LDAPMessage* entry = ldap_first_entry(conn.get(), result);
         entry != nullptr;
         entry = ldap_next_entry(conn.get(), entry))
    {
        // Get DN
        char* dnRaw = ldap_get_dn(conn.get(), entry);
        if (!dnRaw) {
            spdlog::warn("[LdapCertificateRepository] Entry without DN, skipping");
            continue;
        }
        std::string dn(dnRaw);
        ldap_memfree(dnRaw);

        // Apply type filtering if needed
        if (needsTypeFiltering) {
            domain::models::CertificateType dnType = extractCertTypeFromDn(dn);
            if (dnType != *criteria.certType) {
                continue; // Skip entries that don't match the requested type
            }
        }

        // Parse entry (will need it for validity check or final result)
        try {
            // Parse entry into Certificate entity
            domain::models::Certificate cert = parseEntry(conn.get(), entry, dn);

            // Update statistics (before validity filtering)
            // Note: Statistics represent all matching certificates (ignoring validity filter)
            if (!needsValidityFiltering) {
                searchResult.stats.total++;
                auto validityStatus = cert.getValidityStatus();
                switch (validityStatus) {
                    case domain::models::ValidityStatus::VALID:
                        searchResult.stats.valid++;
                        break;
                    case domain::models::ValidityStatus::EXPIRED:
                        searchResult.stats.expired++;
                        break;
                    case domain::models::ValidityStatus::NOT_YET_VALID:
                        searchResult.stats.notYetValid++;
                        break;
                    default:
                        searchResult.stats.unknown++;
                        break;
                }
            }

            // Apply validity filtering if needed
            if (needsValidityFiltering) {
                if (cert.getValidityStatus() != *criteria.validity) {
                    continue; // Skip entries that don't match the requested validity status
                }
            }

            // This entry matches all criteria
            matchedCount++;

            // Skip entries before offset
            if (matchedCount <= criteria.offset) {
                continue;
            }

            // Check if we've collected enough (but keep counting for total)
            if (collected >= criteria.limit) {
                continue;
            }

            // Add to result
            searchResult.certificates.push_back(std::move(cert));
            ++collected;
        } catch (const std::exception& e) {
            spdlog::warn("[LdapCertificateRepository] Failed to parse entry {}: {}", dn, e.what());
            continue;
        }
    }

    // Update total count if we applied any filtering
    if (needsTypeFiltering || needsValidityFiltering) {
        searchResult.total = matchedCount;
    }

    ldap_msgfree(result);

    spdlog::info("[LdapCertificateRepository] Search completed - Returned: {}/{} (Offset: {})",
        searchResult.certificates.size(), totalCount, criteria.offset);

    return searchResult;
}

domain::models::Certificate LdapCertificateRepository::getByDn(const std::string& dn) {
    // Acquire LDAP connection from pool (RAII - automatically released)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        throw std::runtime_error("Failed to acquire LDAP connection from pool");
    }

    spdlog::debug("[LdapCertificateRepository] Fetching certificate by DN: {}", dn);

    // Attributes to retrieve (including DSC_NC specific attributes)
    const char* attrs[] = {
        "cn", "serialNumber", "c", "o", "userCertificate;binary",
        "cACertificate;binary", "certificateRevocationList;binary",
        "pkdConformanceCode", "pkdConformanceText", "pkdVersion", nullptr
    };

    // Search for specific DN (base search)
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(
        conn.get(),  // Use connection from pool
        dn.c_str(),
        LDAP_SCOPE_BASE,
        "(objectClass=*)",
        const_cast<char**>(attrs),
        0,
        nullptr,
        nullptr,
        nullptr,
        0,
        &result
    );

    if (rc != LDAP_SUCCESS) {
        std::string error = "LDAP search failed for DN '" + dn + "': " + std::string(ldap_err2string(rc));
        spdlog::error("[LdapCertificateRepository] {}", error);
        if (result) {
            ldap_msgfree(result);
        }
        throw std::runtime_error(error);
    }

    // Get first entry
    LDAPMessage* entry = ldap_first_entry(conn.get(), result);
    if (!entry) {
        ldap_msgfree(result);
        throw std::runtime_error("Certificate not found for DN: " + dn);
    }

    // Parse entry
    domain::models::Certificate cert = parseEntry(conn.get(), entry, dn);
    ldap_msgfree(result);

    spdlog::info("[LdapCertificateRepository] Certificate fetched successfully: {}", dn);
    return cert;
}

std::vector<uint8_t> LdapCertificateRepository::getCertificateBinary(const std::string& dn) {
    // Acquire LDAP connection from pool (RAII - automatically released)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        throw std::runtime_error("Failed to acquire LDAP connection from pool");
    }

    spdlog::debug("[LdapCertificateRepository] Fetching certificate binary for DN: {}", dn);

    // Attributes to retrieve
    const char* attrs[] = {
        "userCertificate;binary", "cACertificate;binary",
        "certificateRevocationList;binary", nullptr
    };

    // Search for specific DN
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(
        conn.get(),  // Use connection from pool
        dn.c_str(),
        LDAP_SCOPE_BASE,
        "(objectClass=*)",
        const_cast<char**>(attrs),
        0,
        nullptr,
        nullptr,
        nullptr,
        0,
        &result
    );

    if (rc != LDAP_SUCCESS) {
        std::string error = "LDAP search failed for DN '" + dn + "': " + std::string(ldap_err2string(rc));
        spdlog::error("[LdapCertificateRepository] {}", error);
        if (result) {
            ldap_msgfree(result);
        }
        throw std::runtime_error(error);
    }

    // Get first entry
    LDAPMessage* entry = ldap_first_entry(conn.get(), result);
    if (!entry) {
        ldap_msgfree(result);
        throw std::runtime_error("Certificate not found for DN: " + dn);
    }

    // Try to get binary data from different attributes
    std::vector<uint8_t> binaryData = getBinaryAttributeValue(conn.get(), entry, "userCertificate;binary");
    if (binaryData.empty()) {
        binaryData = getBinaryAttributeValue(conn.get(), entry, "cACertificate;binary");
    }
    if (binaryData.empty()) {
        binaryData = getBinaryAttributeValue(conn.get(), entry, "certificateRevocationList;binary");
    }

    ldap_msgfree(result);

    if (binaryData.empty()) {
        throw std::runtime_error("No certificate binary data found for DN: " + dn);
    }

    spdlog::info("[LdapCertificateRepository] Certificate binary fetched: {} bytes", binaryData.size());
    return binaryData;
}

std::vector<std::string> LdapCertificateRepository::getDnsByCountryAndType(
    const std::string& country,
    std::optional<domain::models::CertificateType> certType
) {
    // Acquire LDAP connection from pool (RAII - automatically released)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        throw std::runtime_error("Failed to acquire LDAP connection from pool");
    }

    spdlog::debug("[LdapCertificateRepository] Fetching DNs - Country: {}, CertType: {}",
        country, certType.has_value() ? "SPECIFIED" : "ALL");

    // Build search parameters
    std::string baseDn = getSearchBaseDn(country, certType);
    std::string filter = "(|(objectClass=pkdDownload)(objectClass=cRLDistributionPoint))";

    spdlog::debug("[LdapCertificateRepository] Search - BaseDN: {}, Filter: {}", baseDn, filter);

    // We only need DN attribute
    const char* attrs[] = { "1.1", nullptr }; // Request no attributes (DN only)

    // Execute LDAP search
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(
        conn.get(),  // Use connection from pool
        baseDn.c_str(),
        LDAP_SCOPE_SUBTREE,
        filter.c_str(),
        const_cast<char**>(attrs),
        0,
        nullptr,
        nullptr,
        nullptr,
        0,
        &result
    );

    if (rc != LDAP_SUCCESS) {
        std::string error = "LDAP search failed: " + std::string(ldap_err2string(rc));
        spdlog::error("[LdapCertificateRepository] {}", error);
        if (result) {
            ldap_msgfree(result);
        }
        throw std::runtime_error(error);
    }

    // Collect all DNs
    std::vector<std::string> dns;
    for (LDAPMessage* entry = ldap_first_entry(conn.get(), result);
         entry != nullptr;
         entry = ldap_next_entry(conn.get(), entry))
    {
        char* dnRaw = ldap_get_dn(conn.get(), entry);
        if (dnRaw) {
            dns.emplace_back(dnRaw);
            ldap_memfree(dnRaw);
        }
    }

    ldap_msgfree(result);

    spdlog::info("[LdapCertificateRepository] Found {} DNs for country={}, certType={}",
        dns.size(), country, certType.has_value() ? "SPECIFIED" : "ALL");

    return dns;
}

// --- Private Helper Methods - Search Filter & Base DN ---

std::string LdapCertificateRepository::buildSearchFilter(
    const domain::models::CertificateSearchCriteria& criteria
) {
    // Use pkdDownload and cRLDistributionPoint objectClasses
    // Note: Country filtering is done via base DN, not filter
    std::string filter = "(|(objectClass=pkdDownload)(objectClass=cRLDistributionPoint))";

    // If search term is provided, add CN/serialNumber filter
    // SECURITY: Escape filter value to prevent LDAP injection (RFC 4515)
    if (criteria.searchTerm.has_value() && !criteria.searchTerm->empty()) {
        std::string searchTerm = ldap_utils::escapeFilterValue(*criteria.searchTerm);
        filter = "(&" + filter + "(|(cn=*" + searchTerm + "*)(serialNumber=*" + searchTerm + "*)))";
    }

    return filter;
}

std::string LdapCertificateRepository::getSearchBaseDn(
    std::optional<std::string> country,
    std::optional<domain::models::CertificateType> certType
) {
    std::string baseDn;

    // Certificate type determines the 'o' (organization) component
    std::string orgComponent;
    if (certType.has_value()) {
        switch (*certType) {
            case domain::models::CertificateType::CSCA:
                orgComponent = "o=csca,";
                break;
            case domain::models::CertificateType::MLSC:
                orgComponent = "o=mlsc,";
                break;
            case domain::models::CertificateType::DSC:
                orgComponent = "o=dsc,";
                break;
            case domain::models::CertificateType::DSC_NC:
                orgComponent = "o=dsc,"; // DSC_NC is under o=dsc in nc-data branch
                break;
            case domain::models::CertificateType::CRL:
                orgComponent = "o=crl,";
                break;
            case domain::models::CertificateType::ML:
                orgComponent = "o=ml,";
                break;
        }
    }

    // Country determines the 'c' component
    std::string countryComponent;
    if (country.has_value() && !country->empty()) {
        countryComponent = "c=" + *country + ",";
    }

    // Determine data tree (dc=data or dc=nc-data)
    std::string dataTree = "dc=data";
    if (certType.has_value() && *certType == domain::models::CertificateType::DSC_NC) {
        dataTree = "dc=nc-data";
    }

    // Build base DN
    // Pattern: [o={type},][c={country},]{dataTree},{baseDn}
    // Priority: certType + country > certType only > country only > all

    if (certType.has_value() && country.has_value() && !country->empty()) {
        // Both type and country specified: o={type},c={country},{dataTree},...
        baseDn = orgComponent + countryComponent + dataTree + "," + baseDn_;
    } else if (certType.has_value()) {
        // Only type specified: search all countries for this type
        // We need to search under each country's o={type} branch
        // Since LDAP doesn't support this easily, we'll search from dataTree and filter in-memory
        // Better approach: search from o={type} level if it exists at top level
        // For now, search all and filter by parsing DN
        baseDn = dataTree + "," + baseDn_;
    } else if (country.has_value() && !country->empty()) {
        // Only country specified: c={country},{dataTree},...
        baseDn = countryComponent + dataTree + "," + baseDn_;
    } else {
        // No country and no type: search all under dataTree
        baseDn = dataTree + "," + baseDn_;
    }

    return baseDn;
}

// --- Private Helper Methods - LDAP Entry Parsing ---

domain::models::Certificate LdapCertificateRepository::parseEntry(
    LDAP* ldap,
    LDAPMessage* entry,
    const std::string& dn
) {
    // Extract basic attributes from LDAP
    std::string cn = getAttributeValue(ldap, entry, "cn");
    std::string sn = getAttributeValue(ldap, entry, "serialNumber");
    std::string country = extractCountryFromDn(dn);
    domain::models::CertificateType certType = extractCertTypeFromDn(dn);

    // Get binary certificate data
    std::vector<uint8_t> certBinary = getBinaryAttributeValue(ldap, entry, "userCertificate;binary");
    if (certBinary.empty()) {
        certBinary = getBinaryAttributeValue(ldap, entry, "cACertificate;binary");
    }
    if (certBinary.empty()) {
        certBinary = getBinaryAttributeValue(ldap, entry, "certificateRevocationList;binary");
    }

    if (certBinary.empty()) {
        throw std::runtime_error("No certificate binary data found in entry: " + dn);
    }

    // Parse X.509 certificate (including metadata)
    std::string subjectDn, issuerDn, fingerprint;
    std::chrono::system_clock::time_point validFrom, validTo;

    // X.509 Metadata variables
    int version = 2;
    std::optional<std::string> signatureAlgorithm;
    std::optional<std::string> signatureHashAlgorithm;
    std::optional<std::string> publicKeyAlgorithm;
    std::optional<int> publicKeySize;
    std::optional<std::string> publicKeyCurve;
    std::vector<std::string> keyUsage;
    std::vector<std::string> extendedKeyUsage;
    std::optional<bool> isCA;
    std::optional<int> pathLenConstraint;
    std::optional<std::string> subjectKeyIdentifier;
    std::optional<std::string> authorityKeyIdentifier;
    std::vector<std::string> crlDistributionPoints;
    std::optional<std::string> ocspResponderUrl;
    std::optional<bool> isCertSelfSigned;

    // DN Components (shared library)
    std::optional<icao::x509::DnComponents> subjectDnComponents;
    std::optional<icao::x509::DnComponents> issuerDnComponents;

    parseX509Certificate(
        certBinary,
        subjectDn,
        issuerDn,
        cn, // May be updated from certificate
        sn, // May be updated from certificate
        fingerprint,
        validFrom,
        validTo,
        // X.509 Metadata
        version,
        signatureAlgorithm,
        signatureHashAlgorithm,
        publicKeyAlgorithm,
        publicKeySize,
        publicKeyCurve,
        keyUsage,
        extendedKeyUsage,
        isCA,
        pathLenConstraint,
        subjectKeyIdentifier,
        authorityKeyIdentifier,
        crlDistributionPoints,
        ocspResponderUrl,
        isCertSelfSigned,
        // DN Components
        subjectDnComponents,
        issuerDnComponents
    );

    // Read DSC_NC specific attributes (optional)
    std::optional<std::string> pkdConformanceCode;
    std::optional<std::string> pkdConformanceText;
    std::optional<std::string> pkdVersion;

    if (certType == domain::models::CertificateType::DSC_NC) {
        std::string conformanceCode = getAttributeValue(ldap, entry, "pkdConformanceCode");
        if (!conformanceCode.empty()) {
            pkdConformanceCode = conformanceCode;
            spdlog::debug("[LdapCertificateRepository] DSC_NC pkdConformanceCode: {}", conformanceCode);
        }

        std::string conformanceText = getAttributeValue(ldap, entry, "pkdConformanceText");
        if (!conformanceText.empty()) {
            pkdConformanceText = conformanceText;
            spdlog::debug("[LdapCertificateRepository] DSC_NC pkdConformanceText: {}", conformanceText.substr(0, 50));
        }

        std::string version = getAttributeValue(ldap, entry, "pkdVersion");
        if (!version.empty()) {
            pkdVersion = version;
            spdlog::debug("[LdapCertificateRepository] DSC_NC pkdVersion: {}", version);
        }

        spdlog::info("[LdapCertificateRepository] DSC_NC attributes read - Code:{}, Text:{}, Version:{}",
            pkdConformanceCode.has_value() ? "YES" : "NO",
            pkdConformanceText.has_value() ? "YES" : "NO",
            pkdVersion.has_value() ? "YES" : "NO");
    }

    // Create Certificate entity (with X.509 metadata)
    return domain::models::Certificate(
        dn,
        cn,
        sn,
        country,
        certType,
        subjectDn,
        issuerDn,
        fingerprint,
        validFrom,
        validTo,
        pkdConformanceCode,
        pkdConformanceText,
        pkdVersion,
        // DN Components (shared library)
        subjectDnComponents,
        issuerDnComponents,
        // X.509 Metadata
        version,
        signatureAlgorithm,
        signatureHashAlgorithm,
        publicKeyAlgorithm,
        publicKeySize,
        publicKeyCurve,
        keyUsage,
        extendedKeyUsage,
        isCA,
        pathLenConstraint,
        subjectKeyIdentifier,
        authorityKeyIdentifier,
        crlDistributionPoints,
        ocspResponderUrl,
        isCertSelfSigned
    );
}

domain::models::CertificateType LdapCertificateRepository::extractCertTypeFromDn(const std::string& dn) {
    // Convert to lowercase for case-insensitive matching
    std::string dnLower = dn;
    std::transform(dnLower.begin(), dnLower.end(), dnLower.begin(), ::tolower);

    // Check for certificate type in DN (o=csca, o=lc, o=mlsc, o=dsc, o=crl, o=ml)
    if (dnLower.find("o=csca") != std::string::npos) {
        return domain::models::CertificateType::CSCA;
    } else if (dnLower.find("o=lc") != std::string::npos) {
        // Link Certificates are stored as CSCA type in database
        return domain::models::CertificateType::CSCA;
    } else if (dnLower.find("o=mlsc") != std::string::npos) {
        return domain::models::CertificateType::MLSC;
    } else if (dnLower.find("o=dsc") != std::string::npos) {
        // Check if it's under nc-data (non-conformant)
        if (dnLower.find("dc=nc-data") != std::string::npos) {
            return domain::models::CertificateType::DSC_NC;
        } else {
            return domain::models::CertificateType::DSC;
        }
    } else if (dnLower.find("o=crl") != std::string::npos) {
        return domain::models::CertificateType::CRL;
    } else if (dnLower.find("o=ml") != std::string::npos) {
        return domain::models::CertificateType::ML;
    }

    // Default to DSC if unable to determine
    spdlog::warn("[LdapCertificateRepository] Unable to determine cert type from DN: {}", dn);
    return domain::models::CertificateType::DSC;
}

std::string LdapCertificateRepository::extractCountryFromDn(const std::string& dn) {
    // Extract country code from DN (c=XX)
    size_t pos = dn.find("c=");
    if (pos == std::string::npos) {
        pos = dn.find("C=");
    }
    if (pos == std::string::npos) {
        return "XX"; // Unknown country
    }

    pos += 2; // Skip "c=" or "C="
    size_t endPos = dn.find(',', pos);
    if (endPos == std::string::npos) {
        endPos = dn.length();
    }

    std::string country = dn.substr(pos, endPos - pos);

    // Convert to uppercase
    std::transform(country.begin(), country.end(), country.begin(), ::toupper);

    return country;
}

void LdapCertificateRepository::parseX509Certificate(
    const std::vector<uint8_t>& derData,
    std::string& subjectDn,
    std::string& issuerDn,
    std::string& cn,
    std::string& sn,
    std::string& fingerprint,
    std::chrono::system_clock::time_point& validFrom,
    std::chrono::system_clock::time_point& validTo,
    // X.509 Metadata
    int& version,
    std::optional<std::string>& signatureAlgorithm,
    std::optional<std::string>& signatureHashAlgorithm,
    std::optional<std::string>& publicKeyAlgorithm,
    std::optional<int>& publicKeySize,
    std::optional<std::string>& publicKeyCurve,
    std::vector<std::string>& keyUsage,
    std::vector<std::string>& extendedKeyUsage,
    std::optional<bool>& isCA,
    std::optional<int>& pathLenConstraint,
    std::optional<std::string>& subjectKeyIdentifier,
    std::optional<std::string>& authorityKeyIdentifier,
    std::vector<std::string>& crlDistributionPoints,
    std::optional<std::string>& ocspResponderUrl,
    std::optional<bool>& isCertSelfSigned,
    // DN Components (shared library)
    std::optional<icao::x509::DnComponents>& subjectDnComponents,
    std::optional<icao::x509::DnComponents>& issuerDnComponents
) {
    // Parse DER-encoded certificate using OpenSSL
    const unsigned char* data = derData.data();
    X509* cert = d2i_X509(nullptr, &data, derData.size());
    if (!cert) {
        throw std::runtime_error("Failed to parse X.509 certificate");
    }

    // Extract Subject DN
    char* subjectStr = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (subjectStr) {
        subjectDn = subjectStr;
        OPENSSL_free(subjectStr);
    }

    // Extract Issuer DN
    char* issuerStr = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    if (issuerStr) {
        issuerDn = issuerStr;
        OPENSSL_free(issuerStr);
    }

    // Parse DNs into DnComponents using shared library
    try {
        // Extract DN components directly from X509 certificate
        X509_NAME* subject = X509_get_subject_name(cert);
        if (subject) {
            subjectDnComponents = icao::x509::extractDnComponents(subject);
            if (subjectDnComponents->country.has_value() || subjectDnComponents->organization.has_value()) {
                spdlog::debug("[LdapCertificateRepository] Parsed Subject DN components: C={}, O={}, CN={}",
                             subjectDnComponents->country.value_or(""),
                             subjectDnComponents->organization.value_or(""),
                             subjectDnComponents->commonName.value_or(""));
            }
        }

        X509_NAME* issuer = X509_get_issuer_name(cert);
        if (issuer) {
            issuerDnComponents = icao::x509::extractDnComponents(issuer);
            if (issuerDnComponents->country.has_value() || issuerDnComponents->organization.has_value()) {
                spdlog::debug("[LdapCertificateRepository] Parsed Issuer DN components: C={}, O={}, CN={}",
                             issuerDnComponents->country.value_or(""),
                             issuerDnComponents->organization.value_or(""),
                             issuerDnComponents->commonName.value_or(""));
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[LdapCertificateRepository] Failed to parse DN components: {}", e.what());
        // Continue without DN components - fields will remain as nullopt
    }

    // Extract CN from Subject (if not already set)
    if (cn.empty()) {
        X509_NAME* subject = X509_get_subject_name(cert);
        int cnPos = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
        if (cnPos >= 0) {
            X509_NAME_ENTRY* cnEntry = X509_NAME_get_entry(subject, cnPos);
            ASN1_STRING* cnAsn1 = X509_NAME_ENTRY_get_data(cnEntry);
            const unsigned char* cnData = ASN1_STRING_get0_data(cnAsn1);
            if (cnData) {
                cn = reinterpret_cast<const char*>(cnData);
            }
        }
    }

    // Extract Serial Number (if not already set)
    if (sn.empty()) {
        ASN1_INTEGER* serial = X509_get_serialNumber(cert);
        if (serial) {
            BIGNUM* bnSerial = ASN1_INTEGER_to_BN(serial, nullptr);
            if (bnSerial) {
                char* serialStr = BN_bn2hex(bnSerial);
                if (serialStr) {
                    sn = serialStr;
                    OPENSSL_free(serialStr);
                }
                BN_free(bnSerial);
            }
        }
    }

    // Calculate SHA-256 fingerprint
    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int hashLen;
    if (X509_digest(cert, EVP_sha256(), hash, &hashLen)) {
        std::ostringstream oss;
        for (unsigned int i = 0; i < hashLen; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        fingerprint = oss.str();
    }

    // Extract validity dates
    const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);

    // Convert ASN1_TIME to time_point
    if (notBefore) {
        struct tm tm = {};
        ASN1_TIME_to_tm(notBefore, &tm);
        time_t t = mktime(&tm);
        validFrom = std::chrono::system_clock::from_time_t(t);
    }

    if (notAfter) {
        struct tm tm = {};
        ASN1_TIME_to_tm(notAfter, &tm);
        time_t t = mktime(&tm);
        validTo = std::chrono::system_clock::from_time_t(t);
    }

    // Extract X.509 metadata
    try {
        auto metadata = x509::extractMetadata(cert);

        version = metadata.version;
        signatureAlgorithm = metadata.signatureAlgorithm;
        signatureHashAlgorithm = metadata.signatureHashAlgorithm;
        publicKeyAlgorithm = metadata.publicKeyAlgorithm;
        publicKeySize = metadata.publicKeySize;
        publicKeyCurve = metadata.publicKeyCurve;
        keyUsage = metadata.keyUsage;
        extendedKeyUsage = metadata.extendedKeyUsage;
        isCA = metadata.isCA;
        pathLenConstraint = metadata.pathLenConstraint;
        subjectKeyIdentifier = metadata.subjectKeyIdentifier;
        authorityKeyIdentifier = metadata.authorityKeyIdentifier;
        crlDistributionPoints = metadata.crlDistributionPoints;
        ocspResponderUrl = metadata.ocspResponderUrl;
        isCertSelfSigned = metadata.isSelfSigned;

        spdlog::debug("[LdapCertificateRepository] Extracted X.509 metadata - "
                     "Version: {}, SigAlg: {}, PubKeyAlg: {}, KeySize: {}, isCA: {}",
                     metadata.version,
                     metadata.signatureAlgorithm,
                     metadata.publicKeyAlgorithm,
                     metadata.publicKeySize,
                     metadata.isCA ? "TRUE" : "FALSE");
    } catch (const std::exception& e) {
        spdlog::warn("[LdapCertificateRepository] Failed to extract X.509 metadata: {}", e.what());
        // Continue without metadata - fields will remain as default/nullopt
    }

    X509_free(cert);
}

// --- Private Helper Methods - LDAP Attribute Access ---

std::string LdapCertificateRepository::getAttributeValue(LDAP* ldap, LDAPMessage* entry, const char* attrName) {
    BerElement* ber = nullptr;
    char* attr = ldap_first_attribute(ldap, entry, &ber);

    std::string result;

    while (attr) {
        if (strcasecmp(attr, attrName) == 0) {
            struct berval** values = ldap_get_values_len(ldap, entry, attr);
            if (values && values[0]) {
                result = std::string(values[0]->bv_val, values[0]->bv_len);
                ldap_value_free_len(values);
            }
            ldap_memfree(attr);
            break;
        }
        ldap_memfree(attr);
        attr = ldap_next_attribute(ldap, entry, ber);
    }

    if (ber) {
        ber_free(ber, 0);
    }

    return result;
}

std::vector<uint8_t> LdapCertificateRepository::getBinaryAttributeValue(
    LDAP* ldap,
    LDAPMessage* entry,
    const char* attrName
) {
    BerElement* ber = nullptr;
    char* attr = ldap_first_attribute(ldap, entry, &ber);

    std::vector<uint8_t> result;

    while (attr) {
        if (strcasecmp(attr, attrName) == 0) {
            struct berval** values = ldap_get_values_len(ldap, entry, attr);
            if (values && values[0]) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(values[0]->bv_val);
                size_t len = values[0]->bv_len;
                result.assign(data, data + len);
                ldap_value_free_len(values);
            }
            ldap_memfree(attr);
            break;
        }
        ldap_memfree(attr);
        attr = ldap_next_attribute(ldap, entry, ber);
    }

    if (ber) {
        ber_free(ber, 0);
    }

    return result;
}

} // namespace repositories

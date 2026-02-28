/**
 * @file ldap_storage_service.cpp
 * @brief LDAP storage operations implementation
 */

#include "ldap_storage_service.h"
#include "../infrastructure/app_config.h"
#include "../common/ldap_utils.h"
#include <icao/x509/dn_parser.h>
#include <icao/x509/dn_components.h>

#include <ldap.h>
#include <spdlog/spdlog.h>

#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>

namespace services {

LdapStorageService::LdapStorageService(const AppConfig& config)
    : config_(config) {}

// --- LDAP Connection Management ---

LDAP* LdapStorageService::getLdapWriteConnection() {
    LDAP* ld = nullptr;
    std::string uri = "ldap://" + config_.ldapWriteHost + ":" + std::to_string(config_.ldapWritePort);

    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    // DoS defense: network timeout to prevent blocking on unresponsive LDAP
    int writeTimeoutSec = 10;
    if (auto* v = std::getenv("LDAP_WRITE_TIMEOUT")) writeTimeoutSec = std::stoi(v);
    struct timeval ldapTimeout = {writeTimeoutSec, 0};
    ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &ldapTimeout);

    struct berval cred;
    cred.bv_val = const_cast<char*>(config_.ldapBindPassword.c_str());
    cred.bv_len = config_.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, config_.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    spdlog::debug("LDAP write: Connected successfully to {}:{}", config_.ldapWriteHost, config_.ldapWritePort);
    return ld;
}

LDAP* LdapStorageService::getLdapReadConnection() {
    if (config_.ldapReadHostList.empty()) {
        spdlog::error("LDAP read connection failed: No LDAP hosts configured");
        return nullptr;
    }

    size_t hostIndex = ldapReadRoundRobinIndex_.fetch_add(1) % config_.ldapReadHostList.size();
    std::string selectedHost = config_.ldapReadHostList[hostIndex];
    std::string uri = "ldap://" + selectedHost;

    spdlog::debug("LDAP read: Connecting to {} (round-robin index: {})", selectedHost, hostIndex);

    LDAP* ld = nullptr;
    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection initialize failed for {}: {}", selectedHost, ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    // DoS defense: network timeout to prevent blocking on unresponsive LDAP
    int writeTimeoutSec = 10;
    if (auto* v = std::getenv("LDAP_WRITE_TIMEOUT")) writeTimeoutSec = std::stoi(v);
    struct timeval ldapTimeout = {writeTimeoutSec, 0};
    ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &ldapTimeout);

    struct berval cred;
    cred.bv_val = const_cast<char*>(config_.ldapBindPassword.c_str());
    cred.bv_len = config_.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, config_.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection bind failed for {}: {}", selectedHost, ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    spdlog::debug("LDAP read: Connected successfully to {}", selectedHost);
    return ld;
}

// --- DN Building ---

std::string LdapStorageService::escapeLdapDnValue(const std::string& value) {
    if (value.empty()) return value;

    std::string escaped;
    escaped.reserve(value.size() * 2);

    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];

        if (c == ',' || c == '=' || c == '+' || c == '"' || c == '\\' ||
            c == '<' || c == '>' || c == ';') {
            escaped += '\\';
            escaped += c;
        }
        else if (i == 0 && (c == ' ' || c == '#')) {
            escaped += '\\';
            escaped += c;
        }
        else if (i == value.size() - 1 && c == ' ') {
            escaped += '\\';
            escaped += c;
        }
        else {
            escaped += c;
        }
    }

    return escaped;
}

std::pair<std::string, std::string> LdapStorageService::extractStandardAttributes(const std::string& subjectDn) {
    std::string standardDn;
    std::string nonStandardAttrs;

    try {
        // Use shared library DN parsing for robust handling
        X509_NAME* x509Name = icao::x509::parseDnString(subjectDn);
        if (x509Name) {
            auto components = icao::x509::extractDnComponents(x509Name);
            X509_NAME_free(x509Name);

            // Build standardDn from known standard fields
            std::vector<std::string> standardRdns;

            if (components.commonName.has_value() && !components.commonName->empty()) {
                standardRdns.push_back("CN=" + *components.commonName);
            }
            if (components.organization.has_value() && !components.organization->empty()) {
                standardRdns.push_back("O=" + *components.organization);
            }
            if (components.organizationalUnit.has_value() && !components.organizationalUnit->empty()) {
                standardRdns.push_back("OU=" + *components.organizationalUnit);
            }
            if (components.country.has_value() && !components.country->empty()) {
                standardRdns.push_back("C=" + *components.country);
            }
            if (components.locality.has_value() && !components.locality->empty()) {
                standardRdns.push_back("L=" + *components.locality);
            }
            if (components.stateOrProvince.has_value() && !components.stateOrProvince->empty()) {
                standardRdns.push_back("ST=" + *components.stateOrProvince);
            }

            for (size_t i = 0; i < standardRdns.size(); ++i) {
                if (i > 0) standardDn += ",";
                standardDn += standardRdns[i];
            }

            if (standardDn.empty()) {
                standardDn = subjectDn;
            }

            // Non-standard attributes: email, serialNumber, etc.
            std::vector<std::string> nonStdRdns;
            if (components.email.has_value() && !components.email->empty()) {
                nonStdRdns.push_back("emailAddress=" + *components.email);
            }
            if (components.serialNumber.has_value() && !components.serialNumber->empty()) {
                nonStdRdns.push_back("serialNumber=" + *components.serialNumber);
            }
            for (size_t i = 0; i < nonStdRdns.size(); ++i) {
                if (i > 0) nonStandardAttrs += ",";
                nonStandardAttrs += nonStdRdns[i];
            }
        } else {
            standardDn = subjectDn;
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to parse DN '{}': {}", subjectDn.substr(0, 80), e.what());
        standardDn = subjectDn;
    }

    if (standardDn.empty()) {
        standardDn = subjectDn;
    }

    return {standardDn, nonStandardAttrs};
}

std::string LdapStorageService::buildCertificateDn(const std::string& certType, const std::string& countryCode,
                                                     const std::string& subjectDn, const std::string& serialNumber) {
    std::string ou;
    std::string dataContainer;

    if (certType == "CSCA") {
        ou = "csca";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "LC") {
        ou = "lc";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "DSC_NC") {
        ou = "dsc";
        dataContainer = config_.ldapNcDataContainer;
    } else {
        ou = "dsc";
        dataContainer = config_.ldapDataContainer;
    }

    auto [standardDn_extracted, nonStandardAttrs] = extractStandardAttributes(subjectDn);
    std::string escapedSubjectDn = escapeLdapDnValue(standardDn_extracted);

    return "cn=" + escapedSubjectDn + "+sn=" + serialNumber + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + "," + config_.ldapBaseDn;
}

std::string LdapStorageService::buildCertificateDnV2(const std::string& fingerprint, const std::string& certType,
                                                       const std::string& countryCode) {
    std::string ou;
    std::string dataContainer;

    if (certType == "CSCA") {
        ou = "csca";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "DSC_NC") {
        ou = "dsc";
        dataContainer = config_.ldapNcDataContainer;
    } else if (certType == "LC") {
        ou = "lc";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "MLSC") {
        ou = "mlsc";
        dataContainer = config_.ldapDataContainer;
    } else {
        ou = "dsc";
        dataContainer = config_.ldapDataContainer;
    }

    return "cn=" + fingerprint + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + "," + config_.ldapBaseDn;
}

std::string LdapStorageService::buildCrlDn(const std::string& countryCode, const std::string& fingerprint) {
    return "cn=" + ldap_utils::escapeDnComponent(fingerprint) +
           ",o=crl,c=" + ldap_utils::escapeDnComponent(countryCode) +
           "," + config_.ldapDataContainer + "," + config_.ldapBaseDn;
}

std::string LdapStorageService::buildMasterListDn(const std::string& countryCode, const std::string& fingerprint) {
    return "cn=" + ldap_utils::escapeDnComponent(fingerprint) +
           ",o=ml,c=" + ldap_utils::escapeDnComponent(countryCode) +
           ",dc=data," + config_.ldapBaseDn;
}

// --- LDAP OU Management ---

bool LdapStorageService::ensureCountryOuExists(LDAP* ld, const std::string& countryCode, bool isNcData) {
    std::string dataContainer = isNcData ? config_.ldapNcDataContainer : config_.ldapDataContainer;

    // Ensure data container exists before creating country entry
    std::string dataContainerDn = dataContainer + "," + config_.ldapBaseDn;
    LDAPMessage* dcResult = nullptr;
    int dcRc = ldap_search_ext_s(ld, dataContainerDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                   nullptr, 0, nullptr, nullptr, nullptr, 1, &dcResult);
    if (dcResult) {
        ldap_msgfree(dcResult);
    }

    if (dcRc == LDAP_NO_SUCH_OBJECT) {
        std::string dcValue = isNcData ? "nc-data" : "data";

        LDAPMod dcObjClass;
        dcObjClass.mod_op = LDAP_MOD_ADD;
        dcObjClass.mod_type = const_cast<char*>("objectClass");
        char* dcOcVals[] = {const_cast<char*>("top"), const_cast<char*>("dcObject"),
                            const_cast<char*>("organization"), nullptr};
        dcObjClass.mod_values = dcOcVals;

        LDAPMod dcDc;
        dcDc.mod_op = LDAP_MOD_ADD;
        dcDc.mod_type = const_cast<char*>("dc");
        char* dcVal[] = {const_cast<char*>(dcValue.c_str()), nullptr};
        dcDc.mod_values = dcVal;

        LDAPMod dcO;
        dcO.mod_op = LDAP_MOD_ADD;
        dcO.mod_type = const_cast<char*>("o");
        char* oVal[] = {const_cast<char*>(dcValue.c_str()), nullptr};
        dcO.mod_values = oVal;

        LDAPMod* dcMods[] = {&dcObjClass, &dcDc, &dcO, nullptr};

        int createRc = ldap_add_ext_s(ld, dataContainerDn.c_str(), dcMods, nullptr, nullptr);
        if (createRc != LDAP_SUCCESS && createRc != LDAP_ALREADY_EXISTS) {
            spdlog::warn("Failed to create data container {}: {}", dataContainerDn, ldap_err2string(createRc));
            return false;
        }
        spdlog::info("Created LDAP data container: {}", dataContainerDn);
    }

    std::string countryDn = "c=" + ldap_utils::escapeDnComponent(countryCode) +
                           "," + dataContainer + "," + config_.ldapBaseDn;

    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(ld, countryDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);

    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_SUCCESS) {
        return true;
    }

    if (rc != LDAP_NO_SUCH_OBJECT) {
        spdlog::warn("LDAP search for country {} failed: {}", countryCode, ldap_err2string(rc));
        return false;
    }

    // Create country entry
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {const_cast<char*>("country"), const_cast<char*>("top"), nullptr};
    modObjectClass.mod_values = ocVals;

    LDAPMod modC;
    modC.mod_op = LDAP_MOD_ADD;
    modC.mod_type = const_cast<char*>("c");
    char* cVal[] = {const_cast<char*>(countryCode.c_str()), nullptr};
    modC.mod_values = cVal;

    LDAPMod* mods[] = {&modObjectClass, &modC, nullptr};

    rc = ldap_add_ext_s(ld, countryDn.c_str(), mods, nullptr, nullptr);
    if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
        spdlog::warn("Failed to create country entry {}: {}", countryDn, ldap_err2string(rc));
        return false;
    }

    // Create organizational units under country
    std::vector<std::string> ous = isNcData ? std::vector<std::string>{"dsc"}
                                            : std::vector<std::string>{"csca", "dsc", "lc", "mlsc", "crl"};

    for (const auto& ouName : ous) {
        std::string ouDn = "o=" + ouName + "," + countryDn;

        LDAPMod ouObjClass;
        ouObjClass.mod_op = LDAP_MOD_ADD;
        ouObjClass.mod_type = const_cast<char*>("objectClass");
        char* ouOcVals[] = {const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        ouObjClass.mod_values = ouOcVals;

        LDAPMod ouO;
        ouO.mod_op = LDAP_MOD_ADD;
        ouO.mod_type = const_cast<char*>("o");
        char* ouVal[] = {const_cast<char*>(ouName.c_str()), nullptr};
        ouO.mod_values = ouVal;

        LDAPMod* ouMods[] = {&ouObjClass, &ouO, nullptr};

        rc = ldap_add_ext_s(ld, ouDn.c_str(), ouMods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::debug("OU creation result for {}: {}", ouDn, ldap_err2string(rc));
        }
    }

    return true;
}

bool LdapStorageService::ensureMasterListOuExists(LDAP* ld, const std::string& countryCode) {
    std::string countryDn = "c=" + ldap_utils::escapeDnComponent(countryCode) +
                           ",dc=data," + config_.ldapBaseDn;

    // First ensure country exists
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(ld, countryDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) ldap_msgfree(result);

    if (rc == LDAP_NO_SUCH_OBJECT) {
        LDAPMod modObjectClass;
        modObjectClass.mod_op = LDAP_MOD_ADD;
        modObjectClass.mod_type = const_cast<char*>("objectClass");
        char* ocVals[] = {const_cast<char*>("country"), const_cast<char*>("top"), nullptr};
        modObjectClass.mod_values = ocVals;

        LDAPMod modC;
        modC.mod_op = LDAP_MOD_ADD;
        modC.mod_type = const_cast<char*>("c");
        char* cVal[] = {const_cast<char*>(countryCode.c_str()), nullptr};
        modC.mod_values = cVal;

        LDAPMod* mods[] = {&modObjectClass, &modC, nullptr};
        rc = ldap_add_ext_s(ld, countryDn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::warn("Failed to create country entry for ML {}: {}", countryDn, ldap_err2string(rc));
            return false;
        }
    }

    // Create o=ml OU under country
    std::string mlOuDn = "o=ml," + countryDn;
    result = nullptr;
    rc = ldap_search_ext_s(ld, mlOuDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                            nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) ldap_msgfree(result);

    if (rc == LDAP_NO_SUCH_OBJECT) {
        LDAPMod ouObjClass;
        ouObjClass.mod_op = LDAP_MOD_ADD;
        ouObjClass.mod_type = const_cast<char*>("objectClass");
        char* ouOcVals[] = {const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        ouObjClass.mod_values = ouOcVals;

        LDAPMod ouO;
        ouO.mod_op = LDAP_MOD_ADD;
        ouO.mod_type = const_cast<char*>("o");
        char* ouVal[] = {const_cast<char*>("ml"), nullptr};
        ouO.mod_values = ouVal;

        LDAPMod* ouMods[] = {&ouObjClass, &ouO, nullptr};
        rc = ldap_add_ext_s(ld, mlOuDn.c_str(), ouMods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::debug("ML OU creation result for {}: {}", mlOuDn, ldap_err2string(rc));
        }
    }

    return true;
}

// --- LDAP Storage ---

std::string LdapStorageService::saveCertificateToLdap(LDAP* ld, const std::string& certType,
                                                        const std::string& countryCode,
                                                        const std::string& subjectDn, const std::string& /*issuerDn*/,
                                                        const std::string& serialNumber, const std::string& fingerprint,
                                                        const std::vector<uint8_t>& certBinary,
                                                        const std::string& pkdConformanceCode,
                                                        const std::string& pkdConformanceText,
                                                        const std::string& pkdVersion,
                                                        bool useLegacyDn) {
    bool isNcData = (certType == "DSC_NC");

    if (!ensureCountryOuExists(ld, countryCode, isNcData)) {
        spdlog::warn("Failed to ensure country OU exists for {}", countryCode);
    }

    auto [standardDn_extracted, nonStandardAttrs] = extractStandardAttributes(subjectDn);

    std::string dn;
    if (useLegacyDn) {
        dn = buildCertificateDn(certType, countryCode, subjectDn, serialNumber);
        spdlog::debug("[Legacy DN] Using Subject DN + Serial: {}", dn);
    } else {
        dn = buildCertificateDnV2(fingerprint, certType, countryCode);
        spdlog::debug("[v2 DN] Using Fingerprint-based DN: {}", dn);
    }

    // Build LDAP entry attributes
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("person"),
        const_cast<char*>("organizationalPerson"),
        const_cast<char*>("inetOrgPerson"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    // cn
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    char* cnVals[3];
    if (useLegacyDn) {
        cnVals[0] = const_cast<char*>(standardDn_extracted.c_str());
        cnVals[1] = const_cast<char*>(fingerprint.c_str());
        cnVals[2] = nullptr;
        spdlog::debug("[v2.1.2] Setting cn attribute (Legacy): standardDn + fingerprint");
        if (!nonStandardAttrs.empty()) {
            spdlog::debug("[v1.5.0] Non-standard attributes moved to description: {}", nonStandardAttrs);
        }
    } else {
        cnVals[0] = const_cast<char*>(fingerprint.c_str());
        cnVals[1] = nullptr;
        spdlog::debug("[v2.1.2] Setting cn attribute (v2): fingerprint only");
    }
    modCn.mod_values = cnVals;

    // sn
    LDAPMod modSn;
    modSn.mod_op = LDAP_MOD_ADD;
    modSn.mod_type = const_cast<char*>("sn");
    char* snVals[] = {const_cast<char*>(serialNumber.c_str()), nullptr};
    modSn.mod_values = snVals;

    // description
    std::string descriptionValue;
    if (!nonStandardAttrs.empty()) {
        descriptionValue = "Full Subject DN: " + subjectDn + " | Non-standard attributes: " + nonStandardAttrs + " | Fingerprint: " + fingerprint;
    } else {
        descriptionValue = "Subject DN: " + subjectDn + " | Fingerprint: " + fingerprint;
    }
    LDAPMod modDescription;
    modDescription.mod_op = LDAP_MOD_ADD;
    modDescription.mod_type = const_cast<char*>("description");
    char* descVals[] = {const_cast<char*>(descriptionValue.c_str()), nullptr};
    modDescription.mod_values = descVals;

    // userCertificate;binary
    LDAPMod modCert;
    modCert.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCert.mod_type = const_cast<char*>("userCertificate;binary");
    berval certBv;
    certBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(certBinary.data()));
    certBv.bv_len = certBinary.size();
    berval* certBvVals[] = {&certBv, nullptr};
    modCert.mod_bvalues = certBvVals;

    // DSC_NC specific attributes
    LDAPMod modConformanceCode, modConformanceText, modVersion;
    char* conformanceCodeVals[] = {nullptr, nullptr};
    char* conformanceTextVals[] = {nullptr, nullptr};
    char* versionVals[] = {nullptr, nullptr};

    std::vector<LDAPMod*> modsVec = {&modObjectClass, &modCn, &modSn, &modDescription, &modCert};

    if (isNcData) {
        if (!pkdConformanceCode.empty()) {
            modConformanceCode.mod_op = LDAP_MOD_ADD;
            modConformanceCode.mod_type = const_cast<char*>("pkdConformanceCode");
            conformanceCodeVals[0] = const_cast<char*>(pkdConformanceCode.c_str());
            modConformanceCode.mod_values = conformanceCodeVals;
            modsVec.push_back(&modConformanceCode);
            spdlog::debug("Adding pkdConformanceCode: {}", pkdConformanceCode);
        }

        if (!pkdConformanceText.empty()) {
            modConformanceText.mod_op = LDAP_MOD_ADD;
            modConformanceText.mod_type = const_cast<char*>("pkdConformanceText");
            conformanceTextVals[0] = const_cast<char*>(pkdConformanceText.c_str());
            modConformanceText.mod_values = conformanceTextVals;
            modsVec.push_back(&modConformanceText);
            spdlog::debug("Adding pkdConformanceText: {}", pkdConformanceText.substr(0, 50) + "...");
        }

        if (!pkdVersion.empty()) {
            modVersion.mod_op = LDAP_MOD_ADD;
            modVersion.mod_type = const_cast<char*>("pkdVersion");
            versionVals[0] = const_cast<char*>(pkdVersion.c_str());
            modVersion.mod_values = versionVals;
            modsVec.push_back(&modVersion);
            spdlog::debug("Adding pkdVersion: {}", pkdVersion);
        }
    }

    modsVec.push_back(nullptr);
    LDAPMod** mods = modsVec.data();

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        LDAPMod modCertReplace;
        modCertReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modCertReplace.mod_type = const_cast<char*>("userCertificate;binary");
        modCertReplace.mod_bvalues = certBvVals;

        LDAPMod* replaceMods[] = {&modCertReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save certificate to LDAP {}: {} (error code: {})", dn, ldap_err2string(rc), rc);

        char *matched_msg = nullptr;
        char *error_msg = nullptr;
        int ldap_rc = ldap_get_option(ld, LDAP_OPT_MATCHED_DN, &matched_msg);
        if (ldap_rc == LDAP_SUCCESS && matched_msg) {
            spdlog::warn("  LDAP matched DN: {}", matched_msg);
            ldap_memfree(matched_msg);
        }
        ldap_rc = ldap_get_option(ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, &error_msg);
        if (ldap_rc == LDAP_SUCCESS && error_msg) {
            spdlog::warn("  LDAP diagnostic: {}", error_msg);
            ldap_memfree(error_msg);
        }

        return "";
    }

    spdlog::debug("Saved certificate to LDAP: {}", dn);
    return dn;
}

std::string LdapStorageService::saveCrlToLdap(LDAP* ld, const std::string& countryCode,
                                                const std::string& /*issuerDn*/, const std::string& fingerprint,
                                                const std::vector<uint8_t>& crlBinary) {
    if (!ensureCountryOuExists(ld, countryCode, false)) {
        spdlog::warn("Failed to ensure country OU exists for CRL {}", countryCode);
    }

    std::string dn = buildCrlDn(countryCode, fingerprint);

    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("cRLDistributionPoint"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    std::string cnValue = fingerprint.substr(0, 32);
    char* cnVals[] = {const_cast<char*>(cnValue.c_str()), nullptr};
    modCn.mod_values = cnVals;

    LDAPMod modCrl;
    modCrl.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCrl.mod_type = const_cast<char*>("certificateRevocationList;binary");
    berval crlBv;
    crlBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(crlBinary.data()));
    crlBv.bv_len = crlBinary.size();
    berval* crlBvVals[] = {&crlBv, nullptr};
    modCrl.mod_bvalues = crlBvVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modCrl, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        LDAPMod modCrlReplace;
        modCrlReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modCrlReplace.mod_type = const_cast<char*>("certificateRevocationList;binary");
        modCrlReplace.mod_bvalues = crlBvVals;

        LDAPMod* replaceMods[] = {&modCrlReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save CRL to LDAP {}: {}", dn, ldap_err2string(rc));
        return "";
    }

    spdlog::debug("Saved CRL to LDAP: {}", dn);
    return dn;
}

std::string LdapStorageService::saveMasterListToLdap(LDAP* ld, const std::string& countryCode,
                                                       const std::string& /*signerDn*/, const std::string& fingerprint,
                                                       const std::vector<uint8_t>& mlBinary) {
    if (!ensureMasterListOuExists(ld, countryCode)) {
        spdlog::warn("Failed to ensure ML OU exists for {}", countryCode);
    }

    std::string dn = buildMasterListDn(countryCode, fingerprint);

    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("person"),
        const_cast<char*>("pkdMasterList"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    std::string cnValue = fingerprint.substr(0, 32);
    char* cnVals[] = {const_cast<char*>(cnValue.c_str()), nullptr};
    modCn.mod_values = cnVals;

    LDAPMod modSn;
    modSn.mod_op = LDAP_MOD_ADD;
    modSn.mod_type = const_cast<char*>("sn");
    char* snVals[] = {const_cast<char*>("1"), nullptr};
    modSn.mod_values = snVals;

    LDAPMod modMlContent;
    modMlContent.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modMlContent.mod_type = const_cast<char*>("pkdMasterListContent");
    berval mlBv;
    mlBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(mlBinary.data()));
    mlBv.bv_len = mlBinary.size();
    berval* mlBvVals[] = {&mlBv, nullptr};
    modMlContent.mod_bvalues = mlBvVals;

    LDAPMod modVersion;
    modVersion.mod_op = LDAP_MOD_ADD;
    modVersion.mod_type = const_cast<char*>("pkdVersion");
    char* versionVals[] = {const_cast<char*>("70"), nullptr};
    modVersion.mod_values = versionVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modSn, &modMlContent, &modVersion, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        LDAPMod modMlReplace;
        modMlReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modMlReplace.mod_type = const_cast<char*>("pkdMasterListContent");
        modMlReplace.mod_bvalues = mlBvVals;

        LDAPMod* replaceMods[] = {&modMlReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save Master List to LDAP {}: {}", dn, ldap_err2string(rc));
        return "";
    }

    spdlog::info("Saved Master List to LDAP: {} (country: {})", dn, countryCode);
    return dn;
}

} // namespace services

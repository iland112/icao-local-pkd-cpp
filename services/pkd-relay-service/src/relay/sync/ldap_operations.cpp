#include "ldap_operations.h"
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

namespace icao {
namespace relay {

LdapOperations::LdapOperations(const Config& config)
    : config_(config) {
}

std::string LdapOperations::buildDn(const std::string& certType,
                                     const std::string& countryCode,
                                     const std::string& fingerprint) const {
    // v2.0.3: Use fingerprint-based DN (compatible with PKD Management buildCertificateDnV2)
    // This matches the DN format used by PKD Management for certificate uploads
    std::string ou;
    std::string dataContainer;

    if (certType == "CSCA") {
        ou = "csca";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = config_.ldapDataContainer;
    } else if (certType == "DSC_NC") {
        ou = "dsc";  // Note: PKD Management uses "dsc" for DSC_NC in o= component
        dataContainer = config_.ldapNcDataContainer;
    } else if (certType == "CRL") {
        ou = "crl";
        dataContainer = config_.ldapDataContainer;
    } else {
        return "";
    }

    // Fingerprint is SHA-256 hex (64 chars), no escaping needed
    // Example DN: cn=0a1b2c...,o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
    return "cn=" + fingerprint + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + "," + config_.ldapBaseDn;
}

std::string LdapOperations::certToPem(const std::vector<unsigned char>& certData) {
    if (certData.empty()) return "";

    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";

    const unsigned char* p = certData.data();
    X509* cert = d2i_X509(nullptr, &p, certData.size());
    if (!cert) {
        BIO_free(bio);
        return "";
    }

    PEM_write_bio_X509(bio, cert);

    char* pemData = nullptr;
    long pemLen = BIO_get_mem_data(bio, &pemData);
    std::string result(pemData, pemLen);

    X509_free(cert);
    BIO_free(bio);

    return result;
}

bool LdapOperations::addCertificate(LDAP* ld,
                                   const CertificateInfo& cert,
                                   std::string& errorMsg) const {
    // v2.0.4: Ensure parent DN hierarchy exists (country + organization containers)
    if (!ensureParentDnExists(ld, cert.certType, cert.countryCode, errorMsg)) {
        return false;
    }

    // v2.0.3: Use fingerprint for DN construction
    std::string dn = cert.ldapDn.empty() ?
                     buildDn(cert.certType, cert.countryCode, cert.fingerprint) :
                     cert.ldapDn;

    if (dn.empty()) {
        errorMsg = "Failed to build LDAP DN";
        return false;
    }

    // v2.0.2: Use same LDAP schema as PKD Management for compatibility
    // objectClass: top, person, organizationalPerson, inetOrgPerson, pkdDownload
    // Required attributes: cn (Subject DN), sn (Serial Number), description

    // Build LDAP attributes (compatible with PKD Management)
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

    // cn (Subject DN)
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    char* cnVals[] = {const_cast<char*>(cert.subject.c_str()), nullptr};
    modCn.mod_values = cnVals;

    // sn (required by person) - use certificate ID as serial
    std::string snValue = cert.id;
    LDAPMod modSn;
    modSn.mod_op = LDAP_MOD_ADD;
    modSn.mod_type = const_cast<char*>("sn");
    char* snVals[] = {const_cast<char*>(snValue.c_str()), nullptr};
    modSn.mod_values = snVals;

    // description
    std::string descriptionValue = "Reconciled: " + cert.certType + " | Subject: " + cert.subject + " | ID: " + cert.id;
    LDAPMod modDescription;
    modDescription.mod_op = LDAP_MOD_ADD;
    modDescription.mod_type = const_cast<char*>("description");
    char* descVals[] = {const_cast<char*>(descriptionValue.c_str()), nullptr};
    modDescription.mod_values = descVals;

    // userCertificate;binary (binary certificate data)
    LDAPMod modCert;
    modCert.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCert.mod_type = const_cast<char*>("userCertificate;binary");
    berval certBv;
    certBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(cert.certData.data()));
    certBv.bv_len = cert.certData.size();
    berval* certBvVals[] = {&certBv, nullptr};
    modCert.mod_bvalues = certBvVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modSn, &modDescription, &modCert, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // Entry already exists - this is OK during reconciliation
        spdlog::debug("Certificate already exists in LDAP: {} ({})", dn, cert.subject);
        return true;
    }

    if (rc != LDAP_SUCCESS) {
        errorMsg = "LDAP add failed: " + std::string(ldap_err2string(rc));
        return false;
    }

    spdlog::debug("Added certificate to LDAP: {} ({})", dn, cert.subject);
    return true;
}

bool LdapOperations::deleteCertificate(LDAP* ld,
                                      const std::string& dn,
                                      std::string& errorMsg) const {
    int rc = ldap_delete_ext_s(ld, dn.c_str(), nullptr, nullptr);

    if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT) {
        errorMsg = "LDAP delete failed: " + std::string(ldap_err2string(rc));
        return false;
    }

    spdlog::debug("Deleted certificate from LDAP: {}", dn);
    return true;
}

// v2.0.4: Helper to create LDAP entry if it doesn't exist
bool LdapOperations::createEntryIfNotExists(
    LDAP* ld,
    const std::string& dn,
    const std::vector<std::string>& objectClasses,
    const std::map<std::string, std::string>& attributes) const {

    // Check if entry already exists
    LDAPMessage* searchRes = nullptr;
    const char* attrs[] = {"dn", nullptr};
    struct timeval timeout = {5, 0};

    int rc = ldap_search_ext_s(ld, dn.c_str(), LDAP_SCOPE_BASE,
                               "(objectClass=*)", const_cast<char**>(attrs), 0,
                               nullptr, nullptr, &timeout, 0, &searchRes);

    if (rc == LDAP_SUCCESS) {
        // Entry already exists
        if (searchRes) ldap_msgfree(searchRes);
        spdlog::debug("LDAP entry already exists: {}", dn);
        return true;
    }

    if (rc != LDAP_NO_SUCH_OBJECT) {
        // Some other error
        if (searchRes) ldap_msgfree(searchRes);
        spdlog::warn("Failed to check if LDAP entry exists: {} - {}", dn, ldap_err2string(rc));
        return false;
    }

    if (searchRes) ldap_msgfree(searchRes);

    // Entry does not exist - create it
    std::vector<LDAPMod> mods;
    std::vector<std::vector<char*>> modValues;

    // objectClass
    std::vector<char*> ocVals;
    for (const auto& oc : objectClasses) {
        ocVals.push_back(const_cast<char*>(oc.c_str()));
    }
    ocVals.push_back(nullptr);
    modValues.push_back(ocVals);

    mods.push_back(LDAPMod{LDAP_MOD_ADD, const_cast<char*>("objectClass"), modValues.back().data()});

    // Other attributes
    for (const auto& [key, value] : attributes) {
        std::vector<char*> attrVals;
        attrVals.push_back(const_cast<char*>(value.c_str()));
        attrVals.push_back(nullptr);
        modValues.push_back(attrVals);

        mods.push_back(LDAPMod{LDAP_MOD_ADD, const_cast<char*>(key.c_str()), modValues.back().data()});
    }

    std::vector<LDAPMod*> modPtrs;
    for (auto& mod : mods) {
        modPtrs.push_back(&mod);
    }
    modPtrs.push_back(nullptr);

    rc = ldap_add_ext_s(ld, dn.c_str(), modPtrs.data(), nullptr, nullptr);

    if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
        spdlog::error("Failed to create LDAP entry: {} - {}", dn, ldap_err2string(rc));
        return false;
    }

    spdlog::debug("Created LDAP entry: {}", dn);
    return true;
}

// v2.0.4: Ensure parent DN hierarchy exists
bool LdapOperations::ensureParentDnExists(
    LDAP* ld,
    const std::string& certType,
    const std::string& countryCode,
    std::string& errorMsg) const {

    std::string dataContainer = (certType == "DSC_NC") ?
                                config_.ldapNcDataContainer :
                                config_.ldapDataContainer;

    // 1. Ensure country container: c={COUNTRY},dc=data,dc=download,dc=pkd,...
    std::string countryDn = "c=" + countryCode + "," + dataContainer + "," + config_.ldapBaseDn;

    if (!createEntryIfNotExists(ld, countryDn,
                                {"top", "country"},
                                {{"c", countryCode}})) {
        errorMsg = "Failed to create country container: " + countryDn;
        return false;
    }

    // 2. Ensure organization container: o={csca|dsc|crl},c={COUNTRY},...
    std::string ou;
    if (certType == "CSCA") {
        ou = "csca";
    } else if (certType == "DSC" || certType == "DSC_NC") {
        ou = "dsc";
    } else if (certType == "CRL") {
        ou = "crl";
    } else {
        errorMsg = "Unknown certificate type: " + certType;
        return false;
    }

    std::string orgDn = "o=" + ou + "," + countryDn;

    if (!createEntryIfNotExists(ld, orgDn,
                                {"top", "organization"},
                                {{"o", ou}})) {
        errorMsg = "Failed to create organization container: " + orgDn;
        return false;
    }

    return true;
}

// v2.0.5: Build CRL DN (compatible with PKD Management buildCrlDn)
std::string LdapOperations::buildCrlDn(const std::string& countryCode,
                                       const std::string& fingerprint) const {
    // CRL DN format: cn={FINGERPRINT},o=crl,c={COUNTRY},dc=data,{baseDn}
    return "cn=" + fingerprint + ",o=crl,c=" + countryCode +
           "," + config_.ldapDataContainer + "," + config_.ldapBaseDn;
}

// v2.0.5: Add CRL to LDAP (compatible with PKD Management saveCrlToLdap)
bool LdapOperations::addCrl(LDAP* ld,
                            const CrlInfo& crl,
                            std::string& errorMsg) const {
    // Ensure parent DN hierarchy exists (country + organization containers)
    if (!ensureParentDnExists(ld, "CRL", crl.countryCode, errorMsg)) {
        return false;
    }

    std::string dn = crl.ldapDn.empty() ?
                     buildCrlDn(crl.countryCode, crl.fingerprint) :
                     crl.ldapDn;

    if (dn.empty()) {
        errorMsg = "Failed to build LDAP DN for CRL";
        return false;
    }

    // Build LDAP attributes (compatible with PKD Management)
    // objectClass: top, cRLDistributionPoint, pkdDownload
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

    // cn (using fingerprint substring for compatibility)
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    std::string cnValue = crl.fingerprint.substr(0, 32);  // First 32 chars
    char* cnVals[] = {const_cast<char*>(cnValue.c_str()), nullptr};
    modCn.mod_values = cnVals;

    // certificateRevocationList;binary (binary CRL data)
    LDAPMod modCrl;
    modCrl.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCrl.mod_type = const_cast<char*>("certificateRevocationList;binary");
    berval crlBv;
    crlBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(crl.crlData.data()));
    crlBv.bv_len = crl.crlData.size();
    berval* crlBvVals[] = {&crlBv, nullptr};
    modCrl.mod_bvalues = crlBvVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modCrl, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // Entry already exists - this is OK during reconciliation
        spdlog::debug("CRL already exists in LDAP: {}", dn);
        return true;
    }

    if (rc != LDAP_SUCCESS) {
        errorMsg = "LDAP add CRL failed: " + std::string(ldap_err2string(rc));
        return false;
    }

    spdlog::debug("Added CRL to LDAP: {}", dn);
    return true;
}

} // namespace relay
} // namespace icao

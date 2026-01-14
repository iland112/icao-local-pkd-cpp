#include "ldap_operations.h"
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

namespace icao {
namespace sync {

LdapOperations::LdapOperations(const Config& config)
    : config_(config) {
}

std::string LdapOperations::buildDn(const std::string& certType,
                                     const std::string& countryCode,
                                    int certId) const {
    std::string org;
    std::string dc = "dc=data";

    if (certType == "CSCA") {
        org = "o=csca";
    } else if (certType == "DSC") {
        org = "o=dsc";
    } else if (certType == "DSC_NC") {
        org = "o=dsc";
        dc = "dc=nc-data";
    } else if (certType == "CRL") {
        org = "o=crl";
    } else {
        return "";
    }

    return "cn=cert-" + std::to_string(certId) + "," + org + ",c=" + countryCode + "," +
           dc + ",dc=download,dc=pkd," + config_.ldapBaseDn;
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
    std::string dn = cert.ldapDn.empty() ?
                     buildDn(cert.certType, cert.countryCode, cert.id) :
                     cert.ldapDn;

    if (dn.empty()) {
        errorMsg = "Failed to build LDAP DN";
        return false;
    }

    std::string pemData = certToPem(cert.certData);
    if (pemData.empty()) {
        errorMsg = "Failed to convert certificate to PEM format";
        return false;
    }

    // Build LDAP attributes
    std::vector<std::string> objectClasses;
    if (cert.certType == "CSCA") {
        objectClasses = {"top", "cscaCertificateObject"};
    } else {
        objectClasses = {"top", "pkiCertificate"};
    }

    std::vector<const char*> ocValues;
    for (const auto& oc : objectClasses) {
        ocValues.push_back(oc.c_str());
    }
    ocValues.push_back(nullptr);

    berval certValue;
    certValue.bv_len = static_cast<ber_len_t>(pemData.size());
    certValue.bv_val = const_cast<char*>(pemData.c_str());
    std::vector<berval> certValues;
    certValues.push_back(certValue);
    std::vector<berval*> certValuePtrs = {&certValues[0], nullptr};

    std::string cnValue = "cert-" + std::to_string(cert.id);
    std::vector<const char*> cnValues = {cnValue.c_str(), nullptr};

    LDAPMod mod_oc = {LDAP_MOD_ADD,
                     const_cast<char*>("objectClass"),
                     {const_cast<char**>(ocValues.data())}};

    LDAPMod mod_cert = {LDAP_MOD_ADD | LDAP_MOD_BVALUES,
                       const_cast<char*>("userCertificate;binary"),
                       {.modv_bvals = certValuePtrs.data()}};

    LDAPMod mod_cn = {LDAP_MOD_ADD,
                     const_cast<char*>("cn"),
                     {const_cast<char**>(cnValues.data())}};

    std::vector<LDAPMod*> mods = {&mod_oc, &mod_cert, &mod_cn, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods.data(), nullptr, nullptr);

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

} // namespace sync
} // namespace icao

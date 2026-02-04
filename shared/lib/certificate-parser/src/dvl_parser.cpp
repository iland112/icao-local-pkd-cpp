#include "dvl_parser.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <cstring>
#include <algorithm>

namespace icao {
namespace certificate_parser {

// ICAO DVL OID: 2.23.136.1.1.7
static const unsigned char DVL_OID_BYTES[] = {
    0x06, 0x06, 0x67, 0x81, 0x08, 0x01, 0x01, 0x07
};

DvlParseResult DvlParser::parse(const std::vector<uint8_t>& data) {
    DvlParseResult result;

    if (data.empty()) {
        result.errorMessage = "Empty data";
        return result;
    }

    // Create BIO from data
    BIO* bio = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    if (!bio) {
        result.errorMessage = "Failed to create BIO";
        return result;
    }

    // Parse PKCS#7 structure
    PKCS7* p7 = d2i_PKCS7_bio(bio, nullptr);
    BIO_free(bio);

    if (!p7) {
        result.errorMessage = "Failed to parse PKCS#7 structure";
        return result;
    }

    // Verify it's SignedData
    if (!PKCS7_type_is_signed(p7)) {
        PKCS7_free(p7);
        result.errorMessage = "Not a PKCS#7 SignedData structure";
        return result;
    }

    // Verify DVL OID is present
    if (!containsDvlOid(data)) {
        PKCS7_free(p7);
        result.errorMessage = "DVL OID (2.23.136.1.1.7) not found";
        return result;
    }

    // Extract signer certificate
    result.signerCertificate = extractSignerCertificate(p7);
    if (!result.signerCertificate) {
        PKCS7_free(p7);
        result.errorMessage = "Failed to extract signer certificate";
        return result;
    }

    // Extract metadata from signer certificate
    result.issuerCountry = getCountryFromCert(result.signerCertificate);
    std::string org = getOrganizationFromCert(result.signerCertificate);
    if (!org.empty()) {
        result.issuerOrg = org;
    }

    // Verify signature
    result.signatureVerified = verifySignature(p7, result.signerCertificate);

    // Extract deviation entries
    result.deviations = extractDeviations(p7);

    // Extract embedded certificates
    result.certificates = extractCertificates(p7);

    // Set DVL version (default to 1.0 if not specified)
    result.version = "1.0";

    result.success = true;
    PKCS7_free(p7);

    return result;
}

bool DvlParser::verifySignature(PKCS7* p7, X509* signerCert) {
    if (!p7 || !signerCert) {
        return false;
    }

    // Create certificate store with signer cert
    X509_STORE* store = X509_STORE_new();
    if (!store) {
        return false;
    }

    X509_STORE_add_cert(store, signerCert);

    // Create BIO for content
    BIO* contentBio = BIO_new(BIO_s_mem());
    if (!contentBio) {
        X509_STORE_free(store);
        return false;
    }

    // Verify signature
    int verify_result = PKCS7_verify(p7, nullptr, store, nullptr, contentBio, 0);

    BIO_free(contentBio);
    X509_STORE_free(store);

    return verify_result == 1;
}

std::vector<DeviationEntry> DvlParser::extractDeviations(PKCS7* p7) {
    std::vector<DeviationEntry> deviations;

    if (!p7 || !PKCS7_type_is_signed(p7)) {
        return deviations;
    }

    // Get SignedData content
    PKCS7_SIGNED* signedData = p7->d.sign;
    if (!signedData || !signedData->contents) {
        return deviations;
    }

    // Get content data
    ASN1_OCTET_STRING* content = signedData->contents->d.data;
    if (!content) {
        return deviations;
    }

    // Parse content as ASN.1 structure
    // DVL content structure varies by country
    // For now, we return empty list (to be enhanced based on specific DVL format)

    // TODO: Parse actual deviation entries from ASN.1 structure
    // This requires knowledge of the specific DVL content schema
    // which may vary by issuing country

    return deviations;
}

bool DvlParser::containsDvlOid(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(DVL_OID_BYTES)) {
        return false;
    }

    // Search for DVL OID bytes in data
    auto it = std::search(
        data.begin(), data.end(),
        DVL_OID_BYTES, DVL_OID_BYTES + sizeof(DVL_OID_BYTES)
    );

    return it != data.end();
}

X509* DvlParser::extractSignerCertificate(PKCS7* p7) {
    if (!p7 || !PKCS7_type_is_signed(p7)) {
        return nullptr;
    }

    // Get signer info
    STACK_OF(PKCS7_SIGNER_INFO)* signerInfos = PKCS7_get_signer_info(p7);
    if (!signerInfos || sk_PKCS7_SIGNER_INFO_num(signerInfos) == 0) {
        return nullptr;
    }

    // Get first signer info
    PKCS7_SIGNER_INFO* signerInfo = sk_PKCS7_SIGNER_INFO_value(signerInfos, 0);
    if (!signerInfo) {
        return nullptr;
    }

    // Get certificates from PKCS#7
    STACK_OF(X509)* certs = PKCS7_get0_signers(p7, nullptr, 0);
    if (!certs || sk_X509_num(certs) == 0) {
        return nullptr;
    }

    // Get first certificate (signer)
    X509* signerCert = sk_X509_value(certs, 0);
    if (!signerCert) {
        sk_X509_free(certs);
        return nullptr;
    }

    // Duplicate certificate for ownership transfer
    X509* result = X509_dup(signerCert);
    sk_X509_free(certs);

    return result;
}

std::vector<X509*> DvlParser::extractCertificates(PKCS7* p7) {
    std::vector<X509*> certificates;

    if (!p7 || !PKCS7_type_is_signed(p7)) {
        return certificates;
    }

    // Get certificates from PKCS#7
    STACK_OF(X509)* certs = p7->d.sign->cert;
    if (!certs) {
        return certificates;
    }

    // Extract all certificates (excluding signer - index 0)
    int certCount = sk_X509_num(certs);
    for (int i = 1; i < certCount; i++) {
        X509* cert = sk_X509_value(certs, i);
        if (cert) {
            // Duplicate certificate for ownership transfer
            X509* dupCert = X509_dup(cert);
            if (dupCert) {
                certificates.push_back(dupCert);
            }
        }
    }

    return certificates;
}

std::string DvlParser::getCountryFromCert(X509* cert) {
    if (!cert) {
        return "";
    }

    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) {
        return "";
    }

    // Find country (C) entry
    int pos = X509_NAME_get_index_by_NID(subject, NID_countryName, -1);
    if (pos < 0) {
        return "";
    }

    X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, pos);
    if (!entry) {
        return "";
    }

    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) {
        return "";
    }

    const unsigned char* str = ASN1_STRING_get0_data(data);
    if (!str) {
        return "";
    }

    return std::string(reinterpret_cast<const char*>(str), ASN1_STRING_length(data));
}

std::string DvlParser::getOrganizationFromCert(X509* cert) {
    if (!cert) {
        return "";
    }

    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) {
        return "";
    }

    // Find organization (O) entry
    int pos = X509_NAME_get_index_by_NID(subject, NID_organizationName, -1);
    if (pos < 0) {
        return "";
    }

    X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, pos);
    if (!entry) {
        return "";
    }

    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) {
        return "";
    }

    const unsigned char* str = ASN1_STRING_get0_data(data);
    if (!str) {
        return "";
    }

    return std::string(reinterpret_cast<const char*>(str), ASN1_STRING_length(data));
}

} // namespace certificate_parser
} // namespace icao

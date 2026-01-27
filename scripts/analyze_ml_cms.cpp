#include <iostream>
#include <fstream>
#include <vector>
#include <openssl/cms.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/err.h>

void printCertInfo(const char* label, X509* cert) {
    char subjectBuf[512];
    char issuerBuf[512];

    X509_NAME_oneline(X509_get_subject_name(cert), subjectBuf, sizeof(subjectBuf));
    X509_NAME_oneline(X509_get_issuer_name(cert), issuerBuf, sizeof(issuerBuf));

    bool isSelfSigned = (strcmp(subjectBuf, issuerBuf) == 0);

    std::cout << "\n" << label << ":\n";
    std::cout << "  Subject: " << subjectBuf << "\n";
    std::cout << "  Issuer:  " << issuerBuf << "\n";
    std::cout << "  Self-signed: " << (isSelfSigned ? "YES" : "NO") << "\n";

    // Check key usage
    ASN1_BIT_STRING* keyUsage = (ASN1_BIT_STRING*)X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr);
    if (keyUsage) {
        std::cout << "  Key Usage: ";
        if (ASN1_BIT_STRING_get_bit(keyUsage, 0)) std::cout << "digitalSignature ";
        if (ASN1_BIT_STRING_get_bit(keyUsage, 1)) std::cout << "nonRepudiation ";
        if (ASN1_BIT_STRING_get_bit(keyUsage, 2)) std::cout << "keyEncipherment ";
        if (ASN1_BIT_STRING_get_bit(keyUsage, 5)) std::cout << "keyCertSign ";
        if (ASN1_BIT_STRING_get_bit(keyUsage, 6)) std::cout << "cRLSign ";
        std::cout << "\n";
        ASN1_BIT_STRING_free(keyUsage);
    }

    // Check basic constraints
    BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr);
    if (bc) {
        std::cout << "  Is CA: " << (bc->ca ? "YES" : "NO") << "\n";
        BASIC_CONSTRAINTS_free(bc);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <master_list.cms>\n";
        return 1;
    }

    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    // Read file
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << argv[1] << "\n";
        return 1;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    // Parse CMS
    BIO* bio = BIO_new_mem_buf(data.data(), data.size());
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        std::cerr << "Failed to parse CMS structure\n";
        return 1;
    }

    std::cout << "=== Master List CMS Analysis ===\n";
    std::cout << "File: " << argv[1] << "\n";
    std::cout << "Size: " << data.size() << " bytes\n";

    // Get SignerInfo certificates
    STACK_OF(CMS_SignerInfo)* signers = CMS_get0_SignerInfos(cms);
    if (signers) {
        int numSigners = sk_CMS_SignerInfo_num(signers);
        std::cout << "\n--- SignerInfo Certificates (" << numSigners << ") ---\n";
        std::cout << "These are the ACTUAL Master List Signer Certificates (MLSC)\n";

        for (int i = 0; i < numSigners; i++) {
            CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signers, i);
            X509* cert = nullptr;

            // Get the signer certificate
            CMS_SignerInfo_get0_algs(si, nullptr, &cert, nullptr, nullptr);

            if (cert) {
                char label[64];
                snprintf(label, sizeof(label), "SignerInfo #%d (MLSC)", i + 1);
                printCertInfo(label, cert);
            }
        }
    }

    // Get certificates from pkiData
    STACK_OF(X509)* certs = CMS_get1_certs(cms);
    if (certs) {
        int numCerts = sk_X509_num(certs);
        std::cout << "\n--- pkiData Certificates (" << numCerts << ") ---\n";
        std::cout << "These are CSCA and Link Certificates\n";

        for (int i = 0; i < numCerts && i < 5; i++) {  // Show first 5
            X509* cert = sk_X509_value(certs, i);
            if (cert) {
                char label[64];
                snprintf(label, sizeof(label), "pkiData #%d", i + 1);
                printCertInfo(label, cert);
            }
        }

        if (numCerts > 5) {
            std::cout << "\n... and " << (numCerts - 5) << " more certificates in pkiData\n";
        }

        sk_X509_pop_free(certs, X509_free);
    }

    CMS_ContentInfo_free(cms);

    std::cout << "\n=== Analysis Complete ===\n";
    return 0;
}

#!/usr/bin/env python3
"""
Analyze ICAO Master List CMS structure to distinguish:
- MLSC (Master List Signer Certificates) in SignerInfo
- CSCA and Link Certificates in pkiData
"""

import sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.backends import default_backend
from cryptography.x509.oid import ExtensionOID, NameOID
from asn1crypto import cms, core

def print_cert_info(label, cert):
    """Print certificate information"""
    subject = cert.subject.rfc4514_string()
    issuer = cert.issuer.rfc4514_string()
    is_self_signed = (subject == issuer)

    print(f"\n{label}:")
    print(f"  Subject: {subject}")
    print(f"  Issuer:  {issuer}")
    print(f"  Self-signed: {'YES' if is_self_signed else 'NO'}")

    # Check key usage
    try:
        key_usage = cert.extensions.get_extension_for_oid(ExtensionOID.KEY_USAGE).value
        usages = []
        if key_usage.digital_signature: usages.append("digitalSignature")
        if key_usage.key_cert_sign: usages.append("keyCertSign")
        if key_usage.crl_sign: usages.append("cRLSign")
        if usages:
            print(f"  Key Usage: {', '.join(usages)}")
    except x509.ExtensionNotFound:
        pass

    # Check basic constraints
    try:
        basic_constraints = cert.extensions.get_extension_for_oid(ExtensionOID.BASIC_CONSTRAINTS).value
        print(f"  Is CA: {'YES' if basic_constraints.ca else 'NO'}")
    except x509.ExtensionNotFound:
        pass

def analyze_master_list(file_path):
    """Analyze Master List CMS structure"""
    print("=== Master List CMS Analysis ===")
    print(f"File: {file_path}")

    with open(file_path, 'rb') as f:
        cms_data = f.read()

    print(f"Size: {len(cms_data)} bytes")

    # Parse CMS using asn1crypto
    content_info = cms.ContentInfo.load(cms_data)
    signed_data = content_info['content']

    # Get SignerInfo certificates
    signer_infos = signed_data['signer_infos']
    print(f"\n--- SignerInfo Certificates ({len(signer_infos)}) ---")
    print("These are the ACTUAL Master List Signer Certificates (MLSC)")

    # SignerInfo doesn't directly contain the certificate
    # We need to match it from the certificates collection

    # Get all certificates from the CMS
    certificates = signed_data['certificates']
    print(f"\n--- Total Certificates in CMS: {len(certificates)} ---")

    # Parse all certificates
    cert_list = []
    for i, cert_choice in enumerate(certificates):
        cert_der = cert_choice.chosen.dump()
        cert = x509.load_der_x509_certificate(cert_der, default_backend())
        cert_list.append(cert)

    # Analyze SignerInfo to find which certificates are signers
    signer_cert_serials = []
    for i, signer_info in enumerate(signer_infos):
        sid = signer_info['sid']
        if sid.name == 'issuer_and_serial_number':
            serial = sid.chosen['serial_number'].native
            signer_cert_serials.append(serial)
            print(f"\nSignerInfo #{i+1} uses certificate with serial: {hex(serial)}")

    # Match signer certificates
    print("\n--- MLSC (Master List Signer Certificates) ---")
    mlsc_certs = []
    for cert in cert_list:
        if cert.serial_number in signer_cert_serials:
            mlsc_certs.append(cert)
            print_cert_info(f"MLSC (Serial: {hex(cert.serial_number)})", cert)

    # Other certificates are CSCA/Link
    print(f"\n--- CSCA and Link Certificates ({len(cert_list) - len(mlsc_certs)}) ---")
    count = 0
    for cert in cert_list:
        if cert.serial_number not in signer_cert_serials:
            count += 1
            if count <= 5:  # Show first 5
                subject = cert.subject.rfc4514_string()
                issuer = cert.issuer.rfc4514_string()
                is_link = (subject != issuer)
                cert_type = "Link Certificate" if is_link else "Self-signed CSCA"
                print_cert_info(f"pkiData #{count} ({cert_type})", cert)

    if count > 5:
        print(f"\n... and {count - 5} more CSCA/Link certificates")

    print("\n=== Analysis Complete ===")
    print("\n=== Summary ===")
    print(f"Total SignerInfo (MLSC): {len(mlsc_certs)}")
    print(f"Total CSCA/Link Certificates: {len(cert_list) - len(mlsc_certs)}")

    return mlsc_certs, [c for c in cert_list if c.serial_number not in signer_cert_serials]

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_ml_cms.py <master_list.cms>")
        sys.exit(1)

    analyze_master_list(sys.argv[1])

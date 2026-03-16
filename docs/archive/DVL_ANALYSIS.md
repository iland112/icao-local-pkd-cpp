# Deviation List (DVL) File Analysis

**Date**: 2026-02-04
**Sample File**: `data/uploads/20181106_DEDeviationList.dvl` (Germany BSI)
**File Size**: 2.7 KB

---

## Executive Summary

Deviation Lists (DVL) are standardized data structures used by ICAO member states to inform other countries about travel documents that deviate from ICAO Doc 9303 specifications. The DVL file format is based on CMS (Cryptographic Message Syntax) SignedData structure with ICAO-specific OID.

---

## File Format Analysis

### 1. Container Format

**Type**: DER-encoded PKCS#7 / CMS SignedData
**Standard**: RFC 3852 (Cryptographic Message Syntax)

```
file data/uploads/20181106_DEDeviationList.dvl
→ DER Encoded PKCS#7 Signed Data
```

### 2. ASN.1 Structure

```
SEQUENCE {
  contentType: pkcs7-signedData (1.2.840.113549.1.7.2)
  content [0] {
    SEQUENCE {
      version: INTEGER (3)
      digestAlgorithms: SET {
        SEQUENCE {
          algorithm: sha1 (1.3.14.3.2.26)
          parameter: NULL
        }
      }
      encapContentInfo: SEQUENCE {
        eContentType: OBJECT IDENTIFIER (2.23.136.1.1.7)  ← ICAO deviationList
        eContent [0]: OCTET STRING { ... }                 ← Deviation data
      }
      certificates [0]: SEQUENCE { ... }                   ← Signer certificate
      signerInfos: SET { ... }                             ← Digital signature
    }
  }
}
```

### 3. ICAO Deviation List OID

**OID**: `2.23.136.1.1.7`
**Full Path**: `{joint-iso-itu-t(2) international-organizations(23) icao(136) mrtd(1) security(1) deviationList(7)}`

**Related OIDs**:
- `2.23.136.1.1.2` - cscaMasterList (CSCA Master List)
- `2.23.136.1.1.6` - defectList (Defect List)
- `2.23.136.1.1.7` - deviationList (Deviation List)

---

## Deviation List Content Structure

### Sample Deviation Entry (Germany)

**Affected Certificate**:
```
Subject DN: C=DE, O=bund, OU=bsi, serialNumber=013, CN=csca-germany
Serial Number: 0x0142 (322)
```

**Deviation Description**:
```
"Country name is encoded as UTF-8 instead of Printable String."
```

**Deviation Code OID**:
```
OID: 2.23.136.1.1.7.1.2
```

**Binary Content** (192 bytes):
```
30 81 BD 02 01 00 30 07 06 05 2B 0E 03 02 1A 31 81 AE 30 81 AB 30 57 A1 55
30 4F 31 0B 30 09 06 03 55 04 06 13 02 44 45 31 0D 30 0B 06 03 55 04 0A 0C
04 62 75 6E 64 31 0C 30 0A 06 03 55 04 0B 0C 03 62 73 69 31 0C 30 0A 06 03
55 04 05 13 03 30 31 33 31 15 30 13 06 03 55 04 03 0C 0C 63 73 63 61 2D 67
65 72 6D 61 6E 79 02 02 01 42 31 50 30 4E 13 3D 43 6F 75 6E 74 72 79 20 6E
61 6D 65 20 69 73 20 65 6E 63 6F 64 65 64 20 61 73 20 55 54 46 2D 38 20 69
6E 73 74 65 61 64 20 6F 66 20 50 72 69 6E 74 61 62 6C 65 20 53 74 72 69 6E
67 2E 06 08 67 81 08 01 01 07 01 02 A0 03 02 01 00
```

---

## Signer Certificate

**Purpose**: Digitally signs the Deviation List to ensure authenticity and integrity

**Certificate Details**:
```
Subject: C=DE, O=bund, OU=bsi, serialNumber=0001, CN=CSCA Deviation List Signer
Issuer: C=DE, O=bund, OU=bsi, serialNumber=103, CN=csca-germany
Serial: 0x0417 (1047)
Validity:
  Not Before: Jul 12 05:49:59 2018 GMT
  Not After: Jul 12 23:59:59 2022 GMT
Public Key: ECC (Elliptic Curve) - brainpoolP384r1 curve
Signature Algorithm: ecdsa-with-SHA384
```

**Key Usage**:
- Digital Signature (bit 0)
- Non-Repudiation (bit 1)

**Extended Key Usage**:
- Certificate Policy: 0.4.0.127.0.7.3.1.1.1 (ICAO MRTD policy)

---

## ICAO Doc 9303 Standards Reference

### Documentation Sources

1. **ICAO Doc 9303 Part 12**: Public Key Infrastructure for MRTDs
   - Defines PKI data structures including Deviation Lists
   - Specifies OID namespace (2.23.136.x.x.x)

2. **Technical Report**: "Travel Document Deviation List Issuance"
   - URL: https://www.icao.int/Meetings/TAG-MRTD/TagMrtd22/TAG-MRTD-22_WP04.pdf
   - Incorporated into 7th Edition of Doc 9303

3. **ICAO PKD Regulations** (July 2020)
   - URL: https://www.icao.int/sites/default/files/2025-06/ICAO-PKD-Regulations_Version_July2020.pdf
   - Defines distribution mechanisms

### Key Principles

**Purpose**:
> "Deviations are defined as MRTDs that contain elements that do not precisely conform to the ICAO specifications and the governing ISO and RFC standards."

**Distribution**:
- ICAO PKD (Public Key Directory)
- National LDAP servers
- Website downloads (e.g., BSI Germany)

**Scope**:
> "Deviation Lists provide a means of reporting deviations affecting thousands of travel documents rather than a few or a few hundred."

---

## Use Cases in PKD System

### 1. Deviation Awareness
- Immigration officers can check if a passport's certificate has known deviations
- Automated systems can adjust validation logic based on published deviations

### 2. Trust Chain Validation
- DVL helps distinguish between actual security issues vs. known deviations
- Reduces false positives in certificate validation

### 3. Interoperability
- States inform each other about non-standard implementations
- Facilitates cross-border document verification

---

## Implementation Requirements

### Parser Requirements

1. **CMS SignedData Parsing**
   - Verify signature using signer certificate
   - Extract encapsulated content (OID 2.23.136.1.1.7)

2. **Deviation Entry Parsing**
   - Extract affected certificate DN
   - Extract serial number
   - Parse deviation description (UTF-8 string)
   - Parse deviation code OID

3. **Certificate Chain Validation**
   - Verify signer certificate against CSCA
   - Check certificate validity period
   - Validate signature algorithm

### Storage Requirements

**Database Schema**:
```sql
CREATE TABLE deviation_list (
    id UUID PRIMARY KEY,
    country_code VARCHAR(2) NOT NULL,
    csca_subject_dn VARCHAR(500) NOT NULL,
    csca_serial_number VARCHAR(100) NOT NULL,
    deviation_description TEXT NOT NULL,
    deviation_code_oid VARCHAR(100),
    signer_certificate_fingerprint VARCHAR(64),
    issued_date TIMESTAMP,
    valid_until TIMESTAMP,
    uploaded_at TIMESTAMP DEFAULT NOW()
);
```

**LDAP Schema**:
```
dn: cn={fingerprint},o=dvl,c={COUNTRY},dc=data,dc=download,dc=pkd,...
objectClass: pkdDeviation
cn: {fingerprint}
cscaSubjectDN: C=DE,O=bund,OU=bsi,serialNumber=013,CN=csca-germany
cscaSerialNumber: 0142
deviationDescription: Country name is encoded as UTF-8 instead of Printable String.
deviationCodeOID: 2.23.136.1.1.7.1.2
signerCertificate: {DER-encoded certificate}
issuedDate: 20181106000000Z
```

---

## Related File Types Comparison

| Type | OID | Purpose | File Ext | Frequency |
|------|-----|---------|----------|-----------|
| CSCA Master List | 2.23.136.1.1.2 | CSCA certificates | .ml | Weekly |
| Defect List | 2.23.136.1.1.6 | Compromised certs | .dfl | As needed |
| Deviation List | 2.23.136.1.1.7 | Non-conforming MRTDs | .dvl | As needed |
| CRL | 2.5.4.38 | Revoked certificates | .crl | Daily/Weekly |

---

## References

### Standards Documents
- [ICAO Doc 9303 Part 12 - PKI for MRTDs](https://store.icao.int/en/machine-readable-travel-documents-part-12-public-key-infrastructure-for-mrtds-doc-9303-12)
- [RFC 3852 - Cryptographic Message Syntax (CMS)](https://datatracker.ietf.org/doc/html/rfc3852)
- [RFC 5280 - X.509 Certificate and CRL Profile](https://datatracker.ietf.org/doc/html/rfc5280)

### Technical Resources
- [OID Repository: 2.23.136.1.1.7](https://oid-base.com/get/2.23.136.1.1.7)
- [ICAO Technical Report: DVL Issuance](https://www.icao.int/Meetings/TAG-MRTD/TagMrtd22/TAG-MRTD-22_WP04.pdf)
- [ZeroPass ICAO 9303 Overview](https://github.com/ZeroPass/Port-documentation-and-tools/blob/master/Overview%20of%20ICAO%209303.md)

### Sample Data Sources
- [Germany BSI CSCA](https://www.bsi.bund.de/csca)
- [ICAO PKD Portal](https://www.icao.int/security/mrtd/pages/icao-public-key-directory.aspx)

---

## Next Steps

1. ✅ Understand DVL file structure
2. ✅ Document ICAO standards
3. 🔄 Design DVL parser library
4. 🔄 Implement CMS SignedData verification
5. 🔄 Create database schema for deviation storage
6. 🔄 Integrate with upload workflow
7. 🔄 Add frontend visualization for deviations

---

**Analysis Completed**: 2026-02-04
**Analyst**: Claude Sonnet 4.5
**Status**: ✅ Complete

# PKD Certificate Search & Export Feature Design

**Version**: 1.0.0
**Created**: 2026-01-14
**Updated**: 2026-01-14
**Status**: Backend Implementation Complete (Clean Architecture)

---

## 📋 Table of Contents

1. [Overview](#overview)
2. [LDAP Data Structure Analysis](#ldap-data-structure-analysis)
3. [Export Feature Requirements](#export-feature-requirements)
4. [Backend API Design](#backend-api-design)
5. [Frontend UI Design](#frontend-ui-design)
6. [Technical Implementation Details](#technical-implementation-details)
7. [File Naming Conventions](#file-naming-conventions)
8. [Implementation Tasks](#implementation-tasks)

---

## Overview

### Purpose
Provide users with the ability to search, view, and export PKD certificate data stored in LDAP.

### Key Features
- **Search & Filter**: Search certificates by country, type, validity, and keywords
- **Individual Export**: Download single certificate/CRL/ML in DER or PEM format
- **Bulk Export**: Download all certificates from a country as ZIP archive
- **Certificate Details**: View detailed X.509 certificate information

### Data Source
All data is retrieved from **LDAP** (not PostgreSQL database).

---

## LDAP Data Structure Analysis

### DIT (Directory Information Tree) Structure

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=pkd
    └── dc=download
        ├── dc=data                    # Standard certificates
        │   └── c={COUNTRY}
        │       ├── o=csca    (CSCA certificates)
        │       ├── o=dsc     (DSC certificates)
        │       ├── o=crl     (CRLs)
        │       └── o=ml      (Master Lists)
        └── dc=nc-data                 # Non-conformant
            └── c={COUNTRY}
                └── o=dsc     (DSC_NC)
```

### Certificate Entry Attributes

#### CSCA/DSC/DSC_NC (X.509 Certificates)
```ldif
dn: cn=CN\3DUAE CSCA 01\2COU\3DEPASS\2CO\3DMOI\2CC\3DAE+sn=37,o=csca,c=AE,dc=data,...
objectClass: top
objectClass: person
objectClass: organizationalPerson
objectClass: inetOrgPerson
objectClass: pkdDownload
cn: CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE
sn: 37
description: Subject DN: CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE | Fingerprint: 0f15d0e5...
userCertificate;binary:: MIIC4TCCAoigAwIBAgIBNzAKBggq...  # DER-encoded X.509
```

#### CRL (Certificate Revocation List)
```ldif
dn: cn=74bafbc64489608b40a9fd05f944dad4...,o=crl,c=QA,dc=data,...
objectClass: top
objectClass: cRLDistributionPoint
cn: 74bafbc64489608b40a9fd05f944dad4b572dc4e0d7392bebed449ee6524692c
certificateRevocationList;binary:: MIICrDCBlQIBATANBgkq...  # DER-encoded CRL
```

#### Master List (Signed CMS)
```ldif
dn: cn=367223396948683d3dbed0b2ac12f8d1a38ca91f...,o=ml,c=FR,dc=data,...
objectClass: top
objectClass: person
objectClass: pkdMasterList
objectClass: pkdDownload
cn: 367223396948683d3dbed0b2ac12f8d1a38ca91f8485821c9a20a4ed4a0baa7d
sn: 1
pkdMasterListContent:: MIMB1mIGCSqGSIb3DQEHAqCDAdZS...  # DER-encoded CMS
```

### Statistics
- **Total LDAP Entries**: 31,155
- **Certificate Types**: CSCA, DSC, DSC_NC, CRL, ML

---

## Export Feature Requirements

### 1. Individual File Export (Per Table Row)

#### Use Cases
- User clicks a row in the search results table
- User views certificate details dialog
- User selects export format: **DER** (binary) or **PEM** (text)

#### File Format Support

| Certificate Type | DER Extension | PEM Extension | Binary Attribute |
|------------------|---------------|---------------|------------------|
| CSCA/DSC/DSC_NC  | `.der`        | `.crt`        | `userCertificate;binary` |
| CRL              | `.der`        | `.crl`        | `certificateRevocationList;binary` |
| Master List      | `.der`        | `.p7s`        | `pkdMasterListContent` |

#### File Naming Convention
```
{COUNTRY}_{TYPE}_{SERIAL}.{EXTENSION}

Examples:
  AE_CSCA_37.der          # DER format X.509
  AE_CSCA_37.crt          # PEM format X.509
  QA_CRL_74bafbc6.crl     # PEM format CRL
  FR_ML_36722339.p7s      # PEM format Signed CMS
```

### 2. Bulk Export by Country (ZIP Archive)

#### Use Case
- User selects a country from filter dropdown
- User clicks "Export Country (ZIP)" button
- All certificates/CRLs/MLs for that country are downloaded as ZIP

#### ZIP Archive Structure
```
{COUNTRY}_PKD_{YYYYMMDD}.zip
├── csca/
│   ├── AE_CSCA_37.crt
│   ├── AE_CSCA_02.crt
│   └── ...
├── dsc/
│   ├── AE_DSC_*.crt
│   └── ...
├── dsc_nc/
│   └── ...
├── crl/
│   ├── AE_CRL_74bafbc6.crl
│   └── ...
└── ml/
    ├── AE_ML_36722339.p7s
    └── ...
```

Example: `AE_PKD_20260114.zip`

---

## Backend API Design

### Base URL
All APIs are accessible through API Gateway:
```
http://localhost:8080/api
```

---

### 1. Certificate Search API

**Endpoint**: `GET /api/certificates/search`

**Description**: Search and filter certificates from LDAP with pagination.

**Query Parameters**:
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `country` | string | No | - | ISO 3166-1 alpha-2 code (e.g., AE, DE, FR) |
| `certType` | string | No | - | Certificate type: `csca`, `dsc`, `dsc_nc`, `crl`, `ml` |
| `validity` | string | No | `all` | Validity status: `valid`, `expired`, `all` |
| `searchTerm` | string | No | - | Search keyword (CN, Subject) |
| `limit` | integer | No | 50 | Page size (max: 200) |
| `offset` | integer | No | 0 | Page offset |

**Response**:
```json
{
  "success": true,
  "total": 1234,
  "limit": 50,
  "offset": 0,
  "certificates": [
    {
      "dn": "cn=CN\\3DUAE CSCA 01\\2COU\\3DEPASS\\2CO\\3DMOI\\2CC\\3DAE+sn=37,o=csca,c=AE,dc=data,...",
      "cn": "CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE",
      "sn": "37",
      "country": "AE",
      "certType": "CSCA",
      "fingerprint": "0f15d0e5976acce597d77b5e39d2e9650936b96849d82b5d0f5adcee8a47e62a",
      "subjectDn": "CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE",
      "validFrom": "2015-04-15T05:46:55Z",
      "validTo": "2024-06-14T05:46:55Z",
      "isValid": false
    }
  ]
}
```

**LDAP Filter Logic**:
```cpp
// Base filter
std::string filter = "(objectClass=pkdDownload)";

// Add country filter
if (!country.empty()) {
    filter = "(&" + filter + "(c=" + country + "))";
}

// Add type filter
if (certType == "csca") {
    filter = "(&" + filter + "(o=csca))";
} else if (certType == "crl") {
    filter = "(&(objectClass=cRLDistributionPoint)(c=" + country + "))";
}

// Add search term
if (!searchTerm.empty()) {
    filter = "(&" + filter + "(cn=*" + searchTerm + "*))";
}
```

---

### 2. Certificate Detail API

**Endpoint**: `GET /api/certificates/detail`

**Description**: Get detailed information for a specific certificate.

**Query Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `dn` | string | Yes | LDAP Distinguished Name (URL encoded) |

**Response**:
```json
{
  "success": true,
  "certificate": {
    "dn": "cn=...,o=csca,c=AE,dc=data,...",
    "cn": "CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE",
    "sn": "37",
    "country": "AE",
    "certType": "CSCA",
    "description": "Subject DN: ... | Fingerprint: ...",
    "subjectDn": "CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE",
    "issuerDn": "CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE",
    "serialNumber": "00:00:00:37",
    "validFrom": "2015-04-15T05:46:55Z",
    "validTo": "2024-06-14T05:46:55Z",
    "isValid": false,
    "signatureAlgorithm": "ecdsa-with-SHA256",
    "publicKeyAlgorithm": "EC",
    "keySize": 384,
    "fingerprint": "0f15d0e5976acce597d77b5e39d2e9650936b96849d82b5d0f5adcee8a47e62a",
    "certificatePem": "-----BEGIN CERTIFICATE-----\nMIIC4TCCAoigAwIBAgIBNzAK...\n-----END CERTIFICATE-----"
  }
}
```

---

### 3. Individual File Export API

**Endpoint**: `GET /api/certificates/export/file`

**Description**: Download a single certificate/CRL/ML file in DER or PEM format.

**Query Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `dn` | string | Yes | LDAP Distinguished Name (URL encoded) |
| `format` | string | Yes | File format: `der` or `pem` |

**Response Headers**:
```http
Content-Type: application/x-x509-ca-cert       (DER format)
Content-Type: application/x-pem-file           (PEM format)
Content-Disposition: attachment; filename="AE_CSCA_37.der"
```

**Response Body**: Binary file content

**Implementation Flow**:
```cpp
1. Parse DN to extract country, type, serial number
2. LDAP search with DN as base
3. Identify certificate type:
   - objectClass=pkdDownload + inetOrgPerson → userCertificate;binary (X.509)
   - objectClass=cRLDistributionPoint → certificateRevocationList;binary (CRL)
   - objectClass=pkdMasterList → pkdMasterListContent (CMS)
4. Extract binary data (Base64 decode)
5. If format == "pem":
   - X.509: d2i_X509() → PEM_write_bio_X509()
   - CRL: d2i_X509_CRL() → PEM_write_bio_X509_CRL()
   - CMS: d2i_CMS_bio() → PEM_write_bio_CMS()
6. Generate filename: generateFilename(dn, format, certType)
7. Set Content-Disposition header
8. Return binary data
```

---

### 4. Bulk Export by Country API

**Endpoint**: `GET /api/certificates/export/country`

**Description**: Download all certificates/CRLs/MLs for a country as ZIP archive.

**Query Parameters**:
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `country` | string | Yes | - | ISO 3166-1 alpha-2 code |
| `format` | string | No | `pem` | File format: `der` or `pem` |

**Response Headers**:
```http
Content-Type: application/zip
Content-Disposition: attachment; filename="AE_PKD_20260114.zip"
```

**Response Body**: ZIP archive binary data

**Implementation Flow**:
```cpp
1. LDAP search: (c={country})
2. Classify entries by type: csca, dsc, dsc_nc, crl, ml
3. Create ZIP archive (libzip or minizip)
4. For each type:
   - Create folder: {type}/
   - For each entry:
     - Extract binary data
     - Convert to PEM if needed
     - Generate filename
     - Add file to ZIP: zip_file_add(archive, "{type}/{filename}", ...)
5. Close ZIP archive
6. Return ZIP binary data
```

---

## Frontend UI Design

### Menu Structure Change

**Before**:
```
Sidebar Menu:
├── Dashboard
├── PKD Upload
│   └── File Upload (single page)
├── Upload History
└── ...
```

**After**:
```
Sidebar Menu:
├── Dashboard
├── PKD Management          ← Renamed from "PKD Upload"
│   ├── File Upload         ← Existing page
│   ├── Certificate Search  ← NEW page
│   └── Upload History      ← Moved under PKD Management
└── ...
```

---

### Certificate Search Page UI (`/pkd/certificates`)

#### 1. Search Filter Card
```
┌─ 검색 필터 ─────────────────────────────────────────────────────┐
│                                                                  │
│  국가: [All Countries ▼]  타입: [All Types ▼]  유효성: [All ▼] │
│                                                                  │
│  검색어: [________________________________]  [🔍 검색]           │
│                                                                  │
│  [📦 선택 국가 일괄 Export (ZIP)]  ← Enabled when country selected│
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**Dropdowns**:
- **국가**: All Countries, AE (UAE), DE (Germany), FR (France), ...
- **타입**: All Types, CSCA, DSC, DSC_NC, CRL, Master List
- **유효성**: All, Valid, Expired

#### 2. Statistics Cards
```
┌─ 총 인증서 ─┐ ┌─ CSCA ─┐ ┌─ DSC ─┐ ┌─ 유효 ─┐
│   31,155    │ │  525   │ │ 29,610 │ │ 15,234 │
└─────────────┘ └────────┘ └────────┘ └────────┘
```

#### 3. Certificate List Table
```
┌─ 인증서 리스트 ──────────────────────────────────────────────────┐
│                                                                   │
│  국가  │  타입   │  Subject CN         │  유효기간      │ 상태 │ 액션 │
│ ──────┼─────────┼────────────────────┼───────────────┼──────┼───────│
│  🇦🇪 AE│  CSCA  │ UAE CSCA 01        │ 2015 ~ 2024   │ ❌만료│[상세] │
│        │        │ OU=EPASS, O=MOI    │               │      │ [▼]  │
│ ──────┼─────────┼────────────────────┼───────────────┼──────┼───────│
│  🇩🇪 DE│  DSC   │ csca-germany       │ 2020 ~ 2030   │ ✅유효│[상세] │
│        │        │ OU=bsi, O=bund     │               │      │ [▼]  │
│ ──────┼─────────┼────────────────────┼───────────────┼──────┼───────│
│  🇫🇷 FR│  CRL   │ CSCA France        │ 2023 ~ 2024   │ ✅유효│[상세] │
│        │        │                    │               │      │ [▼]  │
│                                                                   │
│              [1] [2] [3] ... [10]  (1234 건)                     │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

**Export Dropdown Menu** (per row):
```
[▼] 클릭 시:
  📥 Export DER
  📥 Export PEM
```

#### 4. Certificate Details Dialog
```
┌─ 인증서 상세 정보 ───────────────────────────────────────────┐
│                                                               │
│  📋 기본 정보                                                 │
│  ─────────────────────────────────────────────────────────   │
│  타입:         CSCA                                           │
│  국가:         🇦🇪 UAE (AE)                                   │
│  일련번호:      00:00:00:37                                   │
│                                                               │
│  📝 Subject DN                                                │
│  ─────────────────────────────────────────────────────────   │
│  CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE                          │
│                                                               │
│  🔐 Issuer DN                                                 │
│  ─────────────────────────────────────────────────────────   │
│  CN=UAE CSCA 01,OU=EPASS,O=MOI,C=AE (Self-signed)            │
│                                                               │
│  ⏰ 유효기간                                                   │
│  ─────────────────────────────────────────────────────────   │
│  시작: 2015-04-15 05:46:55 UTC                                │
│  종료: 2024-06-14 05:46:55 UTC                                │
│  상태: ❌ 만료됨                                               │
│                                                               │
│  🔑 암호화 정보                                                │
│  ─────────────────────────────────────────────────────────   │
│  서명 알고리즘:    ecdsa-with-SHA256                          │
│  공개키 알고리즘:   EC                                         │
│  키 크기:         384 bits                                    │
│                                                               │
│  🔍 Fingerprint (SHA-256)                                     │
│  ─────────────────────────────────────────────────────────   │
│  0f15d0e5976acce597d77b5e39d2e9650936b96849d82b5d0f5adcee8...│
│                                                               │
│  [📥 Export DER] [📥 Export PEM] [📄 PEM 보기] [닫기]        │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**PEM View Dialog** (when "PEM 보기" clicked):
```
┌─ PEM Certificate ────────────────────────────────────────────┐
│                                                               │
│  -----BEGIN CERTIFICATE-----                                 │
│  MIIC4TCCAoigAwIBAgIBNzAKBggqhkjOPQQDAjBBMQswCQYDVQQGEwJBRT  │
│  EMMAoGA1UECgwDTU9JMQ4wDAYDVQQLDAVFUEFTUzEUMBIGA1UEAwwLVU  │
│  ...                                                          │
│  -----END CERTIFICATE-----                                   │
│                                                               │
│  [📋 Copy to Clipboard] [닫기]                                │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

---

## Technical Implementation Details

### Backend Dependencies

#### Required Libraries
```json
{
  "dependencies": [
    "drogon",
    "openldap",
    "openssl",
    "nlohmann-json",
    "libzip"
  ]
}
```

#### CMakeLists.txt Addition
```cmake
find_package(libzip REQUIRED)
target_link_libraries(pkd-management PRIVATE libzip::zip)
```

---

### DER ↔ PEM Conversion (OpenSSL)

#### X.509 Certificate: DER → PEM
```cpp
#include <openssl/x509.h>
#include <openssl/pem.h>

std::string convertX509DerToPem(const std::vector<unsigned char>& derData) {
    const unsigned char* data = derData.data();
    X509* cert = d2i_X509(nullptr, &data, derData.size());
    if (!cert) {
        throw std::runtime_error("Failed to parse X.509 DER");
    }

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);

    BUF_MEM* mem;
    BIO_get_mem_ptr(bio, &mem);
    std::string pem(mem->data, mem->length);

    BIO_free(bio);
    X509_free(cert);

    return pem;
}
```

#### CRL: DER → PEM
```cpp
#include <openssl/x509.h>
#include <openssl/pem.h>

std::string convertCrlDerToPem(const std::vector<unsigned char>& derData) {
    const unsigned char* data = derData.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, derData.size());
    if (!crl) {
        throw std::runtime_error("Failed to parse CRL DER");
    }

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509_CRL(bio, crl);

    BUF_MEM* mem;
    BIO_get_mem_ptr(bio, &mem);
    std::string pem(mem->data, mem->length);

    BIO_free(bio);
    X509_CRL_free(crl);

    return pem;
}
```

#### CMS (Master List): DER → PEM
```cpp
#include <openssl/cms.h>
#include <openssl/pem.h>

std::string convertCmsDerToPem(const std::vector<unsigned char>& derData) {
    BIO* bio_in = BIO_new_mem_buf(derData.data(), derData.size());
    CMS_ContentInfo* cms = d2i_CMS_bio(bio_in, nullptr);
    BIO_free(bio_in);

    if (!cms) {
        throw std::runtime_error("Failed to parse CMS DER");
    }

    BIO* bio_out = BIO_new(BIO_s_mem());
    PEM_write_bio_CMS(bio_out, cms);

    BUF_MEM* mem;
    BIO_get_mem_ptr(bio_out, &mem);
    std::string pem(mem->data, mem->length);

    BIO_free(bio_out);
    CMS_ContentInfo_free(cms);

    return pem;
}
```

---

### ZIP Archive Creation (libzip)

```cpp
#include <zip.h>

void createCountryZipExport(const std::string& country,
                           const std::string& format,
                           const std::string& outputPath) {
    int error;
    zip_t* archive = zip_open(outputPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!archive) {
        throw std::runtime_error("Failed to create ZIP archive");
    }

    // Search LDAP for all entries in country
    std::vector<LdapCertEntry> entries = searchLdapByCountry(country);

    // Group by type
    std::map<std::string, std::vector<LdapCertEntry>> grouped;
    for (const auto& entry : entries) {
        grouped[entry.certType].push_back(entry);
    }

    // Add files to ZIP
    for (const auto& [type, certs] : grouped) {
        for (const auto& cert : certs) {
            // Get binary data
            std::vector<unsigned char> data = cert.binaryData;

            // Convert to PEM if needed
            if (format == "pem") {
                std::string pem = convertToPem(data, cert.certType);
                data = std::vector<unsigned char>(pem.begin(), pem.end());
            }

            // Generate filename
            std::string filename = type + "/" + generateFilename(cert.dn, format, cert.certType);

            // Add to ZIP
            zip_source_t* source = zip_source_buffer(archive, data.data(), data.size(), 0);
            if (zip_file_add(archive, filename.c_str(), source, ZIP_FL_OVERWRITE) < 0) {
                zip_source_free(source);
                throw std::runtime_error("Failed to add file to ZIP");
            }
        }
    }

    zip_close(archive);
}
```

---

### LDAP Search with Pagination

```cpp
#include <ldap.h>

struct LdapSearchResult {
    std::vector<LdapCertEntry> entries;
    int totalCount;
};

LdapSearchResult searchCertificates(const std::string& country,
                                   const std::string& certType,
                                   const std::string& searchTerm,
                                   int limit, int offset) {
    LDAP* ld = ldap_init("localhost", 389);

    // Build LDAP filter
    std::string filter = "(objectClass=pkdDownload)";
    if (!country.empty()) {
        filter = "(&" + filter + "(c=" + country + "))";
    }
    if (certType == "csca") {
        filter = "(&" + filter + "(o=csca))";
    } else if (certType == "dsc") {
        filter = "(&" + filter + "(o=dsc))";
    }
    // ... other types

    if (!searchTerm.empty()) {
        filter = "(&" + filter + "(cn=*" + searchTerm + "*))";
    }

    // Search with pagination
    LDAPControl* serverControls[2];
    LDAPControl pageControl;
    // Setup pagination control (RFC 2696)

    LDAPMessage* result;
    int rc = ldap_search_ext_s(ld, baseDn.c_str(), LDAP_SCOPE_SUBTREE,
                               filter.c_str(), nullptr, 0,
                               serverControls, nullptr, nullptr, 0, &result);

    // Parse results
    LdapSearchResult searchResult;
    for (LDAPMessage* entry = ldap_first_entry(ld, result);
         entry != nullptr;
         entry = ldap_next_entry(ld, entry)) {
        LdapCertEntry cert = parseLdapEntry(ld, entry);
        searchResult.entries.push_back(cert);
    }

    ldap_msgfree(result);
    ldap_unbind_ext_s(ld, nullptr, nullptr);

    return searchResult;
}
```

---

## File Naming Conventions

### Filename Generation Logic

```cpp
std::string generateFilename(const std::string& dn,
                             const std::string& format,
                             const std::string& certType) {
    // Extract country from DN: "c=AE" → "AE"
    std::string country = extractCountryFromDn(dn);

    // Extract serial number from DN: "sn=37" → "37"
    // For CRL/ML, use first 8 chars of CN hash
    std::string serial = extractSerialFromDn(dn, certType);

    // Determine file extension
    std::string ext;
    if (certType == "CSCA" || certType == "DSC" || certType == "DSC_NC") {
        ext = (format == "pem") ? ".crt" : ".der";
    } else if (certType == "CRL") {
        ext = (format == "pem") ? ".crl" : ".der";
    } else if (certType == "ML") {
        ext = (format == "pem") ? ".p7s" : ".der";
    }

    return country + "_" + certType + "_" + serial + ext;
}

std::string extractCountryFromDn(const std::string& dn) {
    // Parse "c=AE" from DN
    size_t pos = dn.find(",c=");
    if (pos == std::string::npos) return "UNKNOWN";

    size_t start = pos + 3;
    size_t end = dn.find(",", start);
    if (end == std::string::npos) end = dn.length();

    return dn.substr(start, end - start);
}

std::string extractSerialFromDn(const std::string& dn, const std::string& certType) {
    if (certType == "CRL" || certType == "ML") {
        // Use first 8 chars of CN hash
        size_t pos = dn.find("cn=");
        if (pos != std::string::npos) {
            size_t start = pos + 3;
            size_t end = dn.find(",", start);
            std::string cn = dn.substr(start, std::min(8UL, end - start));
            return cn;
        }
        return "unknown";
    } else {
        // Extract sn=37 → "37"
        size_t pos = dn.find("sn=");
        if (pos != std::string::npos) {
            size_t start = pos + 3;
            size_t end = dn.find(",", start);
            if (end == std::string::npos) end = dn.length();
            return dn.substr(start, end - start);
        }
        return "unknown";
    }
}
```

### Example Filenames

| Certificate Type | Country | Serial | Format | Filename |
|------------------|---------|--------|--------|----------|
| CSCA | AE | 37 | DER | `AE_CSCA_37.der` |
| CSCA | AE | 37 | PEM | `AE_CSCA_37.crt` |
| DSC | DE | 011D | PEM | `DE_DSC_011D.crt` |
| CRL | QA | 74bafbc6 | PEM | `QA_CRL_74bafbc6.crl` |
| ML | FR | 36722339 | PEM | `FR_ML_36722339.p7s` |

---

## Implementation Tasks

### Backend (pkd-management service)

- [x] 1. Requirements analysis and design
- [x] 2. Export feature redesign (individual + bulk)
- [ ] 3. Certificate search API implementation
  - [ ] LDAP filter generation (country, type, search term)
  - [ ] Pagination support (limit, offset)
  - [ ] X.509 parsing for validity check
  - [ ] JSON response formatting
- [ ] 4. Certificate detail API implementation
  - [ ] DN-based LDAP lookup
  - [ ] Full X.509 parsing (Issuer, Serial, Signature Algorithm)
  - [ ] PEM conversion for display
- [ ] 5. Individual file export API implementation
  - [ ] DER/PEM format support
  - [ ] Type detection (X.509, CRL, CMS)
  - [ ] Filename generation
  - [ ] Content-Disposition header
- [ ] 6. Bulk export API implementation
  - [ ] Country-based LDAP search
  - [ ] ZIP archive creation (libzip)
  - [ ] Folder structure (csca/, dsc/, crl/, ml/)
  - [ ] Streaming response for large files

### Frontend (React)

- [ ] 7. Menu restructuring
  - [ ] Rename "PKD Upload" → "PKD Management"
  - [ ] Create submenu structure
  - [ ] Move "Upload History" under PKD Management
- [ ] 8. CertificateSearch page component
  - [ ] Route: `/pkd/certificates`
  - [ ] Layout structure
- [ ] 9. Search filter UI
  - [ ] Country dropdown (dynamic from API)
  - [ ] Certificate type dropdown
  - [ ] Validity dropdown
  - [ ] Search input with debounce
  - [ ] Bulk export button (enabled when country selected)
- [ ] 10. Statistics cards
  - [ ] Total certificates
  - [ ] CSCA count
  - [ ] DSC count
  - [ ] Valid count
- [ ] 11. Certificate list table
  - [ ] Pagination
  - [ ] Country flag display
  - [ ] Validity status icon
  - [ ] Export dropdown menu per row
  - [ ] Row click → details dialog
- [ ] 12. Certificate details dialog
  - [ ] X.509 info display
  - [ ] Export DER/PEM buttons
  - [ ] PEM view dialog
- [ ] 13. Export functionality
  - [ ] Individual file download (API call)
  - [ ] Bulk ZIP download
  - [ ] Download progress indicator
- [ ] 14. Navigation integration
  - [ ] Update Sidebar component
  - [ ] Update routing configuration

### Testing

- [ ] 15. Integration testing
  - [ ] Search filter combinations
  - [ ] Pagination
  - [ ] Individual export (DER/PEM)
  - [ ] Bulk export (ZIP structure)
  - [ ] File format validation
  - [ ] Large dataset performance (31K+ entries)

---

## Appendix

### LDAP Base DN Configuration
```cpp
std::string LDAP_BASE_DN = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
std::string LDAP_DATA_DN = "dc=data,dc=download," + LDAP_BASE_DN;
std::string LDAP_NC_DATA_DN = "dc=nc-data,dc=download," + LDAP_BASE_DN;
```

### HTTP Response Headers for File Download
```http
# DER format X.509 certificate
Content-Type: application/x-x509-ca-cert
Content-Disposition: attachment; filename="AE_CSCA_37.der"

# PEM format X.509 certificate
Content-Type: application/x-pem-file
Content-Disposition: attachment; filename="AE_CSCA_37.crt"

# ZIP archive
Content-Type: application/zip
Content-Disposition: attachment; filename="AE_PKD_20260114.zip"
```

### Error Handling

```json
// LDAP connection error
{
  "success": false,
  "error": "LDAP connection failed",
  "code": "LDAP_CONN_ERROR"
}

// DN not found
{
  "success": false,
  "error": "Certificate not found",
  "code": "CERT_NOT_FOUND"
}

// Invalid format parameter
{
  "success": false,
  "error": "Invalid format. Use 'der' or 'pem'",
  "code": "INVALID_FORMAT"
}
```

---

**End of Design Document**

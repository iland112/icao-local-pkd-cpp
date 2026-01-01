# ICAO Local PKD - C++ Implementation

**Version**: 1.0
**Last Updated**: 2026-01-01
**Status**: Production Ready

---

## Project Overview

C++ REST API 기반의 ICAO Local PKD 관리 및 Passive Authentication (PA) 검증 시스템입니다.

### Core Features

| Module | Description | Status |
|--------|-------------|--------|
| **PKD Upload** | LDIF/Master List 파일 업로드, 파싱, 검증 | ✅ Complete |
| **Certificate Validation** | CSCA/DSC Trust Chain, CRL 검증 | ✅ Complete |
| **LDAP Integration** | OpenLDAP 연동 (ICAO PKD DIT) | ✅ Complete |
| **Passive Authentication** | ICAO 9303 PA 검증 (SOD, DG 해시) | ✅ Complete |
| **React.js Frontend** | CSR 기반 웹 UI | ✅ Complete |

### Technology Stack

| Category | Technology |
|----------|------------|
| **Language** | C++20 |
| **Web Framework** | Drogon 1.9+ |
| **Database** | PostgreSQL 15 + libpq |
| **LDAP** | OpenLDAP C API (libldap) |
| **Crypto** | OpenSSL 3.x |
| **JSON** | nlohmann/json |
| **Logging** | spdlog |
| **Build** | CMake 3.20+ / vcpkg |
| **Frontend** | React 19 + TypeScript + Vite + TailwindCSS 4 |

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         React.js Frontend (:3000)                        │
└─────────────────────────────────────────────────────────────────────────┘
                                    │ REST API
┌─────────────────────────────────────────────────────────────────────────┐
│                    PKD Management Service (:8081)                        │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────────────────┐   │
│  │ Upload API    │  │ Statistics    │  │ Certificate Validation    │   │
│  └───────────────┘  └───────────────┘  └───────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
         │                              │
         ↓                              ↓
┌─────────────────┐          ┌─────────────────────────────────────────┐
│   PostgreSQL    │          │         OpenLDAP MMR Cluster            │
│     :5432       │          │  ┌───────────┐      ┌───────────┐       │
│                 │          │  │ OpenLDAP1 │◄────►│ OpenLDAP2 │       │
│ - certificate   │          │  │   :3891   │      │   :3892   │       │
│ - crl           │          │  └─────┬─────┘      └─────┬─────┘       │
│ - master_list   │          │        └──────┬──────────┘              │
│ - validation    │          │               ↓                         │
└─────────────────┘          │        ┌───────────┐                    │
                             │        │  HAProxy  │ :389               │
                             │        └───────────┘                    │
                             └─────────────────────────────────────────┘
```

### LDAP DIT Structure (ICAO PKD)

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=pkd
    └── dc=download
        ├── dc=data
        │   └── c={COUNTRY}
        │       ├── o=csca    (CSCA certificates)
        │       ├── o=dsc     (DSC certificates)
        │       ├── o=crl     (CRL)
        │       └── o=ml      (Master Lists)
        └── dc=nc-data
            └── c={COUNTRY}
                └── o=dsc     (DSC_NC - Non-Conformant)
```

---

## Directory Structure

```
icao-local-pkd/
├── services/
│   └── pkd-management/        # Main C++ service
│       ├── src/main.cpp       # All endpoints and logic
│       ├── CMakeLists.txt
│       ├── vcpkg.json
│       └── Dockerfile
├── frontend/                  # React.js frontend
├── docker/
│   ├── docker-compose.yaml
│   └── init-scripts/          # PostgreSQL init
├── openldap/
│   ├── schemas/               # ICAO PKD custom schema
│   ├── bootstrap/             # Initial LDIF
│   └── scripts/               # Init scripts
├── .docker-data/              # Bind mount data (gitignored)
└── data/cert/                 # Trust anchor certificates
```

---

## Quick Start

### Docker (Recommended)

```bash
# Start all services
./docker-start.sh

# With rebuild
./docker-start.sh --build

# Infrastructure only (no app)
./docker-start.sh --skip-app

# Clean all data and restart
./docker-clean.sh
```

### Access URLs

| Service | URL |
|---------|-----|
| Frontend | http://localhost:3000 |
| Backend API | http://localhost:8081/api |
| HAProxy Stats | http://localhost:8404 |
| PostgreSQL | localhost:5432 (pkd/pkd123) |
| LDAP (HAProxy) | ldap://localhost:389 |

---

## API Endpoints

### File Upload

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/upload/ldif` | Upload LDIF file |
| POST | `/api/upload/masterlist` | Upload Master List file |
| GET | `/api/upload/history` | Get upload history |
| GET | `/api/upload/statistics` | Get upload statistics |
| GET | `/api/progress/stream/{id}` | SSE progress stream |

### Health Check

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/health` | Application health |
| GET | `/api/health/database` | PostgreSQL status |
| GET | `/api/health/ldap` | LDAP status |

---

## ICAO 9303 Compliance

### DSC Trust Chain Validation

```
1. Parse DSC from LDIF/Master List
2. Extract issuer DN from DSC
3. Lookup CSCA by issuer DN (case-insensitive)
4. Verify DSC signature with CSCA public key: X509_verify(dsc, csca_pubkey)
5. Check validity period
6. Record validation result in DB
```

### Validation Statistics (Current)

| Metric | Count |
|--------|-------|
| Total Certificates | 30,637 |
| CSCA | 525 |
| DSC | 29,610 |
| DSC_NC | 502 |
| Trust Chain Valid | 5,868 |
| Trust Chain Invalid | 24,244 |
| CSCA Not Found | 6,299 |

---

## Key Technical Notes

### PostgreSQL Bytea Storage

**Important**: Use standard quotes for bytea hex format, NOT escape string literal.

```cpp
// CORRECT - PostgreSQL interprets \x as bytea hex format
"'" + byteaEscaped + "'"

// WRONG - E'' causes \x to be treated as escape sequence
"E'" + byteaEscaped + "'"
```

### LDAP Connection Strategy

| Operation | Host | Purpose |
|-----------|------|---------|
| Read | haproxy:389 | Load balanced across MMR nodes |
| Write | openldap1:389 | Direct to primary master |

### Master List Processing

- ICAO Master List contains **ONLY CSCA** certificates (per ICAO Doc 9303)
- Both self-signed and cross-signed CSCAs are classified as CSCA
- Uses OpenSSL CMS API (`d2i_CMS_bio`) for parsing

---

## Development

### Build from Source

```bash
cd services/pkd-management
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)
```

### Docker Build

```bash
docker-compose -f docker/docker-compose.yaml build pkd-management
docker-compose -f docker/docker-compose.yaml up -d pkd-management
```

---

## References

- **ICAO Doc 9303 Part 11**: Security Mechanisms for MRTDs
- **ICAO Doc 9303 Part 12**: PKI for MRTDs
- **RFC 5280**: X.509 PKI Certificate and CRL Profile
- **RFC 5652**: Cryptographic Message Syntax (CMS)

---

## Change Log

### 2026-01-01: Frontend UI/UX Improvements

**Upload History Page** (`/upload-history`):
- Added statistics cards (Total, Completed, Failed, In Progress)
- Added filter card with file format, status, date range, and search
- Consistent design pattern with PA History page

**Upload Dashboard Page** (`/upload-dashboard`):
- Removed pie charts (certificate type, upload status)
- Added overview stats cards (4 columns): Total certificates, Upload status, Validation rate, Countries
- Added certificate breakdown cards (6 columns) with visual indicators
- Replaced pie charts with horizontal progress bars for validation status
- Improved Trust Chain validation display with card grid layout

**Dashboard Page** (`/`):
- Moved certificate/validation statistics to Upload Dashboard
- Added Top 18 countries display with 2-column grid layout
- Country cards show flag, CSCA/DSC counts, and progress bars

### 2026-01-01: Bytea Storage Bug Fix

**Issue**: DSC Trust Chain validation returned 0 valid certificates despite 30k DSCs matching 525 CSCAs.

**Root Cause**: Certificate binary data stored as ASCII hex text instead of raw DER bytes.
- `PQescapeByteaConn` returns `\x30820100` as text
- Using `E'...'` (escape string) caused PostgreSQL to interpret `\x` as escape char
- Result: `E'\x30820100'` stored `'0820100'` (ASCII) instead of bytes `0x30 0x82 0x01 0x00`

**Fix**: Removed `E` prefix from bytea INSERT statements in `main.cpp`:
```cpp
// Before: "E'" + byteaEscaped + "'"
// After:  "'" + byteaEscaped + "'"
```

**Result**: Trust Chain validation now works correctly (5,868 valid out of 29,610 DSCs).

### 2025-12-31: DSC Trust Chain Validation

- Added `findCscaByIssuerDn()` function for CSCA lookup
- Added `validateDscCertificate()` for Trust Chain verification
- Fixed Master List certificates to always classify as CSCA

### 2025-12-30: Upload Pipeline Complete

- End-to-end LDIF/ML upload with DB and LDAP storage
- OpenSSL CMS API for Master List parsing
- LDAP MMR write strategy (direct to primary master)

---

**Project Owner**: kbjung
**Organization**: SmartCore Inc.

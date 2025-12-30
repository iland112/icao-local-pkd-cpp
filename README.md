# ICAO Local PKD - C++ Implementation

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Drogon](https://img.shields.io/badge/Drogon-1.9+-green.svg)](https://github.com/drogonframework/drogon)
[![OpenSSL](https://img.shields.io/badge/OpenSSL-3.x-red.svg)](https://www.openssl.org/)
[![License](https://img.shields.io/badge/License-Proprietary-gray.svg)](LICENSE)

ICAO Doc 9303 표준 기반의 Local PKD(Public Key Directory) 관리 및 Passive Authentication 검증 시스템입니다.

## 주요 기능

| 모듈 | 설명 |
|------|------|
| **PKD Upload** | LDIF/Master List 파일 업로드, 파싱, 검증 |
| **Certificate Validation** | CSCA/DSC Trust Chain, CRL 검증 (RFC 5280) |
| **LDAP Integration** | OpenLDAP MMR 연동 (ICAO PKD DIT) |
| **Passive Authentication** | ICAO 9303 PA 검증 (SOD, DG 해시) |
| **React.js Frontend** | CSR 기반 웹 UI (Preline UI) |

## 시스템 아키텍처

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         React.js Frontend (:3000)                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ PKD Upload  │  │ PA Verify   │  │ History     │  │ Dashboard   │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ↓ REST API
┌─────────────────────────────────────────────────────────────────────────┐
│                      C++ Backend (Drogon)                                │
│  ┌───────────────────────────┐  ┌───────────────────────────┐          │
│  │  PKD Management (:8081)   │  │    PA Service (:8082)     │          │
│  │  - File Upload/Parse      │  │  - SOD Verification       │          │
│  │  - Certificate Validation │  │  - DG Hash Validation     │          │
│  │  - LDAP Write             │  │  - Trust Chain Check      │          │
│  └───────────────────────────┘  └───────────────────────────┘          │
└─────────────────────────────────────────────────────────────────────────┘
         │                              │
         ↓                              ↓
┌─────────────────┐          ┌─────────────────────────────────────────┐
│   PostgreSQL    │          │         OpenLDAP MMR Cluster            │
│     :5432       │          │  ┌───────────┐      ┌───────────┐       │
│                 │          │  │ OpenLDAP1 │◄────►│ OpenLDAP2 │       │
│ - certificate   │          │  │  :3891    │      │  :3892    │       │
│ - crl           │          │  └─────┬─────┘      └─────┬─────┘       │
│ - upload_history│          │        └──────┬──────────┘              │
│ - pa_verification│         │               ↓                         │
└─────────────────┘          │        ┌───────────┐                    │
                             │        │  HAProxy  │ :389               │
                             │        └───────────┘                    │
                             └─────────────────────────────────────────┘
```

## 기술 스택

| 카테고리 | 기술 |
|----------|------|
| **Language** | C++20 |
| **Web Framework** | Drogon 1.9+ |
| **Database** | PostgreSQL 15 |
| **LDAP** | OpenLDAP (MMR Cluster) |
| **Crypto** | OpenSSL 3.x |
| **JSON** | nlohmann/json |
| **Logging** | spdlog |
| **Build** | CMake 3.20+ |
| **Package Manager** | vcpkg |
| **Frontend** | React 19 + TypeScript + Vite + TailwindCSS 4 + Preline |

## 빠른 시작

### 사전 요구사항

- Docker & Docker Compose
- Git

### 설치 및 실행

```bash
# 1. 저장소 클론
git clone https://github.com/smartcore/icao-local-pkd.git
cd icao-local-pkd

# 2. Docker 컨테이너 시작
./docker-start.sh

# 3. 브라우저에서 접속
open http://localhost:3000
```

### 접속 정보

| 서비스 | URL | 설명 |
|--------|-----|------|
| **Frontend** | http://localhost:3000 | 웹 UI |
| **PKD Management API** | http://localhost:8081/api | PKD 관리 API |
| **PA Service API** | http://localhost:8082/api | PA 검증 API |
| **HAProxy Stats** | http://localhost:8404 | LDAP 로드밸런서 통계 |
| **PostgreSQL** | localhost:5432 | 데이터베이스 (pkd/pkd) |
| **OpenLDAP 1** | ldap://localhost:3891 | LDAP 노드 1 |
| **OpenLDAP 2** | ldap://localhost:3892 | LDAP 노드 2 |

## 운영 가이드

### 컨테이너 관리 스크립트

```bash
# 컨테이너 시작
./docker-start.sh                    # 전체 서비스 시작
./docker-start.sh --build            # 이미지 다시 빌드 후 시작
./docker-start.sh --skip-app         # 인프라만 시작 (DB, LDAP)
./docker-start.sh --skip-ldap        # LDAP 제외

# 컨테이너 중지
./docker-stop.sh

# 컨테이너 재시작
./docker-restart.sh                  # 전체 재시작
./docker-restart.sh pkd-management   # 특정 서비스만 재시작

# 로그 확인
./docker-logs.sh                     # 전체 로그
./docker-logs.sh pkd-management      # 특정 서비스 로그
./docker-logs.sh pkd-management 500  # 최근 500줄

# 헬스 체크
./docker-health.sh                   # 모든 서비스 상태 확인

# 백업/복구
./docker-backup.sh                   # 데이터 백업
./docker-restore.sh ./backups/20251231_120000  # 복구

# 완전 초기화
./docker-clean.sh                    # 모든 데이터 삭제 (주의!)

# LDAP 초기화 (스키마 + MMR + DIT)
./docker-ldap-init.sh
```

### 서비스 상태 모니터링

```bash
# 헬스 체크 API
curl http://localhost:8081/api/health
curl http://localhost:8081/api/health/database
curl http://localhost:8081/api/health/ldap

# 업로드 통계
curl http://localhost:8081/api/upload/statistics

# PA 통계
curl http://localhost:8082/api/pa/statistics
```

### LDAP DIT 구조

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=pkd
    └── dc=download
        ├── dc=data                    # 적합 인증서
        │   └── c={COUNTRY}
        │       ├── o=csca            # CSCA 인증서
        │       ├── o=dsc             # DSC 인증서
        │       ├── o=crl             # CRL
        │       └── o=ml              # Master List
        └── dc=nc-data                 # 비적합 인증서
            └── c={COUNTRY}
                └── o=dsc             # DSC_NC
```

## API 문서

### PKD Management API (:8081)

| Method | Endpoint | 설명 |
|--------|----------|------|
| POST | `/api/upload/ldif` | LDIF 파일 업로드 |
| POST | `/api/upload/masterlist` | Master List 업로드 |
| GET | `/api/upload/history` | 업로드 이력 조회 |
| GET | `/api/upload/{id}` | 업로드 상세 정보 |
| GET | `/api/upload/statistics` | 업로드 통계 |
| GET | `/api/progress/stream/{id}` | SSE 진행 상황 스트림 |

### PA Service API (:8082)

| Method | Endpoint | 설명 |
|--------|----------|------|
| POST | `/api/pa/verify` | Passive Authentication 검증 |
| GET | `/api/pa/history` | PA 검증 이력 조회 |
| GET | `/api/pa/{id}` | PA 검증 결과 상세 |
| GET | `/api/pa/statistics` | PA 통계 |

### LDAP API (:8081)

| Method | Endpoint | 설명 |
|--------|----------|------|
| GET | `/api/ldap/health` | LDAP 연결 상태 |
| GET | `/api/ldap/certificates` | 인증서 검색 |
| GET | `/api/ldap/certificates/{fingerprint}` | 인증서 조회 |
| GET | `/api/ldap/crls` | CRL 검색 |
| GET | `/api/ldap/revocation/check` | 폐기 상태 확인 |

## 인증서 검증 표준

이 시스템은 다음 표준을 준수합니다:

- **ICAO Doc 9303** - Machine Readable Travel Documents
  - Part 11: Security Mechanisms for MRTDs
  - Part 12: Public Key Infrastructure for MRTDs
- **RFC 5280** - X.509 PKI Certificate and CRL Profile
- **RFC 5652** - Cryptographic Message Syntax (CMS)

### 검증 절차

#### CSCA (Country Signing CA) 검증
1. Self-Signed 확인
2. CA Flag 확인 (BasicConstraints)
3. KeyUsage 확인 (keyCertSign, cRLSign)
4. 유효 기간 확인
5. 자체 서명 검증

#### DSC (Document Signer Certificate) 검증
1. Issuer DN 일치 확인 (CSCA Subject DN)
2. CSCA 공개키로 서명 검증
3. KeyUsage 확인 (digitalSignature)
4. 유효 기간 확인
5. CRL 폐기 확인

#### Passive Authentication
1. SOD 파싱 (Tag 0x77 unwrap)
2. DSC 추출
3. CSCA 조회 (LDAP)
4. Trust Chain 검증
5. SOD 서명 검증
6. Data Group 해시 검증
7. CRL 폐기 확인

## 개발 가이드

### 로컬 빌드 (Docker 없이)

```bash
# 사전 요구사항 설치 (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git \
    libssl-dev libpq-dev libldap2-dev uuid-dev

# vcpkg 설치
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh
export VCPKG_ROOT=$(pwd)

# 빌드
cd /path/to/icao-local-pkd
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)

# 실행
./build/icao-local-pkd
```

### 프로젝트 구조

```
icao-local-pkd/
├── src/                           # C++ 소스 코드
│   ├── main.cpp                   # 애플리케이션 진입점
│   ├── shared/                    # 공유 커널
│   ├── fileupload/                # 파일 업로드 모듈
│   ├── fileparsing/               # 파일 파싱 모듈
│   ├── certificatevalidation/     # 인증서 검증 모듈
│   ├── ldapintegration/           # LDAP 연동 모듈
│   └── passiveauthentication/     # PA 검증 모듈
├── frontend/                      # React.js 프론트엔드
├── services/                      # 마이크로서비스
│   ├── pkd-management/            # PKD 관리 서비스
│   └── pa-service/                # PA 검증 서비스
├── docker/                        # Docker 설정
├── openldap/                      # OpenLDAP 설정
├── haproxy/                       # HAProxy 설정
├── docs/                          # 문서
└── tests/                         # 테스트
```

## 문서

| 문서 | 설명 |
|------|------|
| [CLAUDE.md](CLAUDE.md) | 프로젝트 개발 문서 및 이력 |
| [CERTIFICATE_VALIDATION_COMPARISON.md](docs/CERTIFICATE_VALIDATION_COMPARISON.md) | Java vs C++ 검증 비교 |
| [RFC5280_LDAP_UPDATE_GUIDE.md](docs/RFC5280_LDAP_UPDATE_GUIDE.md) | LDAP 업데이트 가이드 |

## 라이선스

Proprietary - SmartCore Inc.

---

**Project Owner**: kbjung
**Organization**: SmartCore Inc.
**Last Updated**: 2025-12-31

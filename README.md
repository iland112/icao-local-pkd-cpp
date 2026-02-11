# ICAO Local PKD

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Drogon](https://img.shields.io/badge/Drogon-1.9+-green.svg)](https://github.com/drogonframework/drogon)
[![OpenSSL](https://img.shields.io/badge/OpenSSL-3.x-red.svg)](https://www.openssl.org/)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-15-336791.svg)](https://www.postgresql.org/)
[![Oracle](https://img.shields.io/badge/Oracle-XE%2021c-F80000.svg)](https://www.oracle.com/database/)
[![React](https://img.shields.io/badge/React-19-61DAFB.svg)](https://react.dev/)
[![License](https://img.shields.io/badge/License-Proprietary-gray.svg)](LICENSE)

ICAO Doc 9303 표준 기반의 **Local PKD(Public Key Directory)** 관리 및 **Passive Authentication** 검증 시스템입니다.

31,000+ 인증서(95개국), Multi-DBMS(PostgreSQL/Oracle), OpenLDAP MMR 클러스터를 지원하는 프로덕션 시스템입니다.

---

## 주요 기능

| 모듈 | 설명 |
|------|------|
| **PKD Upload** | LDIF/Master List 파일 업로드, 파싱, 검증 (AUTO/MANUAL 모드) |
| **Certificate Validation** | CSCA/DSC Trust Chain, CRL, Link Certificate 검증 (RFC 5280) |
| **Certificate Search** | 국가/유형/상태 필터 검색, 메타데이터 조회, 인증서 내보내기 |
| **LDAP Integration** | OpenLDAP MMR 클러스터 연동 (Software Load Balancing) |
| **Passive Authentication** | ICAO 9303 PA 검증 (SOD, DG 해시, 8단계 프로세스) |
| **DB-LDAP Sync** | 실시간 동기화 모니터링 및 자동 Reconciliation |
| **ICAO Monitoring** | ICAO PKD 버전 모니터링 (신규 버전 자동 감지) |
| **Audit Logging** | 이중 감사 로그 (인증 이벤트 + 운영 작업) |
| **Multi-DBMS** | PostgreSQL 15 / Oracle XE 21c 런타임 전환 |
| **React Frontend** | 20개 페이지, ECharts 통계, Tree 시각화 |

---

## 시스템 아키텍처

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                       React 19 Frontend (:3000)                              │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐          │
│  │Dashboard │ │ Upload   │ │ PA Verify│ │Cert.Search│ │Sync Mon. │          │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘          │
└──────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ↓ REST API
┌──────────────────────────────────────────────────────────────────────────────┐
│                     API Gateway (nginx :8080)                                │
│              Rate Limiting / JWT Routing / Reverse Proxy                     │
└──────────────────────────────────────────────────────────────────────────────┘
         │                    │                    │                │
         ↓                    ↓                    ↓                ↓
┌────────────────┐ ┌────────────────┐ ┌────────────────┐ ┌────────────────┐
│ PKD Management │ │  PA Service    │ │  PKD Relay     │ │  Monitoring    │
│    (:8081)     │ │   (:8082)      │ │   (:8083)      │ │   (:8084)      │
│                │ │                │ │                │ │                │
│ - File Upload  │ │ - SOD Verify   │ │ - DB-LDAP Sync │ │ - Health Check │
│ - Cert Validate│ │ - DG Hash      │ │ - Reconcile    │ │ - Metrics      │
│ - LDAP Write   │ │ - Trust Chain  │ │ - Sync Monitor │ │ - Service Mon. │
│ - Auth/RBAC    │ │ - MRZ Parse    │ │                │ │                │
│ - Cert Search  │ │ - DG2 Face     │ │                │ │                │
│ - ICAO Monitor │ │                │ │                │ │                │
│ - Audit Log    │ │                │ │                │ │                │
└────────┬───────┘ └───────┬────────┘ └───────┬────────┘ └────────────────┘
         │                 │                   │
         ↓                 ↓                   ↓
┌──────────────────────────────────────────────────────────────────────────────┐
│                         Shared Libraries (shared/lib/)                       │
│  icao::database │ icao::ldap │ icao::audit │ icao::certificate-parser       │
│  icao::config   │ icao::logging │ icao::exception │ icao::icao9303          │
└──────────────────────────────────────────────────────────────────────────────┘
         │                                     │
         ↓                                     ↓
┌─────────────────────┐          ┌─────────────────────────────────────────┐
│   PostgreSQL 15     │          │         OpenLDAP MMR Cluster            │
│   / Oracle XE 21c  │          │  ┌───────────┐      ┌───────────┐       │
│     (DB_TYPE)       │          │  │ OpenLDAP1 │◄────►│ OpenLDAP2 │       │
│                     │          │  │  :3891    │ MMR  │  :3892    │       │
│ - certificate       │          │  └───────────┘      └───────────┘       │
│ - crl               │          │         Software Load Balancing          │
│ - master_list       │          │         95+ Countries, 31,000+ Certs    │
│ - uploaded_file     │          └─────────────────────────────────────────┘
│ - pa_verification   │
│ - sync_status       │
│ - audit logs        │
└─────────────────────┘
```

---

## 기술 스택

| 카테고리 | 기술 |
|----------|------|
| **Language** | C++20 |
| **Web Framework** | Drogon 1.9+ |
| **Database** | PostgreSQL 15 / Oracle XE 21c (런타임 전환) |
| **LDAP** | OpenLDAP (MMR Cluster, Software Load Balancing) |
| **Crypto** | OpenSSL 3.x |
| **JSON** | nlohmann/json |
| **Logging** | spdlog |
| **Build** | CMake 3.20+, vcpkg |
| **Frontend** | React 19, TypeScript 5.9, Vite 7, Tailwind CSS 4 |
| **State Management** | Zustand 5, TanStack Query 5 |
| **Charts** | ECharts 6, Recharts 3 |
| **API Gateway** | nginx (Rate Limiting, JWT Routing) |
| **Containerization** | Docker, Docker Compose |
| **Auth** | JWT (HS256) + RBAC (admin/user) |

---

## 빠른 시작

### 사전 요구사항

- Docker & Docker Compose
- Git

### 설치 및 실행

```bash
# 1. 저장소 클론
git clone https://github.com/smartcore/icao-local-pkd.git
cd icao-local-pkd

# 2. 환경 변수 설정
cp .env.example .env  # 필요시 수정

# 3. Docker 컨테이너 시작
./docker-start.sh

# 4. 브라우저에서 접속
open http://localhost:3000
```

### 접속 정보

| 서비스 | URL | 설명 |
|--------|-----|------|
| **Frontend** | http://localhost:3000 | 웹 UI |
| **API Gateway** | http://localhost:8080/api | 통합 API 엔드포인트 |
| **Swagger UI** | http://localhost:8090 | API 문서 (OpenAPI) |
| **PostgreSQL** | localhost:15432 | 데이터베이스 |
| **OpenLDAP 1** | ldap://localhost:3891 | LDAP 노드 1 |
| **OpenLDAP 2** | ldap://localhost:3892 | LDAP 노드 2 |

---

## 프로젝트 구조

```
icao-local-pkd/
├── services/                          # C++ 마이크로서비스
│   ├── pkd-management/                # PKD 관리 (:8081)
│   │   ├── src/
│   │   │   ├── main.cpp              # Drogon 서버 + 라우트 정의
│   │   │   ├── repositories/         # Repository 계층 (10개)
│   │   │   ├── services/             # Service 계층 (10개)
│   │   │   ├── domain/models/        # 도메인 모델
│   │   │   ├── middleware/           # JWT Auth, RBAC
│   │   │   ├── handlers/            # SSE 핸들러
│   │   │   └── auth/                # 인증 처리
│   │   ├── Dockerfile
│   │   └── CMakeLists.txt
│   ├── pa-service/                    # PA 검증 (:8082)
│   │   ├── src/
│   │   │   ├── main.cpp
│   │   │   ├── repositories/
│   │   │   ├── services/
│   │   │   └── domain/models/
│   │   ├── Dockerfile
│   │   └── CMakeLists.txt
│   ├── pkd-relay-service/             # DB-LDAP 동기화 (:8083)
│   │   ├── src/
│   │   │   ├── main.cpp
│   │   │   ├── repositories/
│   │   │   ├── services/
│   │   │   ├── domain/models/        # 7개 도메인 모델
│   │   │   └── relay/                # Relay 전용 로직
│   │   ├── Dockerfile
│   │   └── CMakeLists.txt
│   ├── monitoring-service/            # 시스템 모니터링 (:8084)
│   └── common-lib/                    # 공유 유틸리티 라이브러리
│
├── shared/                            # 공유 C++ 라이브러리
│   └── lib/
│       ├── database/                  # DB 커넥션 풀 + Query Executor
│       │   ├── i_query_executor.h     # 인터페이스 (Strategy Pattern)
│       │   ├── postgresql_query_executor.*  # PostgreSQL 구현
│       │   ├── oracle_query_executor.*     # Oracle OCI 구현
│       │   └── *_connection_pool.*         # RAII 커넥션 풀
│       ├── ldap/                      # LDAP 커넥션 풀 (min=2, max=10)
│       ├── audit/                     # 통합 감사 로깅
│       ├── certificate-parser/        # X.509 파싱 (22 메타데이터 필드)
│       ├── icao9303/                  # ICAO 9303 SOD/DG 파서
│       ├── config/                    # 환경 설정 관리
│       ├── logging/                   # 구조화된 로깅 (spdlog)
│       └── exception/                 # 커스텀 예외 타입
│
├── frontend/                          # React 19 SPA
│   ├── src/
│   │   ├── pages/                     # 20개 페이지 컴포넌트
│   │   ├── components/                # 재사용 가능한 UI 컴포넌트
│   │   ├── api/                       # API 클라이언트
│   │   ├── services/                  # 비즈니스 로직
│   │   ├── stores/                    # Zustand 상태 관리
│   │   ├── types/                     # TypeScript 타입 정의
│   │   ├── hooks/                     # 커스텀 React 훅
│   │   └── utils/                     # 유틸리티 함수
│   ├── Dockerfile
│   └── package.json
│
├── docker/                            # Docker 설정
│   ├── docker-compose.yaml            # 메인 (11개 서비스)
│   ├── docker-compose.dev.yaml        # 개발 환경
│   ├── docker-compose.arm64.yaml      # ARM64 배포
│   ├── init-scripts/                  # PostgreSQL 초기화 SQL (9개)
│   ├── db-oracle/                     # Oracle 스키마 초기화
│   └── db/migrations/                 # DB 마이그레이션 스크립트
│
├── nginx/                             # API Gateway 설정
│   └── api-gateway.conf               # Rate Limiting, Reverse Proxy
│
├── openldap/                          # OpenLDAP 설정
│   ├── Dockerfile                     # 커스텀 이미지
│   ├── bootstrap/                     # 초기 DIT 구조
│   ├── mmr/                           # Multi-Master Replication
│   └── schemas/                       # LDAP 스키마
│
├── haproxy/                           # HAProxy 로드밸런서 (선택)
│
├── scripts/                           # 자동화 스크립트
│   ├── docker/                        # Docker 라이프사이클 관리
│   ├── build/                         # 빌드 도구
│   ├── helpers/                       # DB/LDAP 헬퍼 함수
│   ├── maintenance/                   # 데이터 관리
│   ├── monitoring/                    # ICAO 버전 체크
│   ├── deploy/                        # 배포 스크립트
│   ├── dev/                           # 개발 환경
│   └── luckfox/                       # ARM64 배포
│
├── docs/                              # 프로젝트 문서
│   ├── openapi/                       # OpenAPI 스펙 (3개 서비스)
│   └── *.md                           # 아키텍처, 설계, 단계별 보고서
│
├── tests/                             # 단위 테스트 (Catch2)
├── data/cert/                         # Trust Anchor 인증서
├── CMakeLists.txt                     # 루트 CMake 설정
└── vcpkg.json                         # C++ 의존성 매니페스트
```

---

## 운영 가이드

### 기본 커맨드

```bash
# 시스템 시작 (전체)
./docker-start.sh

# 시스템 중지
./docker-stop.sh

# 헬스 체크
./docker-health.sh

# 완전 초기화 후 재시작
./docker-clean-and-init.sh
```

### 케이스별 실행

| 상황 | 실행 순서 |
|------|----------|
| **최초 설치** | `./docker-start.sh` |
| **완전 초기화** | `./docker-clean-and-init.sh` |
| **재시작 (데이터 유지)** | `./docker-stop.sh` → `./docker-start.sh` |
| **특정 서비스 재빌드** | `./scripts/build/rebuild-pkd-relay.sh [--no-cache]` |
| **프론트엔드 재빌드** | `./scripts/build/rebuild-frontend.sh` |
| **개발 환경** | `docker compose -f docker/docker-compose.dev.yaml up -d` |

### 서비스 재빌드

```bash
# 캐시 빌드 (2-3분, 일반 코드 변경시)
./scripts/build/rebuild-pkd-relay.sh

# 클린 빌드 (20-30분, 의존성 변경시)
./scripts/build/rebuild-pkd-relay.sh --no-cache
```

`--no-cache` 사용 시점: CMakeLists.txt 변경, vcpkg.json 변경, Dockerfile 수정, 최종 배포

### 데이터 확인 헬퍼

```bash
# DB 헬퍼
source scripts/helpers/db-helpers.sh
db_count_certs          # 인증서 수량 확인
db_count_crls           # CRL 수량 확인

# LDAP 헬퍼
source scripts/helpers/ldap-helpers.sh
ldap_count_all          # 전체 LDAP 엔트리 수
ldap_list_countries     # 적재된 국가 목록
```

### 서비스 상태 모니터링

```bash
# 헬스 체크
curl http://localhost:8080/api/health
curl http://localhost:8080/api/health/database
curl http://localhost:8080/api/health/ldap

# 업로드 통계
curl http://localhost:8080/api/upload/statistics

# PA 통계
curl http://localhost:8080/api/pa/statistics

# 동기화 상태
curl http://localhost:8080/api/sync/status
```

---

## Multi-DBMS 지원

`DB_TYPE` 환경 변수로 런타임에 데이터베이스를 전환할 수 있습니다.

```bash
# PostgreSQL (권장, 10-50x 빠름)
DB_TYPE=postgres

# Oracle (엔터프라이즈)
DB_TYPE=oracle
```

### 데이터베이스 전환

```bash
# .env 파일에서 DB_TYPE 변경 후
docker compose -f docker/docker-compose.yaml restart pkd-management pa-service pkd-relay
```

### Oracle 호환성

| 이슈 | PostgreSQL | Oracle | 솔루션 |
|------|-----------|--------|--------|
| 컬럼 이름 | lowercase | UPPERCASE | OracleQueryExecutor 자동 변환 |
| Boolean | TRUE/FALSE | 1/0 | DB 인식 포매팅 |
| 페이지네이션 | LIMIT/OFFSET | OFFSET ROWS FETCH NEXT | DB별 SQL 생성 |
| 대소문자 검색 | ILIKE | UPPER() LIKE UPPER() | 조건부 SQL |
| 빈 문자열 | '' (빈 값) | NULL | IS NOT NULL 필터 |
| 타임스탬프 | NOW() | SYSTIMESTAMP | DB별 함수 |
| 커넥션 풀 | min=5, max=20 | min=2, max=10 (OCI) | 별도 풀 사이징 |

---

## API 문서

모든 API는 API Gateway(`http://localhost:8080/api`)를 통해 접근합니다.

### PKD Management API

**파일 업로드**:

| Method | Endpoint | 설명 |
|--------|----------|------|
| POST | `/api/upload/ldif` | LDIF 파일 업로드 |
| POST | `/api/upload/masterlist` | Master List 업로드 |
| GET | `/api/upload/history` | 업로드 이력 (페이지네이션) |
| GET | `/api/upload/detail/{id}` | 업로드 상세 |
| DELETE | `/api/upload/{id}` | 업로드 삭제 |
| GET | `/api/upload/statistics` | 업로드 통계 |
| GET | `/api/upload/countries` | 국가별 통계 (대시보드) |
| GET | `/api/upload/countries/detailed` | 국가별 상세 분석 |
| GET | `/api/upload/{id}/validations` | Trust Chain 검증 결과 |
| GET | `/api/upload/{id}/issues` | 중복 인증서 감지 |
| GET | `/api/upload/{id}/ldif-structure` | LDIF/ASN.1 구조 시각화 |
| GET | `/api/progress/{id}` | 업로드 진행 SSE 스트림 |

**인증서 검색**:

| Method | Endpoint | 설명 |
|--------|----------|------|
| GET | `/api/certificates/countries` | 국가 목록 |
| GET | `/api/certificates/search` | 인증서 검색 (필터: 국가, 유형, 상태) |
| GET | `/api/certificates/validation` | Fingerprint별 검증 결과 |
| GET | `/api/certificates/export/{format}` | 인증서 내보내기 |

**인증**:

| Method | Endpoint | 설명 |
|--------|----------|------|
| POST | `/api/auth/login` | 로그인 |
| POST | `/api/auth/logout` | 로그아웃 |
| POST | `/api/auth/refresh` | 토큰 갱신 |
| GET | `/api/auth/me` | 현재 사용자 정보 |
| GET | `/api/auth/users` | 사용자 관리 (admin) |

**감사 로그**:

| Method | Endpoint | 설명 |
|--------|----------|------|
| GET | `/api/auth/audit-log` | 인증 감사 로그 (admin) |
| GET | `/api/audit/operations` | 운영 감사 로그 |
| GET | `/api/audit/operations/stats` | 운영 통계 |

**ICAO 모니터링**:

| Method | Endpoint | 설명 |
|--------|----------|------|
| GET | `/api/icao/status` | ICAO 버전 상태 비교 |
| GET | `/api/icao/latest` | 최신 ICAO 버전 정보 |
| GET | `/api/icao/history` | 버전 체크 이력 |
| GET | `/api/icao/check-updates` | 수동 버전 체크 트리거 |

### PA Service API

| Method | Endpoint | 설명 |
|--------|----------|------|
| POST | `/api/pa/verify` | PA 검증 (8단계 프로세스) |
| POST | `/api/pa/parse-sod` | SOD (보안 객체) 파싱 |
| POST | `/api/pa/parse-dg1` | DG1 (MRZ) 파싱 |
| POST | `/api/pa/parse-dg2` | DG2 (얼굴 이미지) 추출 |
| POST | `/api/pa/parse-mrz-text` | MRZ 텍스트 파싱 |
| GET | `/api/pa/history` | PA 검증 이력 |
| GET | `/api/pa/statistics` | PA 통계 |
| GET | `/api/pa/{id}` | 검증 상세 |
| GET | `/api/pa/{id}/datagroups` | DataGroup 상세 |

### PKD Relay API

| Method | Endpoint | 설명 |
|--------|----------|------|
| GET | `/api/sync/status` | DB-LDAP 동기화 상태 |
| GET | `/api/sync/stats` | 동기화 통계 |
| POST | `/api/sync/check` | 수동 동기화 체크 |
| POST | `/api/sync/reconcile` | Reconciliation 실행 |
| GET | `/api/sync/reconcile/history` | Reconciliation 이력 |
| GET | `/api/sync/reconcile/{id}` | Reconciliation 상세 |
| GET | `/api/sync/reconcile/stats` | Reconciliation 통계 |

---

## Frontend

### 페이지 (20개)

| 페이지 | 라우트 | 설명 |
|--------|--------|------|
| Dashboard | `/` | 인증서 통계 대시보드 |
| FileUpload | `/upload` | LDIF/Master List 업로드 |
| CertificateSearch | `/pkd/certificates` | 인증서 검색 및 상세 |
| UploadHistory | `/upload-history` | 업로드 이력 관리 |
| UploadDetail | `/upload/:uploadId` | 업로드 상세 및 구조 시각화 |
| UploadDashboard | `/upload-dashboard` | 업로드 통계 대시보드 |
| PAVerify | `/pa/verify` | PA 검증 |
| PAHistory | `/pa/history` | PA 검증 이력 |
| PADetail | `/pa/:paId` | PA 검증 상세 |
| PADashboard | `/pa/dashboard` | PA 통계 대시보드 |
| SyncDashboard | `/sync` | DB-LDAP 동기화 모니터링 |
| IcaoStatus | `/icao` | ICAO PKD 버전 추적 |
| MonitoringDashboard | `/monitoring` | 시스템 모니터링 |
| Login | `/login` | 로그인 |
| Profile | `/profile` | 사용자 프로필 |
| UserManagement | `/admin/users` | 사용자 관리 (admin) |
| AuditLog | `/admin/audit-log` | 인증 감사 로그 |
| OperationAuditLog | `/admin/operation-audit` | 운영 감사 추적 |
| ValidationDemo | `/validation-demo` | 검증 데모 |

### 주요 컴포넌트

| 컴포넌트 | 설명 |
|----------|------|
| TreeViewer | 범용 트리 시각화 (react-arborist) |
| CountryStatisticsDialog | 국가별 인증서 분석 |
| TrustChainVisualization | Trust Chain 경로 표시 |
| DuplicateCertificatesTree | 중복 감지 (국가별 그룹핑) |
| LdifStructure / MasterListStructure | 파일 구조 시각화 |
| CertificateMetadataCard | X.509 메타데이터 상세 |
| RealTimeStatisticsPanel | 실시간 통계 (SSE) |

---

## 인증서 검증 표준

이 시스템은 다음 표준을 준수합니다:

- **ICAO Doc 9303** - Machine Readable Travel Documents
  - Part 10: Logical Data Structure (LDS)
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

#### Passive Authentication (8단계)
1. SOD 파싱 (Tag 0x77 unwrap)
2. DSC 추출
3. CSCA 조회 (LDAP)
4. Trust Chain 검증
5. SOD 서명 검증
6. Data Group 해시 검증
7. CRL 폐기 확인
8. ICAO 9303 적합성 체크

---

## LDAP DIT 구조

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data                           # 적합 인증서
│   └── c={COUNTRY}                   # 95+ 국가
│       ├── o=csca                    # CSCA 인증서
│       ├── o=mlsc                    # Master List Signer 인증서
│       ├── o=dsc                     # DSC 인증서
│       ├── o=crl                     # CRL
│       └── o=ml                      # Master List
└── dc=nc-data                        # 비적합 인증서
    └── c={COUNTRY}
        └── o=dsc                     # DSC_NC

엔트리 DN 형식: cn={SHA256_FINGERPRINT},o={TYPE},c={COUNTRY},dc=data,...
```

---

## 보안

| 항목 | 구현 |
|------|------|
| **SQL Injection** | 100% 파라미터화된 쿼리 (전체 서비스) |
| **인증** | JWT (HS256) + 토큰 갱신 |
| **인가** | RBAC (admin/user 역할) |
| **파일 업로드** | MIME 타입 검증, 경로 Sanitization |
| **감사 로그** | `auth_audit_log` (인증) + `operation_audit_log` (운영) |
| **API 제한** | nginx Rate Limiting (IP/User별) |
| **크리덴셜** | .env 외부화, 코드 내 하드코딩 없음 |
| **IP 추적** | 클라이언트 IP, User-Agent 로깅 |

---

## 설계 패턴

| 패턴 | 용도 |
|------|------|
| **Repository Pattern** | 100% SQL 추상화 - 컨트롤러에 SQL 없음 |
| **Query Executor Pattern** | DB 독립적 쿼리 실행 (IQueryExecutor) |
| **Factory Pattern** | 런타임 DB 선택 (DB_TYPE) |
| **Strategy Pattern** | 업로드 처리 모드 (AUTO/MANUAL) |
| **RAII** | 커넥션 풀 자동 반환 (DB, LDAP) |
| **Middleware** | 횡단 관심사 (Auth, Permission) |

---

## 프로덕션 데이터

| 인증서 유형 | 수량 | LDAP | 커버리지 |
|-------------|------|------|----------|
| CSCA | 845 | 845 | 100% |
| MLSC | 27 | 27 | 100% |
| DSC | 29,838 | 29,838 | 100% |
| DSC_NC | 502 | 502 | 100% |
| CRL | 69 | 69 | 100% |
| **합계** | **31,212** | **31,212** | **100%** |

---

## 빌드 (Docker 없이)

```bash
# 사전 요구사항 (Ubuntu/Debian)
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
```

---

## 문서

| 문서 | 설명 |
|------|------|
| [CLAUDE.md](CLAUDE.md) | 프로젝트 개발 가이드 (종합) |
| [DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md) | 개발 가이드 |
| [SOFTWARE_ARCHITECTURE.md](docs/SOFTWARE_ARCHITECTURE.md) | 소프트웨어 아키텍처 |
| [MASTER_LIST_PROCESSING_GUIDE.md](docs/MASTER_LIST_PROCESSING_GUIDE.md) | Master List 처리 가이드 |
| [ORACLE_AUTHENTICATION_COMPLETION.md](docs/ORACLE_AUTHENTICATION_COMPLETION.md) | Oracle 인증 구현 |
| [PA_SERVICE_REFACTORING_PROGRESS.md](docs/PA_SERVICE_REFACTORING_PROGRESS.md) | PA Service 리팩토링 |
| [LUCKFOX_DEPLOYMENT.md](docs/LUCKFOX_DEPLOYMENT.md) | ARM64 배포 가이드 |
| [OpenAPI Specs](docs/openapi/) | API 명세 (PKD, PA, Sync) |

---

## 라이선스

Proprietary - SmartCore Inc.

---

**Version**: v2.6.3
**Project Owner**: kbjung
**Organization**: SmartCore Inc.
**Last Updated**: 2026-02-11

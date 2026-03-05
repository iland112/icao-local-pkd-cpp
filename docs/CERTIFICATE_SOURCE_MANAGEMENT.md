# 인증서 출처 관리 및 LDAP DIT 저장 정책

**작성일**: 2026-03-05
**버전**: v1.1.0

---

## 1. 개요

ICAO Local PKD 시스템은 다양한 경로를 통해 인증서를 수집한다. 본 문서는 각 출처(source)의 특성, DB/LDAP 저장 정책, 그리고 출처가 다른 인증서가 동일한 LDAP DIT 경로에 저장되는 이유를 설명한다.

### 핵심 원칙

> **인증서의 LDAP DIT 위치는 인증서 유형(type)과 국가(country)에 의해 결정되며, 출처(source)에 의해 결정되지 않는다.**
> 출처 추적은 DB 레벨의 `source_type` 컬럼으로 관리한다.

---

## 2. 인증서 출처 유형

| source_type | 설명 | 수집 경로 |
|-------------|------|----------|
| `LDIF_PARSED` | ICAO PKD LDIF 파일에서 파싱 | `icaopkd-001` (CSCA, DSC, CRL), `icaopkd-003` (DSC_NC) |
| `ML_PARSED` | Master List 파일에서 추출 | `icaopkd-002` (Master List 내 CSCA/MLSC) |
| `FILE_UPLOAD` | 개별 인증서 파일 직접 업로드 | PEM, DER, P7B, DL, CRL 파일 업로드 |
| `PA_EXTRACTED` | PA 검증 시 SOD에서 DSC 추출 | `POST /api/pa/verify` 요청 시 자동 등록 |
| `DL_PARSED` | Deviation List에서 파싱 | DL 파일 업로드 |

---

## 3. LDAP DIT 저장 정책

### 3.1 DIT 구조 (인증서 유형 기반)

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data                                    ← 모든 출처의 준수 인증서
│   └── c={COUNTRY}
│       ├── o=csca   (CSCA)
│       ├── o=dsc    (DSC)        ← LDIF_PARSED, FILE_UPLOAD, PA_EXTRACTED 모두 동일 경로
│       ├── o=lc     (Link Certificate)
│       ├── o=crl    (CRL)
│       ├── o=mlsc   (MLSC)
│       └── o=ml     (Master List)
└── dc=nc-data                                 ← ICAO 비준수 인증서 (deprecated 2021)
    └── c={COUNTRY}
        └── o=dsc    (DSC_NC)
```

### 3.2 DN 구성 규칙

모든 인증서의 DN은 출처와 무관하게 동일한 규칙으로 구성된다:

```
cn={FINGERPRINT_SHA256},o={TYPE},c={COUNTRY},{CONTAINER},{BASE_DN}
```

예시:
| 인증서 | source_type | LDAP DN |
|--------|-------------|---------|
| 한국 DSC (ICAO PKD) | `LDIF_PARSED` | `cn=abc123...,o=dsc,c=KR,dc=data,...` |
| 한국 DSC (PA 추출) | `PA_EXTRACTED` | `cn=5fed6d...,o=dsc,c=KR,dc=data,...` |
| 한국 DSC (직접 업로드) | `FILE_UPLOAD` | `cn=def456...,o=dsc,c=KR,dc=data,...` |

**Fingerprint(SHA-256)가 동일하면 동일 인증서**이므로, 출처가 달라도 LDAP에 중복 저장되지 않는다.

### 3.3 출처별 분리하지 않는 이유

ICAO 표준, 상용 레퍼런스 구현(Keyfactor NPKD), 오픈소스 구현(JMRTD) 모두 출처별 DIT 분리를 하지 않는다:

| 근거 | 설명 |
|------|------|
| **ICAO 중앙 PKD** | DIT 분리 기준은 **적합성**(dc=data vs dc=nc-data)이지 소스가 아님. nc-data는 2021년 폐지 |
| **Keyfactor NPKD** (상용) | DB의 `importedFrom` 컬럼으로 출처 추적. LDAP 게시는 동일 DN 템플릿 사용 |
| **JMRTD** (오픈소스) | ICAO PKD, BSI, 커뮤니티 등 다양한 출처 인증서를 동일 DIT에 로드 |
| **ICAO PKD White Paper (2020)** | Local PKD 내부 구조는 각 국가 구현에 위임. 출처별 분리 요구사항 없음 |

**기술적 이유**:

1. **인증서 동일성은 소스와 무관** — DSC는 fingerprint(SHA-256)로 고유 식별됨. ICAO PKD에서 받든, PA 검증에서 추출하든, 바이너리 내용과 fingerprint가 동일하면 같은 인증서
2. **검사 시스템은 type + country로 조회** — 출입국 시스템이 `o=dsc,c=KR,dc=data,...`에서 DSC를 검색할 때 출처를 구분하지 않음
3. **LDAP 스키마 호환성** — pkdDownload, cRLDistributionPoint 등 표준 objectClass에 출처 속성이 없음. 커스텀 속성 추가는 상호운용성을 저해

---

## 4. PA_EXTRACTED DSC 처리 상세

### 4.1 저장 흐름

```
PA 검증 요청 (POST /api/pa/verify)
  │
  ├─ SOD에서 DSC 추출
  │
  ├─ Fingerprint(SHA-256) 계산
  │
  ├─ DB 중복 검사 (fingerprint 기반)
  │   ├─ 이미 존재 → 저장하지 않음 (대부분의 경우)
  │   └─ 미존재 → DB INSERT (source_type='PA_EXTRACTED', stored_in_ldap=FALSE)
  │
  └─ PA 검증 결과 반환 (dscAutoRegistration 필드 포함)

                    ↓ (비동기, PKD Relay Reconciliation)

PKD Relay Reconciliation
  │
  ├─ stored_in_ldap=FALSE 인증서 조회
  │
  ├─ buildDn() → cn={FINGERPRINT},o=dsc,c={COUNTRY},dc=data,...
  │
  ├─ LDAP에 추가 (addCertificate)
  │
  └─ stored_in_ldap=TRUE 업데이트
```

### 4.2 중복 방지 메커니즘

PA 검증에서 추출한 DSC는 대부분 ICAO PKD에서 이미 수신한 인증서와 동일하다.

| 단계 | 중복 방지 방법 |
|------|---------------|
| **DB 저장** | SHA-256 fingerprint 인메모리 캐시 + DB SELECT 폴백 |
| **LDAP 저장** | Reconciliation이 stored_in_ldap=FALSE만 처리 (이미 LDAP에 있으면 TRUE) |

**운영 데이터 증거** (31,428건 중):

| source_type | 건수 | 비율 | 비고 |
|-------------|------|------|------|
| `LDIF_PARSED` | 31,427 | 99.997% | ICAO PKD LDIF 파일에서 파싱 |
| `PA_EXTRACTED` | 1 | 0.003% | PA 검증에서 추출한 신규 DSC |
| `ML_PARSED` | 0 | — | ML 컬렉션은 LDIF 형식으로 배포 |
| `FILE_UPLOAD` | 0 | — | 개별 업로드 없음 |

> **참고**: ICAO PKD 컬렉션 002(Master List)도 LDIF 형식으로 배포되어 `ldif_processor.cpp`를 통해 처리되므로 `LDIF_PARSED`로 분류된다. `.ml` 파일 직접 업로드 시에만 `ML_PARSED`가 사용된다.

PA 검증으로 중복 저장된 인증서는 **0건**. 유일한 PA_EXTRACTED 1건은 ICAO PKD LDIF에 포함되지 않았던 신규 DSC.

### 4.3 PA_EXTRACTED DSC가 LDAP에 저장되는 경우

PA_EXTRACTED DSC가 LDAP에 저장되는 것은 **ICAO PKD에 없는 신규 DSC**인 경우에만 해당:

- ICAO PKD에 미참여 국가가 발급한 DSC
- 최근 발급되어 ICAO PKD에 아직 미반영된 DSC
- 양자간(bilateral) 교환으로만 배포된 DSC

이런 DSC를 LDAP에 저장하면 다른 검사 시스템에서도 PA 검증 시 해당 DSC를 조회할 수 있다.

---

## 5. 출처 추적 (DB 레벨)

### 5.1 certificate 테이블

```sql
SELECT source_type, COUNT(*) as count
FROM certificate
GROUP BY source_type;
```

| 컬럼 | 설명 |
|------|------|
| `source_type` | 인증서 출처 (LDIF_PARSED, ML_PARSED, FILE_UPLOAD, PA_EXTRACTED, DL_PARSED) |
| `upload_id` | 출처 업로드 ID (PA_EXTRACTED는 NULL) |
| `stored_in_ldap` | LDAP 동기화 상태 |
| `ldap_dn` | LDAP DN (업로드 시 설정, Reconciliation은 미설정) |

### 5.2 source_type 저장 구조

인증서 저장 시 `source_type`은 호출 경로에 따라 자동 설정된다:

| 처리 모듈 | 소스 파일 | source_type |
|-----------|----------|-------------|
| LDIF 파싱 | `ldif_processor.cpp` | `LDIF_PARSED` |
| Master List 파싱 | `masterlist_processor.cpp` | `ML_PARSED` |
| Master List (핸들러 직접 저장) | `upload_handler.cpp` | `ML_PARSED` |
| 개별 인증서 업로드 | `upload_service.cpp` | `FILE_UPLOAD` |
| PA 검증 DSC 추출 | `dsc_auto_registration_service.cpp` | `PA_EXTRACTED` |

**구현 상세**: `saveCertificateWithDuplicateCheck()` 함수의 `sourceType` 파라미터 (기본값: `"FILE_UPLOAD"`)로 INSERT 시 `source_type` 컬럼에 저장된다.

> **v2.29.0 수정**: v2.28.2 이전에는 INSERT 쿼리에 `source_type` 컬럼이 누락되어 모든 인증서가 DB DEFAULT `FILE_UPLOAD`로 저장되었다. v2.29.0에서 `sourceType` 파라미터를 추가하고 각 호출부에서 올바른 타입을 전달하도록 수정.

### 5.3 프론트엔드 활용

- **Dashboard**: "인증서 출처별 현황" 카드 (source_type별 가로 바 차트)
- **Certificate Search**: source 필터 (드롭다운)
- **Upload Statistics API**: `bySource` 필드 (GROUP BY source_type)

---

## 6. 참고 자료

### ICAO 문서

- ICAO Doc 9303 Part 12: Public Key Infrastructure (PKI)
- ICAO PKD White Paper (July 2020)
- ICAO PKD Regulations (July 2020)
- ICAO PKD Participant Guide (2021): nc-data deprecation notice

### 레퍼런스 구현

- [Keyfactor NPKD Documentation](https://docs.keyfactor.com/npkd/latest/) — 상용 National PKD 솔루션
- [JMRTD Project](https://jmrtd.org/certificates.shtml) — 오픈소스 Java MRTD 구현

### 관련 내부 문서

- [DSC_NC_HANDLING.md](DSC_NC_HANDLING.md) — DSC_NC 비준수 인증서 처리 가이드
- [PA_API_GUIDE.md](PA_API_GUIDE.md) — PA Service API 가이드 (DSC Auto-Registration 포함)
- [LDAP_QUERY_GUIDE.md](LDAP_QUERY_GUIDE.md) — LDAP 조회 가이드
- [SOFTWARE_ARCHITECTURE.md](SOFTWARE_ARCHITECTURE.md) — 시스템 아키텍처

---

**Copyright 2026 SmartCore Inc. All rights reserved.**

# PA Service API Guide for External Clients

**Version**: 2.1.9
**Last Updated**: 2026-02-23
**API Gateway**: HTTP (:80) / HTTPS (:443) / Internal (:8080)

---

## Overview

PA Service는 ICAO 9303 표준에 따른 Passive Authentication(수동 인증) 검증을 수행하는 REST API 서비스입니다. 전자여권 판독기가 연결된 외부 클라이언트 애플리케이션에서 이 API를 사용하여 여권의 진위를 검증할 수 있습니다.

**세 가지 검증/분석 방식**을 제공합니다:
- **전체 검증** (`POST /api/pa/verify`): SOD + Data Groups를 전송하여 8단계 전체 PA 검증 수행
- **간편 조회** (`POST /api/certificates/pa-lookup`): DSC Subject DN 또는 Fingerprint만으로 기존 Trust Chain 검증 결과를 즉시 조회 (v2.1.3+)
- **AI 인증서 분석** (`GET /api/ai/certificate/{fingerprint}`): ML 기반 이상 탐지 및 위험도 분석 결과 조회 (v2.1.7+)

### Base URL

**API Gateway (권장)**:
```
# HTTPS (Private CA 인증서 필요 — ca.crt를 클라이언트에 배포)
PA Service:         https://pkd.smartcoreinc.com/api/pa
PKD Management:     https://pkd.smartcoreinc.com/api
AI Analysis:        https://pkd.smartcoreinc.com/api/ai

# HTTP (내부 네트워크)
PA Service:         http://pkd.smartcoreinc.com/api/pa
PKD Management:     http://pkd.smartcoreinc.com/api
AI Analysis:        http://pkd.smartcoreinc.com/api/ai
```

**WiFi 네트워크 (SC-WiFi) 접속**:
```
# Luckfox WiFi IP (192.168.1.70) — 같은 WiFi 네트워크 내에서 접근
PA Service:         http://192.168.1.70:8080/api/pa
PKD Management:     http://192.168.1.70:8080/api
AI Analysis:        http://192.168.1.70:8080/api/ai
Frontend:           http://192.168.1.70
```

**유선 LAN (내부 네트워크) 접속**:
```
# Luckfox 유선 IP (192.168.100.10)
PA Service:         http://192.168.100.10:8080/api/pa
PKD Management:     http://192.168.100.10:8080/api
AI Analysis:        http://192.168.100.10:8080/api/ai
Frontend:           http://192.168.100.10
```

> **Note**: 모든 API 요청은 API Gateway를 통해 라우팅됩니다. HTTPS와 HTTP 모두 지원됩니다. HTTPS 사용 시 Private CA 인증서(`ca.crt`)를 클라이언트에 설치해야 합니다. 전체 검증(`/api/pa/verify`)은 PA Service로, 간편 조회(`/api/certificates/pa-lookup`)는 PKD Management로, AI 분석(`/api/ai/*`)은 AI Analysis Service로 라우팅됩니다.
>
> **⚠ 현재 상태 (2026-02-23)**: `192.168.100.11` 하드웨어 장애로 정지. `192.168.100.10` (유선) 또는 `192.168.1.70` (WiFi, SC-WiFi) 으로 접근.

### 인증

PA Service의 모든 엔드포인트는 **인증 불필요**(Public)입니다. 전자여권 판독기 등 외부 클라이언트에서 별도 인증 없이 호출할 수 있습니다.

---

## API Endpoints Summary

| # | Method | Path | Service | Description |
|---|--------|------|---------|-------------|
| 1 | `POST` | `/api/pa/verify` | PA | PA 검증 (8단계 전체 프로세스) |
| **2** | **`POST`** | **`/api/certificates/pa-lookup`** | **PKD Mgmt** | **간편 조회: DSC Subject DN/Fingerprint → Trust Chain 결과 (v2.1.3+)** |
| 3 | `POST` | `/api/pa/parse-sod` | PA | SOD 메타데이터 파싱 |
| 4 | `POST` | `/api/pa/parse-dg1` | PA | DG1 → MRZ 파싱 |
| 5 | `POST` | `/api/pa/parse-dg2` | PA | DG2 → 얼굴 이미지 추출 |
| 6 | `POST` | `/api/pa/parse-mrz-text` | PA | MRZ 텍스트 파싱 |
| 7 | `GET` | `/api/pa/history` | PA | 검증 이력 조회 |
| 8 | `GET` | `/api/pa/{id}` | PA | 검증 상세 조회 |
| 9 | `GET` | `/api/pa/{id}/datagroups` | PA | Data Groups 상세 조회 |
| 10 | `GET` | `/api/pa/statistics` | PA | 검증 통계 |
| 11 | `GET` | `/api/health` | PA | 서비스 헬스 체크 |
| 12 | `GET` | `/api/health/database` | PA | DB 연결 상태 |
| 13 | `GET` | `/api/health/ldap` | PA | LDAP 연결 상태 |
| **14** | **`GET`** | **`/api/ai/certificate/{fingerprint}`** | **AI** | **인증서 AI 분석 결과 조회 (v2.1.7+)** |
| 15 | `GET` | `/api/ai/anomalies` | AI | 이상 인증서 목록 (필터/페이지네이션) (v2.1.7+) |
| 16 | `GET` | `/api/ai/statistics` | AI | AI 분석 전체 통계 (v2.1.7+) |
| 17 | `POST` | `/api/ai/analyze` | AI | 전체 인증서 일괄 분석 실행 (v2.1.7+) |
| 18 | `GET` | `/api/ai/analyze/status` | AI | 분석 작업 진행 상태 (v2.1.7+) |
| 19 | `GET` | `/api/ai/reports/country-maturity` | AI | 국가별 PKI 성숙도 (v2.1.7+) |
| 20 | `GET` | `/api/ai/reports/algorithm-trends` | AI | 알고리즘 마이그레이션 트렌드 (v2.1.7+) |
| 21 | `GET` | `/api/ai/reports/risk-distribution` | AI | 위험 수준별 분포 (v2.1.7+) |
| 22 | `GET` | `/api/ai/reports/country/{code}` | AI | 국가별 상세 분석 (v2.1.7+) |
| 23 | `GET` | `/api/ai/health` | AI | AI 서비스 헬스 체크 (v2.1.7+) |

---

## 1. PA 검증 (Passive Authentication)

전자여권의 SOD와 Data Groups를 검증합니다. **8단계 검증 프로세스**를 수행하며, 검증 중 발견된 DSC(Document Signer Certificate)를 자동으로 Local PKD에 등록합니다.

**Endpoint**: `POST /api/pa/verify`

### 검증 프로세스 (8단계)

| Step | Name | Description |
|------|------|-------------|
| 1 | SOD Parse | SOD에서 CMS 구조, 해시 알고리즘, DG 해시 추출 |
| 2 | DSC Extract | SOD의 SignedData에서 DSC 인증서 추출 |
| 3 | Trust Chain | DSC → CSCA 신뢰 체인 검증 (공개키 서명 검증) |
| 4 | CSCA Lookup | LDAP에서 CSCA 인증서 검색 (Link Certificate 포함) |
| 5 | SOD Signature | SOD 서명 유효성 검증 |
| 6 | DG Hash | Data Group 해시값 검증 (SOD 내 기대값과 비교) |
| 7 | CRL Check | CRL 유효기간 확인 + DSC 인증서 폐지 여부 확인 |
| 8 | DSC Auto-Registration | 신규 DSC를 Local PKD에 자동 등록 (`source_type='PA_EXTRACTED'`) |

### Request

```json
{
  "sod": "<Base64 encoded SOD>",
  "dataGroups": {
    "1": "<Base64 encoded DG1>",
    "2": "<Base64 encoded DG2>",
    "14": "<Base64 encoded DG14 (optional)>"
  },
  "issuingCountry": "KR",
  "documentNumber": "M12345678",
  "requestedBy": "admin"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| sod | string | **필수** | Base64 인코딩된 SOD (Security Object Document) |
| dataGroups | object | **필수** | DG 번호를 키로, Base64 인코딩된 데이터를 값으로 하는 객체 |
| issuingCountry | string | 선택 | 국가 코드 (DSC `C=` → DG1 MRZ 순으로 자동 추출) |
| documentNumber | string | 선택 | 여권 번호 (DG1 MRZ에서 자동 추출 가능) |
| requestedBy | string | 선택 | 요청자 사용자명 (프론트엔드에서 로그인 사용자 자동 전달, 미전달 시 `anonymous`) |

> **dataGroups 형식**: 키는 `"1"`, `"2"`, `"14"` (숫자 문자열) 또는 `"DG1"`, `"DG2"`, `"DG14"` 형식 모두 지원됩니다. 배열 형식 `[{"number":"DG1","data":"..."}]`도 지원됩니다.

### Response (Success - VALID)

> **Note**: 아래 예시에서 `dscNonConformant`, `pkdConformanceCode`, `pkdConformanceText` 필드는 DSC가 ICAO PKD Non-Conformant인 경우에만 포함됩니다. 대부분의 검증에서는 이 필드가 포함되지 않습니다.

```json
{
  "success": true,
  "data": {
    "verificationId": "550e8400-e29b-41d4-a716-446655440000",
    "status": "VALID",
    "verificationTimestamp": "2026-02-13T10:30:00",
    "processingDurationMs": 245,
    "issuingCountry": "KR",
    "documentNumber": "M12345678",

    "certificateChainValidation": {
      "valid": true,
      "dscSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=Document Signer KR 01",
      "dscSerialNumber": "1A2B3C4D5E6F",
      "cscaSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=Country Signing CA KR",
      "cscaFingerprint": "SHA256:ABCD1234...",
      "countryCode": "KR",
      "notBefore": "2024-01-01T00:00:00Z",
      "notAfter": "2029-12-31T23:59:59Z",
      "crlStatus": "NOT_REVOKED",
      "crlThisUpdate": "2026-02-01T00:00:00",
      "crlNextUpdate": "2026-03-01T00:00:00",
      "dscExpired": false,
      "cscaExpired": false,
      "validAtSigningTime": true,
      "expirationStatus": "VALID",
      "expirationMessage": "",
      "dscNonConformant": true,
      "pkdConformanceCode": "ERR:CSCA.CDP.14",
      "pkdConformanceText": "The Subject Public Key Info field does not contain an rsaEncryption OID"
    },

    "sodSignatureValidation": {
      "valid": true,
      "hashAlgorithm": "SHA-256",
      "signatureAlgorithm": "SHA256withRSA",
      "algorithm": "SHA256withRSA"
    },

    "dataGroupValidation": {
      "totalGroups": 2,
      "validGroups": 2,
      "invalidGroups": 0,
      "details": {
        "DG1": { "valid": true, "expectedHash": "abc123...", "actualHash": "abc123..." },
        "DG2": { "valid": true, "expectedHash": "def456...", "actualHash": "def456..." }
      }
    },

    "dscAutoRegistration": {
      "registered": true,
      "newlyRegistered": false,
      "certificateId": "660e8400-e29b-41d4-a716-446655440099",
      "fingerprint": "a1b2c3d4e5f6...(SHA256 hex 64 chars)",
      "countryCode": "KR"
    }
  }
}
```

### Response (Failure - INVALID)

```json
{
  "success": true,
  "data": {
    "verificationId": "550e8400-e29b-41d4-a716-446655440001",
    "status": "INVALID",
    "verificationTimestamp": "2026-02-13T10:31:00",
    "processingDurationMs": 156,
    "issuingCountry": "KR",
    "documentNumber": "M12345678",

    "certificateChainValidation": {
      "valid": false,
      "message": "CSCA certificate not found for issuer",
      "dscExpired": false,
      "cscaExpired": false,
      "validAtSigningTime": false,
      "expirationStatus": "VALID",
      "expirationMessage": ""
    },

    "sodSignatureValidation": {
      "valid": true,
      "hashAlgorithm": "SHA-256",
      "signatureAlgorithm": "SHA256withRSA"
    },

    "dataGroupValidation": {
      "totalGroups": 2,
      "validGroups": 2,
      "invalidGroups": 0,
      "details": {
        "DG1": { "valid": true, "expectedHash": "abc123...", "actualHash": "abc123..." },
        "DG2": { "valid": true, "expectedHash": "def456...", "actualHash": "def456..." }
      }
    }
  }
}
```

### Response (Error - SOD 파싱 실패 등)

```json
{
  "success": false,
  "error": "SOD parsing failed: Invalid CMS structure"
}
```

### Certificate Chain Validation Fields

| Field | Type | Description |
|-------|------|-------------|
| valid | boolean | 인증서 체인 검증 성공 여부 |
| dscSubject | string | DSC 인증서 Subject DN |
| dscSerialNumber | string | DSC 인증서 시리얼 번호 |
| cscaSubject | string | CSCA 인증서 Subject DN |
| cscaFingerprint | string | CSCA 인증서 SHA256 지문 |
| countryCode | string | 국가 코드 (추출 우선순위: 요청 파라미터 → DG1 MRZ → DSC issuer `C=` → `"XX"`) |
| notBefore | string | DSC 인증서 유효 시작일 |
| notAfter | string | DSC 인증서 유효 종료일 |
| crlStatus | string | CRL 상태: `NOT_REVOKED`, `REVOKED`, `CRL_EXPIRED`, `UNKNOWN` |
| crlThisUpdate | string | CRL 발행일 (ISO 8601, 예: `2026-02-01T00:00:00`) |
| crlNextUpdate | string | CRL 다음 업데이트 예정일 (ISO 8601, 예: `2026-03-01T00:00:00`) |
| dscExpired | boolean | DSC 인증서 만료 여부 |
| cscaExpired | boolean | CSCA 인증서 만료 여부 |
| validAtSigningTime | boolean | 여권 서명 당시 인증서 유효 여부 (Point-in-Time Validation) |
| expirationStatus | string | 만료 상태: `VALID`, `WARNING`, `EXPIRED` |
| expirationMessage | string | 만료 상태 설명 메시지 |
| dscNonConformant | boolean | DSC가 ICAO PKD Non-Conformant(비준수)인 경우 `true` (v2.1.4+, 해당 시에만 포함) |
| pkdConformanceCode | string | ICAO PKD 비준수 사유 코드 (예: `ERR:CSCA.CDP.14`) (v2.1.4+, `dscNonConformant=true` 시에만 포함) |
| pkdConformanceText | string | ICAO PKD 비준수 사유 설명 (v2.1.4+, `dscNonConformant=true` 시에만 포함) |

### DSC Auto-Registration Fields (v2.1.0+)

| Field | Type | Description |
|-------|------|-------------|
| registered | boolean | DSC 등록 성공 여부 |
| newlyRegistered | boolean | `true`: 신규 등록, `false`: 이미 존재 |
| certificateId | string (UUID) | DB 인증서 레코드 ID |
| fingerprint | string | DSC SHA-256 지문 (hex, 64자) |
| countryCode | string | DSC 국가 코드 |

> **Note**: `dscAutoRegistration` 필드는 DSC 자동 등록이 성공한 경우에만 포함됩니다. 자동 등록은 PA 검증 결과에 영향을 주지 않습니다 (검증이 INVALID여도 DSC 등록은 시도됩니다).

> **Point-in-Time Validation (v1.2.0+)**: ICAO 9303 표준에 따라, 인증서가 현재 만료되었더라도 여권 서명 당시에 유효했다면 `validAtSigningTime`이 `true`로 설정됩니다. 이 경우 `expirationStatus`는 `EXPIRED`이지만 검증은 성공(`valid: true`)할 수 있습니다.

> **DSC Non-Conformant 상태 (v2.1.4+)**: DSC가 ICAO PKD의 비준수(Non-Conformant) 인증서로 분류된 경우 `dscNonConformant`, `pkdConformanceCode`, `pkdConformanceText` 필드가 응답에 포함됩니다. Non-Conformant는 ICAO Doc 9303 기술 사양 비준수를 의미하며, 인증서의 유효성과는 독립적입니다. 검증 결과(`valid`)는 Trust Chain, 서명 검증, CRL 상태에 의해 결정됩니다. 자세한 내용은 [DSC_NC_HANDLING.md](DSC_NC_HANDLING.md)를 참조하세요.

---

## 2. 간편 조회 - Trust Chain Lookup (v2.1.3+)

SOD/DG 파일을 전송하지 않고, DSC의 Subject DN 또는 Fingerprint만으로 기존에 수행된 Trust Chain 검증 결과를 DB에서 즉시 조회합니다.

**Endpoint**: `POST /api/certificates/pa-lookup`

> **Note**: 이 엔드포인트는 PKD Management 서비스에서 제공됩니다 (PA Service가 아님). API Gateway를 통해 `/api/certificates/pa-lookup`으로 접근합니다.

### 전체 검증 vs 간편 조회

| 항목 | 전체 검증 (`/api/pa/verify`) | 간편 조회 (`/api/certificates/pa-lookup`) |
|------|--------------------------|---------------------------------------|
| 입력 | SOD + Data Groups (Base64) | Subject DN 또는 Fingerprint (문자열) |
| 처리 | CMS 파싱, 서명 검증, 해시 비교 | DB 조회 (단순 SELECT) |
| 응답 시간 | 100~500ms | 5~20ms |
| SOD 서명 검증 | O | X (기존 결과 참조) |
| DG 해시 검증 | O | X (해당 없음) |
| Trust Chain 결과 | 실시간 검증 | 파일 업로드 시 수행된 결과 조회 |
| DSC 자동 등록 | O | X |
| 사용 시나리오 | 최초 여권 검증 | 이미 알려진 DSC의 상태 확인 |

### Request

Subject DN으로 조회:
```json
{
  "subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234"
}
```

Fingerprint로 조회:
```json
{
  "fingerprint": "a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| subjectDn | string | 택1 필수 | DSC Subject DN (대소문자 무시 비교) |
| fingerprint | string | 택1 필수 | DSC SHA-256 Fingerprint (hex, 64자) |

> `subjectDn`과 `fingerprint` 중 하나만 제공하면 됩니다. 둘 다 제공된 경우 `subjectDn`이 우선 적용됩니다.

### Response (Success - 검증 결과 존재)

**Conformant DSC (일반적인 경우)**:
```json
{
  "success": true,
  "validation": {
    "id": "660e8400-e29b-41d4-a716-446655440001",
    "certificateType": "DSC",
    "countryCode": "KR",
    "subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234",
    "issuerDn": "/C=KR/O=Government of Korea/CN=Country Signing CA KR",
    "serialNumber": "1A2B3C4D",
    "validationStatus": "VALID",
    "trustChainValid": true,
    "trustChainMessage": "",
    "trustChainPath": "DSC → CSCA",
    "cscaFound": true,
    "cscaSubjectDn": "/C=KR/O=Government of Korea/CN=Country Signing CA KR",
    "signatureValid": true,
    "signatureAlgorithm": "SHA256withRSA",
    "validityPeriodValid": true,
    "notBefore": "2024-01-01 00:00:00",
    "notAfter": "2029-12-31 23:59:59",
    "revocationStatus": "not_revoked",
    "crlChecked": true,
    "fingerprintSha256": "a1b2c3d4e5f6...",
    "validatedAt": "2026-02-14T10:30:00"
  }
}
```

**Non-Conformant DSC_NC (v2.1.4+)**:
```json
{
  "success": true,
  "validation": {
    "id": "660e8400-e29b-41d4-a716-446655440002",
    "certificateType": "DSC_NC",
    "countryCode": "DE",
    "subjectDn": "/C=DE/O=Federal Republic of Germany/CN=Document Signer DE 42",
    "issuerDn": "/C=DE/O=Federal Republic of Germany/CN=Country Signing CA DE",
    "serialNumber": "5A6B7C8D",
    "validationStatus": "VALID",
    "trustChainValid": true,
    "trustChainPath": "DSC → CSCA",
    "cscaFound": true,
    "cscaSubjectDn": "/C=DE/O=Federal Republic of Germany/CN=Country Signing CA DE",
    "signatureValid": true,
    "signatureAlgorithm": "SHA256withECDSA",
    "validityPeriodValid": true,
    "notBefore": "2020-01-01 00:00:00",
    "notAfter": "2025-12-31 23:59:59",
    "revocationStatus": "not_revoked",
    "crlChecked": true,
    "fingerprintSha256": "e5f6a7b8c9d0...",
    "validatedAt": "2026-02-14T11:00:00",
    "pkdConformanceCode": "ERR:CSCA.CDP.14",
    "pkdConformanceText": "The Subject Public Key Info field does not contain an rsaEncryption OID",
    "pkdVersion": "90"
  }
}
```

### Response (Not Found - DSC가 DB에 없음)

```json
{
  "success": true,
  "validation": null,
  "message": "No validation result found for the given subjectDn"
}
```

### Response Fields

| Field | Type | Description |
|-------|------|-------------|
| validationStatus | string | `VALID`, `EXPIRED_VALID`, `INVALID`, `PENDING`, `ERROR` |
| trustChainValid | boolean | DSC → CSCA 신뢰 체인 검증 성공 여부 |
| trustChainPath | string | 신뢰 체인 경로 (예: "DSC → Link → CSCA") |
| cscaFound | boolean | CSCA 인증서 검색 성공 여부 |
| signatureValid | boolean | DSC 서명 검증 성공 여부 |
| crlChecked | boolean | CRL 검사 수행 여부 |
| revocationStatus | string | 폐지 상태: `not_revoked`, `revoked`, `unknown` |
| fingerprintSha256 | string | DSC SHA-256 지문 (hex, 64자) |
| pkdConformanceCode | string | ICAO PKD 비준수 사유 코드 (v2.1.4+, `certificateType="DSC_NC"` 시에만 포함) |
| pkdConformanceText | string | ICAO PKD 비준수 사유 설명 (v2.1.4+, `certificateType="DSC_NC"` 시에만 포함) |
| pkdVersion | string | ICAO PKD 버전 (v2.1.4+, `certificateType="DSC_NC"` 시에만 포함) |

---

## 3. SOD 파싱 {#sod-parse}

SOD 메타데이터를 추출합니다 (검증 없이 파싱만 수행).

**Endpoint**: `POST /api/pa/parse-sod`

### Request

```json
{
  "sod": "<Base64 encoded SOD>"
}
```

### Response

```json
{
  "success": true,
  "sodSize": 4096,
  "hashAlgorithm": "SHA-256",
  "hashAlgorithmOid": "2.16.840.1.101.3.4.2.1",
  "signatureAlgorithm": "SHA256withRSA",
  "hasIcaoWrapper": true,
  "dataGroupCount": 5,

  "dscCertificate": {
    "subjectDn": "/C=KR/O=Ministry of Foreign Affairs/CN=DSC KR 01",
    "issuerDn": "/C=KR/O=Ministry of Foreign Affairs/CN=CSCA KR",
    "serialNumber": "1A2B3C4D",
    "notBefore": "Jan 01 00:00:00 2024 GMT",
    "notAfter": "Dec 31 23:59:59 2029 GMT",
    "countryCode": "KR"
  },

  "containedDataGroups": [
    { "dgNumber": 1, "dgName": "DG1", "hashValue": "abc123...", "hashLength": 32 },
    { "dgNumber": 2, "dgName": "DG2", "hashValue": "def456...", "hashLength": 32 },
    { "dgNumber": 11, "dgName": "DG11", "hashValue": "ghi789...", "hashLength": 32 },
    { "dgNumber": 12, "dgName": "DG12", "hashValue": "jkl012...", "hashLength": 32 },
    { "dgNumber": 14, "dgName": "DG14", "hashValue": "mno345...", "hashLength": 32 }
  ],

  "hasDg14": true,
  "hasDg15": false
}
```

---

## 4. DG1 파싱 (MRZ)

DG1에서 MRZ 정보를 추출합니다.

**Endpoint**: `POST /api/pa/parse-dg1`

### Request

```json
{
  "dg1": "<Base64 encoded DG1>"
}
```

> `"dg1Base64"` 키도 지원됩니다.

### Response

```json
{
  "success": true,
  "mrzLine1": "P<KORKIM<<MINHO<<<<<<<<<<<<<<<<<<<<<<<<<<<<",
  "mrzLine2": "M123456784KOR9005151M3005148<<<<<<<<<<<<<<02",
  "mrzFull": "P<KORKIM<<MINHO<<<<<<<<<<<<<<<<<<<<<<<<<<<<M123456784KOR9005151M3005148<<<<<<<<<<<<<<02",

  "documentType": "P",
  "issuingCountry": "KOR",
  "surname": "KIM",
  "givenNames": "MINHO",
  "fullName": "KIM MINHO",
  "documentNumber": "M12345678",
  "nationality": "KOR",
  "dateOfBirth": "1990-05-15",
  "dateOfBirthRaw": "900515",
  "sex": "M",
  "dateOfExpiry": "2030-05-14",
  "dateOfExpiryRaw": "300514",
  "optionalData1": ""
}
```

---

## 5. DG2 파싱 (얼굴 이미지)

DG2에서 얼굴 이미지를 추출합니다.

**Endpoint**: `POST /api/pa/parse-dg2`

### Request

```json
{
  "dg2": "<Base64 encoded DG2>"
}
```

### Response

```json
{
  "success": true,
  "dg2Size": 15000,
  "faceCount": 1,
  "hasFacContainer": true,
  "biometricTemplateFound": true,

  "faceImages": [
    {
      "index": 1,
      "imageFormat": "JPEG",
      "originalFormat": "JPEG2000",
      "imageSize": 12500,
      "imageOffset": 245,
      "width": 480,
      "height": 640,
      "imageDataUrl": "data:image/jpeg;base64,/9j/4AAQ..."
    }
  ]
}
```

> **JPEG2000 자동 변환 (v2.1.0+)**: 브라우저에서 JPEG2000을 렌더링할 수 없으므로, DG2에 포함된 JPEG2000 이미지는 서버에서 자동으로 JPEG로 변환됩니다. `imageFormat`은 항상 `"JPEG"`이며, 원본이 JPEG2000인 경우 `originalFormat`에 `"JPEG2000"`이 표시됩니다.

---

## 6. MRZ 텍스트 파싱

OCR로 읽은 MRZ 텍스트를 파싱합니다.

**Endpoint**: `POST /api/pa/parse-mrz-text`

### Request

```json
{
  "mrz": "P<KORKIM<<MINHO<<<<<<<<<<<<<<<<<<<<<<<<<<<<M123456784KOR9005151M3005148<<<<<<<<<<<<<<02"
}
```

> `"mrzText"` 키도 지원됩니다.

### Response

DG1 파싱과 동일한 MRZ 필드 형식

---

## 7. 검증 이력 조회

**Endpoint**: `GET /api/pa/history`

### Query Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| page | integer | 0 | 페이지 번호 (0부터 시작) |
| size | integer | 20 | 페이지 크기 |
| status | string | - | 상태 필터 (`VALID`, `INVALID`) |
| issuingCountry | string | - | 국가 코드 필터 |

### Response

```json
{
  "total": 150,
  "items": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "verifiedAt": "2026-02-13T10:30:00",
      "issuingCountry": "KR",
      "documentType": "P",
      "documentNumber": "M12345678",
      "status": "VALID",
      "processingDurationMs": 245,
      "certificateChainValid": true,
      "sodSignatureValid": true,
      "dataGroupsValid": true,
      "dscSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=DSC KR 01",
      "cscaSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=CSCA KR",
      "crlStatus": "NOT_REVOKED"
    }
  ]
}
```

---

## 8. 검증 상세 조회

**Endpoint**: `GET /api/pa/{verificationId}`

### Response

검증 레코드의 전체 정보를 반환합니다 (DB에 저장된 모든 필드 포함).

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "documentNumber": "M12345678",
  "issuingCountry": "KR",
  "verificationStatus": "VALID",
  "dscSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=DSC KR 01",
  "dscSerialNumber": "1A2B3C4D5E6F",
  "dscExpired": false,
  "cscaSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=CSCA KR",
  "cscaSerialNumber": "AABB1122",
  "cscaExpired": false,
  "certificateChainValid": true,
  "sodSignatureValid": true,
  "dataGroupsValid": true,
  "crlChecked": true,
  "revoked": false,
  "crlStatus": "NOT_REVOKED",
  "expirationStatus": "VALID",
  "sodHash": "a1b2c3d4...",
  "createdAt": "2026-02-13T10:30:00"
}
```

---

## 9. Data Groups 상세 조회

**Endpoint**: `GET /api/pa/{verificationId}/datagroups`

### Response

```json
{
  "verificationId": "550e8400-e29b-41d4-a716-446655440000",
  "hasDg1": true,
  "hasDg2": true,

  "dg1": {
    "documentType": "P",
    "issuingCountry": "KOR",
    "surname": "KIM",
    "givenNames": "MINHO",
    "documentNumber": "M12345678",
    "dateOfBirth": "1990-05-15",
    "sex": "M",
    "dateOfExpiry": "2030-05-14"
  },

  "dg2": {
    "faceCount": 1,
    "faceImages": [
      {
        "index": 1,
        "imageFormat": "JPEG",
        "width": 480,
        "height": 640,
        "imageDataUrl": "data:image/jpeg;base64,/9j/4AAQ..."
      }
    ]
  }
}
```

> **Note**: DG2 얼굴 이미지는 JPEG2000인 경우 자동으로 JPEG로 변환되어 반환됩니다.

---

## 10. 검증 통계

**Endpoint**: `GET /api/pa/statistics`

### Response

```json
{
  "totalVerifications": 1500,
  "successRate": 90.0,
  "byCountry": [
    { "country": "KR", "count": 500 },
    { "country": "JP", "count": 300 },
    { "country": "US", "count": 200 }
  ],
  "byStatus": {
    "VALID": 1350,
    "INVALID": 150
  }
}
```

---

## 11. 헬스 체크

### 서비스 상태

**Endpoint**: `GET /api/health`

```json
{
  "service": "pa-service",
  "status": "UP",
  "version": "2.1.1",
  "timestamp": "2026-02-13T10:30:00Z"
}
```

### 데이터베이스 상태

**Endpoint**: `GET /api/health/database`

```json
{
  "status": "UP",
  "database": "PostgreSQL 15.x",
  "responseTimeMs": 5
}
```

### LDAP 상태

**Endpoint**: `GET /api/health/ldap`

```json
{
  "status": "UP",
  "host": "openldap1:389",
  "responseTimeMs": 3
}
```

---

## 12. AI 인증서 분석 (v2.1.7+)

ML 기반 인증서 이상 탐지 및 패턴 분석 결과를 조회합니다. PA 검증과 독립적으로 전체 Local PKD 인증서에 대한 분석을 수행하며, Isolation Forest + Local Outlier Factor 이중 모델로 이상치를 탐지합니다.

> **Note**: AI Analysis 엔드포인트는 별도의 AI Analysis Service에서 제공됩니다. 모든 엔드포인트는 **인증 불필요**(Public)입니다.

### 12.1 개별 인증서 AI 분석 결과

PA 검증 후 DSC의 fingerprint로 해당 인증서의 AI 분석 결과를 조회할 수 있습니다.

**Endpoint**: `GET /api/ai/certificate/{fingerprint}`

```json
{
  "fingerprint": "a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd",
  "certificate_type": "DSC",
  "country_code": "KR",
  "anomaly_score": 0.12,
  "anomaly_label": "NORMAL",
  "risk_score": 15.0,
  "risk_level": "LOW",
  "risk_factors": {
    "algorithm": 5,
    "key_size": 10
  },
  "anomaly_explanations": [
    "국가 평균 대비 유효기간 편차: 평균 대비 1.2σ 낮음",
    "키 크기: 평균 대비 0.8σ 낮음"
  ],
  "analyzed_at": "2026-02-21T03:00:05"
}
```

| Field | Type | Description |
|-------|------|-------------|
| anomaly_score | float | 이상 점수 0.0 (정상) ~ 1.0 (이상) |
| anomaly_label | string | `NORMAL` (<0.3), `SUSPICIOUS` (0.3~0.7), `ANOMALOUS` (≥0.7) |
| risk_score | float | 위험 점수 0 ~ 100 (복합 점수) |
| risk_level | string | `LOW` (0~25), `MEDIUM` (26~50), `HIGH` (51~75), `CRITICAL` (76~100) |
| risk_factors | object | 위험 기여 요인 (algorithm, key_size, compliance, validity, extensions, anomaly) |
| anomaly_explanations | list | 이상치 설명 — 상위 5개 기여 피처와 sigma 편차 (한국어) |

### 12.2 이상 인증서 목록

**Endpoint**: `GET /api/ai/anomalies`

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| country | string | - | 국가 코드 필터 |
| type | string | - | 인증서 유형 필터 (`CSCA`, `DSC`, `DSC_NC`, `MLSC`) |
| label | string | - | 이상 수준 필터 (`NORMAL`, `SUSPICIOUS`, `ANOMALOUS`) |
| risk_level | string | - | 위험 수준 필터 (`LOW`, `MEDIUM`, `HIGH`, `CRITICAL`) |
| page | integer | 1 | 페이지 번호 (1부터 시작) |
| size | integer | 20 | 페이지 크기 (최대 100) |

```json
{
  "success": true,
  "items": [
    {
      "fingerprint": "dd4ba0c9...",
      "certificate_type": "DSC",
      "country_code": "ID",
      "anomaly_score": 0.80,
      "anomaly_label": "ANOMALOUS",
      "risk_score": 47.0,
      "risk_level": "MEDIUM",
      "risk_factors": {"algorithm": 5, "key_size": 10, "validity": 15, "extensions": 5, "anomaly": 12.0},
      "anomaly_explanations": ["국가 평균 대비 유효기간 편차: 평균 대비 8.4σ 낮음", "..."],
      "analyzed_at": "2026-02-21T03:00:05"
    }
  ],
  "total": 31212,
  "page": 1,
  "size": 20
}
```

### 12.3 전체 분석 통계

**Endpoint**: `GET /api/ai/statistics`

```json
{
  "total_analyzed": 31212,
  "normal_count": 27305,
  "suspicious_count": 3905,
  "anomalous_count": 2,
  "risk_distribution": {"LOW": 22396, "MEDIUM": 7405, "HIGH": 919, "CRITICAL": 492},
  "avg_risk_score": 24.75,
  "top_anomalous_countries": [
    {"country": "ID", "total": 19, "anomalous": 2, "anomaly_rate": 0.1053}
  ],
  "last_analysis_at": "2026-02-21T03:00:05",
  "model_version": "1.0.0"
}
```

### 12.4 분석 실행 및 상태 확인

**분석 실행**: `POST /api/ai/analyze`

```json
{"success": true, "message": "Analysis started"}
```

> 분석은 비동기 백그라운드로 실행됩니다. 이미 실행 중이면 `409 Conflict`가 반환됩니다.

**진행 상태**: `GET /api/ai/analyze/status`

```json
{
  "status": "RUNNING",
  "progress": 0.65,
  "total_certificates": 31212,
  "processed_certificates": 20000,
  "started_at": "2026-02-21T03:00:00Z",
  "completed_at": null,
  "error_message": null
}
```

| status | Description |
|--------|-------------|
| `IDLE` | 분석 미실행 또는 초기 상태 |
| `RUNNING` | 분석 진행 중 |
| `COMPLETED` | 분석 완료 |
| `FAILED` | 분석 실패 (`error_message`에 사유 표시) |

### 12.5 리포트 API

| Endpoint | Description |
|----------|-------------|
| `GET /api/ai/reports/country-maturity` | 국가별 PKI 성숙도 순위 (알고리즘, 키 크기, 준수성, 확장, 만료율 5개 차원) |
| `GET /api/ai/reports/algorithm-trends` | 연도별 서명 알고리즘 사용 추이 (SHA-1→SHA-256→SHA-384 마이그레이션) |
| `GET /api/ai/reports/key-size-distribution` | 알고리즘 군별 키 크기 분포 (RSA 2048/4096, ECDSA 256/384/521) |
| `GET /api/ai/reports/risk-distribution` | 위험 수준별 인증서 분포 (LOW/MEDIUM/HIGH/CRITICAL) |
| `GET /api/ai/reports/country/{code}` | 특정 국가 상세 분석 (성숙도, 위험/이상 분포, 상위 이상 인증서) |

---

## PA + AI Analysis 연동 활용

PA 검증 결과와 AI 분석 결과를 연동하여 인증서의 종합적인 신뢰도를 평가할 수 있습니다:

1. `POST /api/pa/verify` → PA 검증 수행, DSC fingerprint 획득
2. `GET /api/ai/certificate/{fingerprint}` → 해당 DSC의 AI 이상 탐지 결과 조회
3. PA 검증 결과 (VALID/INVALID) + AI 위험 수준 (LOW~CRITICAL)을 종합 판단

```python
# PA 검증 후 AI 분석 결합 예시
pa_result = client.verify(sod, {1: dg1, 2: dg2})
if pa_result["success"]:
    dsc_reg = pa_result["data"].get("dscAutoRegistration", {})
    fingerprint = dsc_reg.get("fingerprint")
    if fingerprint:
        ai_result = requests.get(f"{base_url}/ai/certificate/{fingerprint}").json()
        print(f"PA: {pa_result['data']['status']}, AI Risk: {ai_result['risk_level']}")
```

---

## Integration Examples

### Python (requests)

```python
import requests
import base64

class PAServiceClient:
    def __init__(self, base_url="http://localhost:8080/api"):
        self.base_url = base_url

    def verify(self, sod: bytes, data_groups: dict) -> dict:
        """
        Perform PA verification.

        Args:
            sod: Raw SOD bytes
            data_groups: Dict mapping DG number (int) to raw bytes
        Returns:
            dict: {"success": true, "data": {"status": "VALID"|"INVALID", ...}}
        """
        request = {
            "sod": base64.b64encode(sod).decode('utf-8'),
            "dataGroups": {
                str(num): base64.b64encode(data).decode('utf-8')
                for num, data in data_groups.items()
            }
        }

        response = requests.post(
            f"{self.base_url}/pa/verify",
            json=request,
            headers={"Content-Type": "application/json"}
        )

        response.raise_for_status()
        return response.json()

    def pa_lookup(self, subject_dn: str = None, fingerprint: str = None) -> dict:
        """
        Lightweight PA lookup by subject DN or fingerprint.
        Returns pre-computed trust chain validation result from DB.

        Args:
            subject_dn: DSC Subject DN (e.g., "/C=KR/O=.../CN=...")
            fingerprint: DSC SHA-256 fingerprint (hex, 64 chars)
        Returns:
            dict: {"success": true, "validation": {...}} or {"success": true, "validation": null}
        """
        params = {}
        if subject_dn:
            params["subjectDn"] = subject_dn
        elif fingerprint:
            params["fingerprint"] = fingerprint
        else:
            raise ValueError("Either subject_dn or fingerprint is required")

        response = requests.post(
            f"{self.base_url}/certificates/pa-lookup",
            json=params,
            headers={"Content-Type": "application/json"}
        )
        response.raise_for_status()
        return response.json()

    def parse_sod(self, sod: bytes) -> dict:
        """Parse SOD metadata."""
        response = requests.post(
            f"{self.base_url}/pa/parse-sod",
            json={"sod": base64.b64encode(sod).decode('utf-8')}
        )
        return response.json()

    def parse_dg1(self, dg1: bytes) -> dict:
        """Parse MRZ from DG1."""
        response = requests.post(
            f"{self.base_url}/pa/parse-dg1",
            json={"dg1": base64.b64encode(dg1).decode('utf-8')}
        )
        return response.json()

    def parse_dg2(self, dg2: bytes) -> dict:
        """Extract face image from DG2 (JPEG2000 auto-converted to JPEG)."""
        response = requests.post(
            f"{self.base_url}/pa/parse-dg2",
            json={"dg2": base64.b64encode(dg2).decode('utf-8')}
        )
        return response.json()

    def get_history(self, page=0, size=20, status=None, country=None) -> dict:
        """Get verification history."""
        params = {"page": page, "size": size}
        if status:
            params["status"] = status
        if country:
            params["issuingCountry"] = country
        response = requests.get(f"{self.base_url}/pa/history", params=params)
        return response.json()

    def get_statistics(self) -> dict:
        """Get verification statistics."""
        response = requests.get(f"{self.base_url}/pa/statistics")
        return response.json()

    # --- AI Analysis API (v2.1.7+) ---

    def get_ai_analysis(self, fingerprint: str) -> dict:
        """
        Get AI anomaly detection result for a specific certificate.

        Args:
            fingerprint: Certificate SHA-256 fingerprint (hex, 64 chars)
        Returns:
            dict: {"fingerprint": "...", "anomaly_score": 0.12, "risk_level": "LOW", ...}
        """
        response = requests.get(f"{self.base_url}/ai/certificate/{fingerprint}")
        if response.status_code == 404:
            return None  # Analysis not yet run for this certificate
        response.raise_for_status()
        return response.json()

    def get_ai_statistics(self) -> dict:
        """Get overall AI analysis statistics."""
        response = requests.get(f"{self.base_url}/ai/statistics")
        return response.json()

    def trigger_ai_analysis(self) -> dict:
        """Trigger full certificate analysis (runs in background)."""
        response = requests.post(f"{self.base_url}/ai/analyze")
        return response.json()

    def get_ai_analysis_status(self) -> dict:
        """Get current AI analysis job status."""
        response = requests.get(f"{self.base_url}/ai/analyze/status")
        return response.json()

    def get_ai_anomalies(self, country=None, label=None, risk_level=None,
                         page=1, size=20) -> dict:
        """Get list of anomalous certificates with filters."""
        params = {"page": page, "size": size}
        if country:
            params["country"] = country
        if label:
            params["label"] = label
        if risk_level:
            params["risk_level"] = risk_level
        response = requests.get(f"{self.base_url}/ai/anomalies", params=params)
        return response.json()


# Usage example
if __name__ == "__main__":
    client = PAServiceClient()

    # Read passport data from reader
    sod = read_sod_from_passport()
    dg1 = read_dg1_from_passport()
    dg2 = read_dg2_from_passport()

    # Option A: Full PA verification (with SOD + DG files)
    result = client.verify(sod, {1: dg1, 2: dg2})

    if result["success"] and result["data"]["status"] == "VALID":
        print("Passport verification successful!")
        data = result["data"]
        print(f"Country: {data['issuingCountry']}")
        print(f"Document: {data['documentNumber']}")

        # Check DSC non-conformant status (v2.1.4+)
        chain = data.get("certificateChainValidation", {})
        if chain.get("dscNonConformant"):
            print(f"⚠ DSC Non-Conformant: {chain['pkdConformanceCode']}")
            print(f"  Reason: {chain['pkdConformanceText']}")

        # Check DSC auto-registration
        if "dscAutoRegistration" in data:
            reg = data["dscAutoRegistration"]
            if reg["newlyRegistered"]:
                print(f"New DSC registered: {reg['fingerprint'][:16]}...")
    else:
        print("Verification failed!")
        print(f"Error: {result.get('error', 'INVALID status')}")

    # Option B: Lightweight lookup (DSC subject DN only, no file upload)
    lookup = client.pa_lookup(
        subject_dn="/C=KR/O=Government of Korea/CN=Document Signer 1234"
    )
    if lookup["success"] and lookup.get("validation"):
        v = lookup["validation"]
        print(f"Trust Chain: {'VALID' if v['trustChainValid'] else 'INVALID'}")
        print(f"Status: {v['validationStatus']}")
        print(f"CSCA: {v.get('cscaSubjectDn', 'N/A')}")

        # Check non-conformant status (v2.1.4+)
        if v.get("certificateType") == "DSC_NC":
            print(f"⚠ Non-Conformant DSC: {v.get('pkdConformanceCode', 'N/A')}")
            print(f"  Reason: {v.get('pkdConformanceText', 'N/A')}")
    else:
        print("DSC not found in local PKD")

    # Option C: AI analysis after PA verification (v2.1.7+)
    if result["success"] and result["data"]["status"] == "VALID":
        dsc_reg = result["data"].get("dscAutoRegistration", {})
        fingerprint = dsc_reg.get("fingerprint")
        if fingerprint:
            ai = client.get_ai_analysis(fingerprint)
            if ai:
                print(f"AI Risk Level: {ai['risk_level']} (score: {ai['risk_score']})")
                print(f"Anomaly: {ai['anomaly_label']} (score: {ai['anomaly_score']:.2f})")
                if ai.get("risk_factors"):
                    for factor, score in ai["risk_factors"].items():
                        print(f"  - {factor}: {score}")
                if ai.get("anomaly_explanations"):
                    for explanation in ai["anomaly_explanations"]:
                        print(f"  📋 {explanation}")

    # Check AI analysis statistics
    stats = client.get_ai_statistics()
    print(f"Total analyzed: {stats['total_analyzed']}")
    print(f"Anomalous: {stats['anomalous_count']}")
    print(f"Avg risk score: {stats['avg_risk_score']}")
```

### Java (Spring RestTemplate)

```java
import org.springframework.web.client.RestTemplate;
import org.springframework.http.*;

public class PAServiceClient {
    private final RestTemplate restTemplate = new RestTemplate();
    private final String baseUrl = "http://localhost:8080/api";

    public Map<String, Object> verify(byte[] sod, Map<Integer, byte[]> dataGroups) {
        Map<String, Object> request = new HashMap<>();
        request.put("sod", Base64.getEncoder().encodeToString(sod));

        Map<String, String> dgMap = new HashMap<>();
        dataGroups.forEach((num, data) ->
            dgMap.put(String.valueOf(num), Base64.getEncoder().encodeToString(data))
        );
        request.put("dataGroups", dgMap);

        HttpHeaders headers = new HttpHeaders();
        headers.setContentType(MediaType.APPLICATION_JSON);

        HttpEntity<Map<String, Object>> entity = new HttpEntity<>(request, headers);

        ResponseEntity<Map> response = restTemplate.exchange(
            baseUrl + "/pa/verify",
            HttpMethod.POST,
            entity,
            Map.class
        );

        return response.getBody();
    }

    public Map<String, Object> paLookup(String subjectDn) {
        Map<String, Object> request = new HashMap<>();
        request.put("subjectDn", subjectDn);

        HttpHeaders headers = new HttpHeaders();
        headers.setContentType(MediaType.APPLICATION_JSON);

        HttpEntity<Map<String, Object>> entity = new HttpEntity<>(request, headers);

        ResponseEntity<Map> response = restTemplate.exchange(
            baseUrl + "/certificates/pa-lookup",
            HttpMethod.POST,
            entity,
            Map.class
        );

        return response.getBody();
    }
}
```

### C# (.NET)

```csharp
using System.Net.Http.Json;

public class PAServiceClient
{
    private readonly HttpClient _client;
    private readonly string _baseUrl;

    public PAServiceClient(string baseUrl = "http://localhost:8080/api")
    {
        _client = new HttpClient();
        _baseUrl = baseUrl;
    }

    public async Task<JsonElement> VerifyAsync(
        byte[] sod,
        Dictionary<int, byte[]> dataGroups)
    {
        var request = new
        {
            sod = Convert.ToBase64String(sod),
            dataGroups = dataGroups.ToDictionary(
                kv => kv.Key.ToString(),
                kv => Convert.ToBase64String(kv.Value)
            )
        };

        var response = await _client.PostAsJsonAsync(
            $"{_baseUrl}/pa/verify",
            request
        );

        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<JsonElement>();
    }

    public async Task<JsonElement> PaLookupAsync(
        string subjectDn = null,
        string fingerprint = null)
    {
        var request = new Dictionary<string, string>();
        if (!string.IsNullOrEmpty(subjectDn))
            request["subjectDn"] = subjectDn;
        else if (!string.IsNullOrEmpty(fingerprint))
            request["fingerprint"] = fingerprint;

        var response = await _client.PostAsJsonAsync(
            $"{_baseUrl}/certificates/pa-lookup",
            request
        );

        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<JsonElement>();
    }
}
```

### curl

> **접속 환경별 HOST 치환**: `localhost:8080` 대신 환경에 맞는 주소 사용
> - WiFi (SC-WiFi): `192.168.1.70:8080`
> - 유선 LAN: `192.168.100.10:8080`
> - 도메인: `pkd.smartcoreinc.com`

```bash
# Full PA Verify (SOD + DG files required)
curl -X POST http://localhost:8080/api/pa/verify \
  -H "Content-Type: application/json" \
  -d '{
    "sod": "'$(base64 -w0 sod.bin)'",
    "dataGroups": {
      "1": "'$(base64 -w0 dg1.bin)'",
      "2": "'$(base64 -w0 dg2.bin)'"
    }
  }' | jq .

# Lightweight PA Lookup (by Subject DN - no file upload needed)
curl -X POST http://localhost:8080/api/certificates/pa-lookup \
  -H "Content-Type: application/json" \
  -d '{"subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234"}' | jq .

# Lightweight PA Lookup (by Fingerprint)
curl -X POST http://localhost:8080/api/certificates/pa-lookup \
  -H "Content-Type: application/json" \
  -d '{"fingerprint": "a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd"}' | jq .

# Parse SOD only
curl -X POST http://localhost:8080/api/pa/parse-sod \
  -H "Content-Type: application/json" \
  -d '{"sod": "'$(base64 -w0 sod.bin)'"}' | jq .

# Get verification history
curl http://localhost:8080/api/pa/history?page=0&size=10 | jq .

# Get statistics
curl http://localhost:8080/api/pa/statistics | jq .

# Health check
curl http://localhost:8080/api/health | jq .

# --- AI Certificate Analysis (v2.1.7+) ---

# Get AI analysis for a specific certificate
curl http://localhost:8080/api/ai/certificate/a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd | jq .

# Get analysis statistics
curl http://localhost:8080/api/ai/statistics | jq .

# List anomalous certificates (filtered)
curl "http://localhost:8080/api/ai/anomalies?label=ANOMALOUS&page=1&size=10" | jq .

# Trigger full analysis (background)
curl -X POST http://localhost:8080/api/ai/analyze | jq .

# Check analysis progress
curl http://localhost:8080/api/ai/analyze/status | jq .

# Country PKI maturity report
curl http://localhost:8080/api/ai/reports/country-maturity | jq .

# Risk distribution report
curl http://localhost:8080/api/ai/reports/risk-distribution | jq .

# Country detail report
curl http://localhost:8080/api/ai/reports/country/KR | jq .

# AI service health check
curl http://localhost:8080/api/ai/health | jq .
```

---

## Data Group Reference

| DG | Name | Content | Required for PA |
|----|------|---------|-----------------|
| DG1 | MRZ | Machine Readable Zone | **권장** |
| DG2 | Face | Facial biometric image (JPEG/JPEG2000) | **권장** |
| DG3 | Finger | Fingerprint biometrics | 선택 |
| DG4 | Iris | Iris biometrics | 선택 |
| DG5-10 | Optional | Additional data | 선택 |
| DG11 | Personal Details | Additional personal info | 선택 |
| DG12 | Document Details | Additional document info | 선택 |
| DG13 | Optional Details | Reserved | 선택 |
| DG14 | Security Options | Active Auth / PACE info | 선택 |
| DG15 | AA Public Key | Active Authentication | 선택 |
| DG16 | Persons to Notify | Emergency contacts | 선택 |

> PA 검증에 필요한 최소 데이터는 **SOD + 1개 이상의 DG**입니다. DG1(MRZ)과 DG2(얼굴 이미지)를 포함하면 여권 정보와 얼굴 이미지를 함께 확인할 수 있습니다.

---

## Error Codes

| Code | Severity | Description |
|------|----------|-------------|
| INVALID_REQUEST | CRITICAL | 잘못된 요청 형식 |
| MISSING_SOD | CRITICAL | SOD 데이터 누락 |
| INVALID_SOD | CRITICAL | SOD 파싱 실패 (CMS 구조 오류) |
| DSC_EXTRACTION_FAILED | CRITICAL | SOD에서 DSC 인증서 추출 실패 |
| CSCA_NOT_FOUND | CRITICAL | LDAP에서 CSCA 인증서를 찾을 수 없음 |
| TRUST_CHAIN_INVALID | HIGH | DSC → CSCA 신뢰 체인 검증 실패 |
| SOD_SIGNATURE_INVALID | HIGH | SOD 서명 검증 실패 |
| DG_HASH_MISMATCH | HIGH | Data Group 해시 불일치 |
| CERTIFICATE_EXPIRED | MEDIUM | 인증서 유효기간 만료 (현재 시점) |
| CRL_EXPIRED | MEDIUM | CRL 유효기간 만료 (nextUpdate 경과) |
| CERTIFICATE_REVOKED | HIGH | 인증서 CRL에 의해 폐지됨 |

---

## OpenAPI Specification

전체 OpenAPI 3.0.3 스펙은 다음에서 확인할 수 있습니다:
- **Swagger UI (PA Service)**: `http://<server-host>:8080/api-docs/?urls.primaryName=PA+Service+API+v2.1.7`
- **Swagger UI (PKD Management)**: `http://<server-host>:8080/api-docs/?urls.primaryName=PKD+Management+API+v2.15.1`
- **OpenAPI YAML (PA)**: `http://<server-host>:8080/api/docs/pa-service.yaml`
- **OpenAPI YAML (PKD Mgmt)**: `http://<server-host>:8080/api/docs/pkd-management.yaml`

---

## Troubleshooting

### 일반적인 오류

**1. CSCA_NOT_FOUND**
- 원인: 해당 국가의 CSCA 인증서가 Local PKD에 등록되지 않음
- 해결: PKD Management 서비스에서 해당 국가의 Master List 또는 LDIF 업로드

**2. TRUST_CHAIN_INVALID**
- 원인: DSC → CSCA 신뢰 체인 검증 실패
- 해결: CSCA 인증서 유효성 확인, Link Certificate 여부 확인, CRL 업데이트

**3. DG_HASH_MISMATCH**
- 원인: Data Group 데이터가 SOD의 해시값과 불일치
- 해결: 여권 판독 시 데이터 무결성 확인 (NFC 통신 오류 가능성)

**4. Invalid Base64 encoding**
- 원인: 잘못된 Base64 인코딩
- 해결: Standard Base64 인코딩 사용 (URL-safe Base64가 아닌 RFC 4648 표준)

**5. JPEG2000 이미지가 표시되지 않음**
- 원인: DG2 파싱 시 JPEG2000 → JPEG 변환 미지원 빌드
- 해결: pa-service가 OpenJPEG(`libopenjp2-dev`) + libjpeg(`libjpeg-dev`)와 함께 빌드되었는지 확인

---

## Changelog

### v2.1.7 (2026-02-21)

**AI 인증서 분석 엔진 연동 (AI Certificate Analysis)**:
- AI Analysis Service(Python FastAPI) 기반 ML 인증서 이상 탐지 및 패턴 분석 API 10개 엔드포인트 추가
- `GET /api/ai/certificate/{fingerprint}` — 개별 인증서 AI 분석 결과 (anomaly_score, risk_level, risk_factors, anomaly_explanations)
- `GET /api/ai/anomalies` — 이상 인증서 목록 (country/type/label/risk_level 필터, 페이지네이션)
- `GET /api/ai/statistics` — 전체 분석 통계 (31,212개 인증서: NORMAL 27,305 / SUSPICIOUS 3,905 / ANOMALOUS 2)
- `POST /api/ai/analyze` — 전체 인증서 일괄 분석 실행 (비동기 백그라운드)
- `GET /api/ai/analyze/status` — 분석 작업 진행 상태 (IDLE/RUNNING/COMPLETED/FAILED)
- 리포트 API 5개: country-maturity, algorithm-trends, key-size-distribution, risk-distribution, country detail
- Anomaly detection: Isolation Forest (global) + Local Outlier Factor (per country/type) 이중 모델
- Risk scoring: 6개 카테고리 복합 점수 (algorithm 0~40, key_size 0~40, compliance 0~20, validity 0~15, extensions 0~15, anomaly 0~15)
- Feature engineering: 25개 ML 피처 (암호학, 유효기간, 준수성, 확장, 국가 상대값)
- Explainability: 이상 인증서당 상위 5개 기여 피처 + sigma 편차 + 한국어 설명
- PA 검증 후 DSC fingerprint로 AI 분석 결합 활용 예시 추가
- Python/curl Integration Example에 AI 분석 API 호출 코드 추가
- 모든 AI 엔드포인트 Public (인증 불필요)

### v2.1.6 (2026-02-19)

**국가 코드 DG1 MRZ Fallback + requestedBy 필드**:
- PA Verify 시 국가 코드 추출 우선순위 변경: 요청 파라미터 `issuingCountry` → DG1 MRZ issuing country (line1[2:5]) → DSC issuer `C=` → `"XX"`
- DG1 MRZ에서 3자리 alpha-3 국가 코드 추출 후 `normalizeCountryCodeToAlpha2()`로 alpha-2 변환 (예: `CAN` → `CA`)
- DSC 인증서에 `C=` 필드가 없는 테스트 인증서에서도 MRZ 데이터로 정확한 국가 코드 표시
- `requestedBy` 필드 추가: 로그인 사용자명이 PA 검증 기록에 저장됨 (프론트엔드에서 localStorage user 또는 JWT 토큰에서 추출)
- PA History 응답에 `requestedBy` 필드 포함

### v2.1.5 (2026-02-19)

**PA 검증 DB 필드 확장**:
- `pkdConformanceText` 필드 DB 저장 (PostgreSQL + Oracle)
- `requestedBy`, `dscNonConformant`, `pkdConformanceCode`, `pkdConformanceText` 검증 시 DB 저장
- PA History 응답에 `requestedBy`, `dscNonConformant`, `pkdConformanceCode`, `pkdConformanceText` 필드 추가

### v2.1.4 (2026-02-14)

**DSC Non-Conformant(비준수) 상태 조회 지원**:
- PA Verify 응답의 `certificateChainValidation`에 DSC Non-Conformant 필드 추가
  - `dscNonConformant`: DSC가 ICAO PKD Non-Conformant인 경우 `true` (해당 시에만 포함)
  - `pkdConformanceCode`: 비준수 사유 코드 (예: `ERR:CSCA.CDP.14`)
  - `pkdConformanceText`: 비준수 사유 설명
- PA Lookup 응답에 DSC_NC conformance 데이터 추가 (`pkdConformanceCode`, `pkdConformanceText`, `pkdVersion`)
- PA Service: SOD에서 추출한 DSC의 fingerprint로 LDAP `dc=nc-data` 검색하여 Non-Conformant 여부 판별
- PA Service: `findDscBySubjectDn()` nc-data 폴백 검색 추가 (`dc=data` → `dc=nc-data`)
- PKD Management: PA Lookup에서 `certificateType="DSC_NC"`인 경우 LDAP nc-data에서 conformance 데이터 보조 조회
- Non-Conformant 상태는 정보성으로만 표시 (검증 결과 VALID/INVALID에 영향 없음)
- 자세한 내용: [DSC_NC_HANDLING.md](DSC_NC_HANDLING.md)

### v2.1.3 (2026-02-14)

**경량 PA 조회 API 추가 (Lightweight PA Lookup)**:
- `POST /api/certificates/pa-lookup` 엔드포인트 추가 (PKD Management 서비스)
- DSC Subject DN 또는 SHA-256 Fingerprint로 기존 Trust Chain 검증 결과 즉시 조회
- SOD/DG 파일 업로드 없이 DB에서 사전 계산된 검증 결과 반환 (5~20ms 응답)
- 대소문자 무시 Subject DN 비교 (`LOWER()`)
- `subjectDn`과 `fingerprint` 두 가지 조회 키 지원
- Public endpoint (JWT 인증 불필요)
- PostgreSQL + Oracle 모두 지원

### v2.1.2 (2026-02-13)

**CRL 유효기간 검증 추가**:
- CRL Check 단계에서 CRL `nextUpdate` 기준 만료 여부 확인
- CRL 만료 시 `crlStatus: "CRL_EXPIRED"` 반환 (폐지 목록 확인 불가)
- `crlThisUpdate`, `crlNextUpdate` 필드 추가 (CRL 발행일/다음 업데이트 예정일)
- CRL 미만료 시에만 인증서 폐지 여부 확인 수행

### v2.1.1 (2026-02-12)

**DSC Auto-Registration from PA Verification**:
- PA 검증 시 SOD에서 추출한 DSC를 자동으로 Local PKD의 certificate 테이블에 등록
- `source_type='PA_EXTRACTED'` 로 등록 출처 추적
- SHA-256 fingerprint 기반 중복 검사 (이미 등록된 DSC 재등록 방지)
- X.509 메타데이터 전체 추출 (signature_algorithm, public_key_algorithm, public_key_size, is_self_signed 등)
- 응답에 `dscAutoRegistration` 필드 추가
- `stored_in_ldap=false`로 등록되며, PKD Relay reconciliation을 통해 LDAP에 동기화

**DG2 JPEG2000 → JPEG 자동 변환**:
- 브라우저에서 JPEG2000을 렌더링할 수 없으므로 서버에서 자동 변환
- OpenJPEG + libjpeg 사용 (빌드 시 `HAS_OPENJPEG` 매크로로 조건부 활성화)
- 원본 형식은 `originalFormat` 필드에 보존

**기타 개선**:
- 검증 결과에 `verificationTimestamp`, `processingDurationMs` 필드 추가
- SOD 바이너리 저장 및 SHA-256 해시 계산
- Oracle 호환성: `LIMIT` → `FETCH FIRST`, `NOW()` → `SYSTIMESTAMP`

### v1.2.0 (2026-01-06)

**Certificate Expiration Handling**:
- `certificateChainValidation` 응답에 인증서 만료 필드 추가
- ICAO 9303 Point-in-Time Validation 지원

### v1.1.0 (2026-01-03)

- API Gateway (포트 8080) 통합
- 모든 API 엔드포인트를 API Gateway를 통해 접근하도록 변경

### v1.0.0 (2025-12-30)

- 초기 릴리스
- PA 검증, SOD/DG1/DG2 파싱 API
- 검증 이력 및 통계 API

---

## Contact

기술 지원이 필요한 경우:
- Email: support@smartcoreinc.com
- GitHub: https://github.com/smartcoreinc/icao-local-pkd

---

**Copyright 2026 SmartCore Inc. All rights reserved.**

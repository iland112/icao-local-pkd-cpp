# PA Service API Guide for External Clients

**Version**: 2.1.2
**Last Updated**: 2026-02-13
**API Gateway Port**: 8080

---

## Overview

PA Service는 ICAO 9303 표준에 따른 Passive Authentication(수동 인증) 검증을 수행하는 REST API 서비스입니다. 전자여권 판독기가 연결된 외부 클라이언트 애플리케이션에서 이 API를 사용하여 여권의 진위를 검증할 수 있습니다.

### Base URL

**API Gateway (권장)**:
```
http://<server-host>:8080/api/pa
```

> **Note**: 모든 API 요청은 API Gateway(포트 8080)를 통해 라우팅됩니다. API Gateway는 로드 밸런싱, Rate Limiting, 통합 로깅 등의 기능을 제공합니다.

### 인증

PA Service의 모든 엔드포인트는 **인증 불필요**(Public)입니다. 전자여권 판독기 등 외부 클라이언트에서 별도 인증 없이 호출할 수 있습니다.

---

## API Endpoints Summary

| # | Method | Path | Description |
|---|--------|------|-------------|
| 1 | `POST` | `/api/pa/verify` | PA 검증 (8단계 프로세스) |
| 2 | `POST` | `/api/pa/parse-sod` | SOD 메타데이터 파싱 |
| 3 | `POST` | `/api/pa/parse-dg1` | DG1 → MRZ 파싱 |
| 4 | `POST` | `/api/pa/parse-dg2` | DG2 → 얼굴 이미지 추출 |
| 5 | `POST` | `/api/pa/parse-mrz-text` | MRZ 텍스트 파싱 |
| 6 | `GET` | `/api/pa/history` | 검증 이력 조회 |
| 7 | `GET` | `/api/pa/{id}` | 검증 상세 조회 |
| 8 | `GET` | `/api/pa/{id}/datagroups` | Data Groups 상세 조회 |
| 9 | `GET` | `/api/pa/statistics` | 검증 통계 |
| 10 | `GET` | `/api/health` | 서비스 헬스 체크 |
| 11 | `GET` | `/api/health/database` | DB 연결 상태 |
| 12 | `GET` | `/api/health/ldap` | LDAP 연결 상태 |

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
  "documentNumber": "M12345678"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| sod | string | **필수** | Base64 인코딩된 SOD (Security Object Document) |
| dataGroups | object | **필수** | DG 번호를 키로, Base64 인코딩된 데이터를 값으로 하는 객체 |
| issuingCountry | string | 선택 | 국가 코드 (SOD DSC에서 자동 추출 가능) |
| documentNumber | string | 선택 | 여권 번호 (DG1 MRZ에서 자동 추출 가능) |

> **dataGroups 형식**: 키는 `"1"`, `"2"`, `"14"` (숫자 문자열) 또는 `"DG1"`, `"DG2"`, `"DG14"` 형식 모두 지원됩니다. 배열 형식 `[{"number":"DG1","data":"..."}]`도 지원됩니다.

### Response (Success - VALID)

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
      "expirationMessage": ""
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
| countryCode | string | DSC에서 추출한 국가 코드 |
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

---

## 2. SOD 파싱

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

## 3. DG1 파싱 (MRZ)

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

## 4. DG2 파싱 (얼굴 이미지)

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

## 5. MRZ 텍스트 파싱

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

## 6. 검증 이력 조회

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

## 7. 검증 상세 조회

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

## 8. Data Groups 상세 조회

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

## 9. 검증 통계

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

## 10. 헬스 체크

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


# Usage example
if __name__ == "__main__":
    client = PAServiceClient()

    # Read passport data from reader
    sod = read_sod_from_passport()
    dg1 = read_dg1_from_passport()
    dg2 = read_dg2_from_passport()

    # Verify
    result = client.verify(sod, {1: dg1, 2: dg2})

    if result["success"] and result["data"]["status"] == "VALID":
        print("Passport verification successful!")
        data = result["data"]
        print(f"Country: {data['issuingCountry']}")
        print(f"Document: {data['documentNumber']}")

        # Check DSC auto-registration
        if "dscAutoRegistration" in data:
            reg = data["dscAutoRegistration"]
            if reg["newlyRegistered"]:
                print(f"New DSC registered: {reg['fingerprint'][:16]}...")
    else:
        print("Verification failed!")
        print(f"Error: {result.get('error', 'INVALID status')}")
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
}
```

### curl

```bash
# PA Verify
curl -X POST http://localhost:8080/api/pa/verify \
  -H "Content-Type: application/json" \
  -d '{
    "sod": "'$(base64 -w0 sod.bin)'",
    "dataGroups": {
      "1": "'$(base64 -w0 dg1.bin)'",
      "2": "'$(base64 -w0 dg2.bin)'"
    }
  }' | jq .

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
- **Swagger UI**: `http://<server-host>:8080/api-docs/?urls.primaryName=PA+Service+API+v2.1.2`
- **OpenAPI YAML**: `http://<server-host>:8080/api/docs/pa-service.yaml`

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

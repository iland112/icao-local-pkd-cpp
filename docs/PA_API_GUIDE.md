# PA Service API Guide for External Clients

**Version**: 1.1.0
**Last Updated**: 2026-01-03
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

현재 버전에서는 별도의 인증이 필요하지 않습니다. 향후 버전에서 API Key 또는 OAuth 인증이 추가될 수 있습니다.

---

## API Endpoints

### 1. PA 검증 (Passive Authentication)

전자여권의 SOD와 Data Groups를 검증합니다.

**Endpoint**: `POST /api/pa/verify`

#### Request

```json
{
  "sod": "<Base64 encoded SOD>",
  "dataGroups": {
    "1": "<Base64 encoded DG1>",
    "2": "<Base64 encoded DG2>",
    "14": "<Base64 encoded DG14 (optional)>"
  },
  "issuingCountry": "KOR",
  "documentNumber": "M12345678",
  "requestedBy": "external-client-id"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| sod | string | ✅ | Base64 인코딩된 SOD (Security Object Document) |
| dataGroups | object | ✅ | DG 번호를 키로, Base64 인코딩된 데이터를 값으로 하는 객체 |
| issuingCountry | string | ❌ | 3자리 국가 코드 (SOD에서 자동 추출) |
| documentNumber | string | ❌ | 여권 번호 (DG1에서 자동 추출) |
| requestedBy | string | ❌ | 요청자 식별자 (로깅 목적) |

#### Response (Success)

```json
{
  "status": "VALID",
  "verificationId": "550e8400-e29b-41d4-a716-446655440000",
  "processingDurationMs": 245,
  "issuingCountry": "KOR",
  "documentNumber": "M12345678",

  "certificateChainValidation": {
    "valid": true,
    "dscSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=Document Signer KR 01",
    "dscSerialNumber": "1A2B3C4D5E6F",
    "cscaSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=Country Signing CA KR",
    "cscaFingerprint": "SHA256:ABCD1234...",
    "validityPeriod": {
      "notBefore": "2024-01-01T00:00:00Z",
      "notAfter": "2029-12-31T23:59:59Z"
    },
    "crlStatus": "NOT_REVOKED"
  },

  "sodSignatureValidation": {
    "valid": true,
    "hashAlgorithm": "SHA-256",
    "signatureAlgorithm": "SHA256withRSA"
  },

  "dataGroupValidation": {
    "valid": true,
    "totalGroups": 3,
    "validGroups": 3,
    "invalidGroups": 0,
    "details": {
      "DG1": { "valid": true, "expectedHash": "abc123...", "actualHash": "abc123..." },
      "DG2": { "valid": true, "expectedHash": "def456...", "actualHash": "def456..." },
      "DG14": { "valid": true, "expectedHash": "ghi789...", "actualHash": "ghi789..." }
    }
  },

  "mrzData": {
    "documentType": "P",
    "issuingCountry": "KOR",
    "surname": "KIM",
    "givenNames": "MINHO",
    "documentNumber": "M12345678",
    "nationality": "KOR",
    "dateOfBirth": "1990-05-15",
    "sex": "M",
    "dateOfExpiry": "2030-05-14"
  },

  "faceImage": {
    "format": "JPEG",
    "width": 480,
    "height": 640,
    "dataUrl": "data:image/jpeg;base64,/9j/4AAQ..."
  },

  "errors": []
}
```

#### Response (Failure)

```json
{
  "status": "INVALID",
  "verificationId": "550e8400-e29b-41d4-a716-446655440001",
  "processingDurationMs": 156,
  "issuingCountry": "KOR",

  "certificateChainValidation": {
    "valid": false,
    "message": "CSCA certificate not found for issuer"
  },

  "sodSignatureValidation": {
    "valid": true,
    "hashAlgorithm": "SHA-256",
    "signatureAlgorithm": "SHA256withRSA"
  },

  "dataGroupValidation": {
    "valid": true,
    "totalGroups": 2,
    "validGroups": 2,
    "invalidGroups": 0
  },

  "errors": [
    {
      "code": "CSCA_NOT_FOUND",
      "message": "CSCA certificate not found for issuing country KOR",
      "severity": "CRITICAL"
    }
  ]
}
```

#### Error Codes

| Code | Severity | Description |
|------|----------|-------------|
| INVALID_REQUEST | CRITICAL | 잘못된 요청 형식 |
| MISSING_SOD | CRITICAL | SOD 데이터 누락 |
| INVALID_SOD | CRITICAL | SOD 파싱 실패 |
| DSC_EXTRACTION_FAILED | CRITICAL | DSC 인증서 추출 실패 |
| CSCA_NOT_FOUND | CRITICAL | CSCA 인증서를 찾을 수 없음 |
| TRUST_CHAIN_INVALID | HIGH | 인증서 신뢰 체인 검증 실패 |
| SOD_SIGNATURE_INVALID | HIGH | SOD 서명 검증 실패 |
| DG_HASH_MISMATCH | HIGH | Data Group 해시 불일치 |
| CERTIFICATE_EXPIRED | MEDIUM | 인증서 유효기간 만료 |
| CERTIFICATE_REVOKED | HIGH | 인증서 폐지됨 |

---

### 2. SOD 파싱

SOD 메타데이터를 추출합니다.

**Endpoint**: `POST /api/pa/parse-sod`

#### Request

```json
{
  "sod": "<Base64 encoded SOD>"
}
```

#### Response

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

### 3. DG1 파싱 (MRZ)

DG1에서 MRZ 정보를 추출합니다.

**Endpoint**: `POST /api/pa/parse-dg1`

#### Request

```json
{
  "dg1": "<Base64 encoded DG1>"
}
```

또는

```json
{
  "dg1Base64": "<Base64 encoded DG1>"
}
```

#### Response

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

### 4. DG2 파싱 (얼굴 이미지)

DG2에서 얼굴 이미지를 추출합니다.

**Endpoint**: `POST /api/pa/parse-dg2`

#### Request

```json
{
  "dg2": "<Base64 encoded DG2>"
}
```

#### Response

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
      "imageSize": 12500,
      "imageOffset": 245,
      "width": 480,
      "height": 640,
      "imageDataUrl": "data:image/jpeg;base64,/9j/4AAQ..."
    }
  ]
}
```

---

### 5. MRZ 텍스트 파싱

OCR로 읽은 MRZ 텍스트를 파싱합니다.

**Endpoint**: `POST /api/pa/parse-mrz-text`

#### Request

```json
{
  "mrzText": "P<KORKIM<<MINHO<<<<<<<<<<<<<<<<<<<<<<<<<<<<M123456784KOR9005151M3005148<<<<<<<<<<<<<<02"
}
```

#### Response

MRZ 파싱 결과 (DG1 파싱과 동일한 형식)

---

### 6. 검증 이력 조회

**Endpoint**: `GET /api/pa/history`

#### Query Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| page | integer | 0 | 페이지 번호 (0부터 시작) |
| size | integer | 20 | 페이지 크기 |
| status | string | - | 상태 필터 (VALID, INVALID, ERROR) |
| issuingCountry | string | - | 국가 코드 필터 |

#### Response

```json
{
  "content": [
    {
      "verificationId": "550e8400-e29b-41d4-a716-446655440000",
      "issuingCountry": "KOR",
      "documentNumber": "M12345678",
      "status": "VALID",
      "verificationTimestamp": "2026-01-03T10:30:00",
      "processingDurationMs": 245,
      "certificateChainValidation": { "valid": true },
      "sodSignatureValidation": { "valid": true },
      "dataGroupValidation": { "valid": true }
    }
  ],
  "page": 0,
  "size": 20,
  "totalElements": 150,
  "totalPages": 8,
  "first": true,
  "last": false
}
```

---

### 7. 검증 상세 조회

**Endpoint**: `GET /api/pa/{verificationId}`

#### Response

전체 검증 결과 (PA 검증 응답과 동일한 형식)

---

### 8. Data Groups 조회

**Endpoint**: `GET /api/pa/{verificationId}/datagroups`

#### Response

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

---

### 9. 통계 조회

**Endpoint**: `GET /api/pa/statistics`

#### Response

```json
{
  "totalVerifications": 1500,
  "validCount": 1350,
  "invalidCount": 120,
  "errorCount": 30,
  "averageProcessingTimeMs": 189,
  "countriesVerified": 45
}
```

---

### 10. 헬스 체크

**Endpoint**: `GET /api/health`

#### Response

```json
{
  "service": "pa-service",
  "status": "UP",
  "version": "2.0.0",
  "timestamp": "2026-01-03T10:30:00Z"
}
```

---

## Integration Examples

### Java (Spring RestTemplate)

```java
import org.springframework.web.client.RestTemplate;
import org.springframework.http.*;

public class PAServiceClient {
    private final RestTemplate restTemplate = new RestTemplate();
    private final String baseUrl = "http://localhost:8080/api";  // API Gateway

    public PAVerifyResponse verify(byte[] sod, Map<Integer, byte[]> dataGroups) {
        // Build request
        Map<String, Object> request = new HashMap<>();
        request.put("sod", Base64.getEncoder().encodeToString(sod));

        Map<String, String> dgMap = new HashMap<>();
        dataGroups.forEach((num, data) ->
            dgMap.put(String.valueOf(num), Base64.getEncoder().encodeToString(data))
        );
        request.put("dataGroups", dgMap);

        // Send request
        HttpHeaders headers = new HttpHeaders();
        headers.setContentType(MediaType.APPLICATION_JSON);

        HttpEntity<Map<String, Object>> entity = new HttpEntity<>(request, headers);

        ResponseEntity<PAVerifyResponse> response = restTemplate.exchange(
            baseUrl + "/pa/verify",
            HttpMethod.POST,
            entity,
            PAVerifyResponse.class
        );

        return response.getBody();
    }
}
```

### Python (requests)

```python
import requests
import base64

class PAServiceClient:
    def __init__(self, base_url="http://localhost:8080/api"):  # API Gateway
        self.base_url = base_url

    def verify(self, sod: bytes, data_groups: dict) -> dict:
        """
        Perform PA verification

        Args:
            sod: Raw SOD bytes
            data_groups: Dict mapping DG number to raw bytes
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
        """Parse SOD metadata"""
        response = requests.post(
            f"{self.base_url}/pa/parse-sod",
            json={"sod": base64.b64encode(sod).decode('utf-8')}
        )
        return response.json()

    def parse_dg1(self, dg1: bytes) -> dict:
        """Parse MRZ from DG1"""
        response = requests.post(
            f"{self.base_url}/pa/parse-dg1",
            json={"dg1": base64.b64encode(dg1).decode('utf-8')}
        )
        return response.json()

    def parse_dg2(self, dg2: bytes) -> dict:
        """Extract face image from DG2"""
        response = requests.post(
            f"{self.base_url}/pa/parse-dg2",
            json={"dg2": base64.b64encode(dg2).decode('utf-8')}
        )
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

    if result["status"] == "VALID":
        print("Passport verification successful!")
        print(f"Holder: {result['mrzData']['fullName']}")
    else:
        print("Verification failed!")
        for error in result["errors"]:
            print(f"  - {error['code']}: {error['message']}")
```

### C# (.NET)

```csharp
using System.Net.Http.Json;

public class PAServiceClient
{
    private readonly HttpClient _client;
    private readonly string _baseUrl;

    public PAServiceClient(string baseUrl = "http://localhost:8080/api")  // API Gateway
    {
        _client = new HttpClient();
        _baseUrl = baseUrl;
    }

    public async Task<PAVerifyResponse> VerifyAsync(
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
        return await response.Content.ReadFromJsonAsync<PAVerifyResponse>();
    }
}
```

---

## Data Group Reference

| DG | Name | Content | Required |
|----|------|---------|----------|
| DG1 | MRZ | Machine Readable Zone | ✅ |
| DG2 | Face | Facial biometric image | ✅ |
| DG3 | Finger | Fingerprint biometrics | ❌ |
| DG4 | Iris | Iris biometrics | ❌ |
| DG5-10 | Optional | Additional data | ❌ |
| DG11 | Personal Details | Additional personal info | ❌ |
| DG12 | Document Details | Additional document info | ❌ |
| DG13 | Optional Details | Reserved | ❌ |
| DG14 | Security Options | Active Auth public key | ❌ |
| DG15 | AA Public Key | Active Authentication | ❌ |
| DG16 | Persons to Notify | Emergency contacts | ❌ |

---

## OpenAPI Specification

전체 OpenAPI 3.0 스펙은 다음에서 확인할 수 있습니다:
- Swagger UI: `http://localhost:8080/api/pa/docs`
- OpenAPI YAML: `http://localhost:8080/api/pa/openapi.yaml`

> **Note**: API Gateway를 통해 접근합니다.

---

## Troubleshooting

### 일반적인 오류

**1. CSCA_NOT_FOUND**
- 원인: 해당 국가의 CSCA 인증서가 Local PKD에 등록되지 않음
- 해결: PKD Management 서비스에서 해당 국가의 Master List 업로드

**2. TRUST_CHAIN_INVALID**
- 원인: DSC → CSCA 신뢰 체인 검증 실패
- 해결: CSCA 인증서 유효성 확인, CRL 업데이트

**3. DG_HASH_MISMATCH**
- 원인: Data Group 데이터가 SOD의 해시값과 불일치
- 해결: 여권 판독 시 데이터 무결성 확인

**4. Invalid Base64 encoding**
- 원인: 잘못된 Base64 인코딩
- 해결: 표준 Base64 인코딩 확인 (URL-safe가 아닌 standard Base64)

---

## Contact

기술 지원이 필요한 경우:
- Email: support@smartcoreinc.com
- GitHub: https://github.com/smartcoreinc/icao-local-pkd

---

**Copyright 2026 SmartCore Inc. All rights reserved.**

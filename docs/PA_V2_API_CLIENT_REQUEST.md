# PA V2 API 클라이언트 요청 상세 문서

> 서버측 검토를 위한 클라이언트 요청 정보

**작성일**: 2026-01-05
**작성자**: FPHPS_WEB_Example 개발팀
**버전**: 1.0

---

## 1. 엔드포인트 정보

| 항목 | 값 |
|------|-----|
| **URL** | `http://192.168.100.11:8080/api/pa/verify` |
| **Method** | `POST` |
| **Content-Type** | `application/json` |

---

## 2. 요청 헤더

```http
POST /api/pa/verify HTTP/1.1
Host: 192.168.100.11:8080
Content-Type: application/json
Accept: application/json
```

---

## 3. 요청 JSON 구조

```json
{
  "sod": "<Base64 인코딩된 SOD 바이트 배열>",
  "dataGroups": {
    "1": "<Base64 인코딩된 DG1 바이트 배열>",
    "2": "<Base64 인코딩된 DG2 바이트 배열>",
    "14": "<Base64 인코딩된 DG14 바이트 배열 (있는 경우)>"
  },
  "issuingCountry": "KOR",
  "documentNumber": "M12345678",
  "requestedBy": "FPHPS_WEB_Example"
}
```

---

## 4. 필드 상세 설명

| 필드 | 타입 | 필수 | 설명 |
|------|------|:----:|------|
| `sod` | String | ✅ | SOD(Security Object Document) 바이트 배열을 Base64로 인코딩한 값 |
| `dataGroups` | Object | ✅ | 숫자 문자열 키("1", "2" 등)와 Base64 인코딩된 DG 값의 맵 |
| `issuingCountry` | String | ✅ | 3자리 ISO 국가 코드 (예: "KOR", "USA", "JPN") |
| `documentNumber` | String | ✅ | 여권 번호 (MRZ에서 추출) |
| `requestedBy` | String | ✅ | 클라이언트 식별자 (고정값: "FPHPS_WEB_Example") |

---

## 5. dataGroups 키 형식

> **중요**: V2 API는 숫자 문자열 키를 사용합니다.

| 클라이언트 전송 키 | 의미 |
|-------------------|------|
| `"1"` | DG1 (MRZ 정보) |
| `"2"` | DG2 (얼굴 이미지) |
| `"14"` | DG14 (보안 옵션) |

### V1과의 차이점

| 버전 | 키 형식 예시 |
|------|-------------|
| V1 | `"DG1"`, `"DG2"`, `"DG14"` |
| **V2** | `"1"`, `"2"`, `"14"` |

---

## 6. 실제 요청 예시

아래는 테스트 시 전송된 실제 요청 샘플입니다:

```json
{
  "sod": "MIIHxQYJKoZIhvcNAQcCoIIHtjCCB7ICAQMxDzAN...(약 3000자)...==",
  "dataGroups": {
    "1": "YWlqZXJvanNkZmpzZGxm...(Base64)...",
    "2": "YmFzZTY0ZW5jb2RlZGRn...(Base64)..."
  },
  "issuingCountry": "KOR",
  "documentNumber": "M87654321",
  "requestedBy": "FPHPS_WEB_Example"
}
```

---

## 7. 클라이언트 코드 (Java)

### 7.1 요청 DTO 클래스

```java
// PaVerificationRequestV2.java
public record PaVerificationRequestV2(
    String sod,
    Map<String, String> dataGroups,  // Keys: "1", "2", "14" (숫자 문자열)
    String issuingCountry,
    String documentNumber,
    String requestedBy
) {}
```

### 7.2 서비스 메서드

```java
// PassiveAuthenticationService.java - verifyV2() 메서드

public PaVerificationResponse verifyV2(DocumentReadResponse response) {
    // 1. SOD 추출 및 Base64 인코딩
    byte[] sodBytes = response.getSod();
    String sodBase64 = Base64.getEncoder().encodeToString(sodBytes);

    // 2. DataGroups 추출 및 Base64 인코딩 (숫자 문자열 키 사용)
    Map<String, String> dataGroups = new HashMap<>();
    if (response.getDg1() != null) {
        dataGroups.put("1", Base64.getEncoder().encodeToString(response.getDg1()));
    }
    if (response.getDg2() != null) {
        dataGroups.put("2", Base64.getEncoder().encodeToString(response.getDg2()));
    }
    if (response.getDg14() != null) {
        dataGroups.put("14", Base64.getEncoder().encodeToString(response.getDg14()));
    }

    // 3. 요청 객체 생성
    PaVerificationRequestV2 request = new PaVerificationRequestV2(
        sodBase64,
        dataGroups,
        extractIssuingCountry(response),  // MRZ에서 추출
        extractDocumentNumber(response),   // MRZ에서 추출
        "FPHPS_WEB_Example"
    );

    // 4. API 호출
    ResponseEntity<PaVerificationResponse> result = restTemplate.postForEntity(
        paApiBaseUrl + "/api/pa/verify",
        request,
        PaVerificationResponse.class
    );

    return result.getBody();
}
```

---

## 8. 현재 서버 응답

현재 서버에서 **500 Internal Server Error**를 반환하고 있습니다:

```
HTTP/1.1 500 Internal Server Error
Content-Length: 0

[응답 본문 없음]
```

응답 본문이 없어 서버측 에러 원인을 파악하기 어렵습니다.

---

## 9. 서버측 확인 요청 사항

서버측에서 다음 사항을 확인해 주시기 바랍니다:

### 9.1 엔드포인트 확인
- `/api/pa/verify` 경로가 올바른지 확인

### 9.2 요청 형식 확인
- `dataGroups`의 키가 `"1"`, `"2"` 형식이 맞는지 확인
- 요청 JSON 구조가 서버 DTO와 일치하는지 확인

### 9.3 데이터 처리 확인
- SOD 및 DataGroups의 Base64 디코딩이 정상 동작하는지 확인
- 디코딩된 바이트 배열의 유효성 검증

### 9.4 에러 로깅
- 500 에러 발생 시 서버측 스택 트레이스 확인
- 에러 시 상세 메시지를 응답 본문에 포함해 주시면 디버깅에 도움이 됩니다

### 9.5 기대 응답 형식

정상 응답 시 다음과 같은 JSON 형식을 기대합니다:

```json
{
  "status": "SUCCESS",
  "overallResult": "PASSED",
  "certificateChainValidation": { ... },
  "sodSignatureValidation": { ... },
  "dataGroupHashValidation": { ... },
  "processingTimeMs": 1234
}
```

---

## 10. 연락처

추가 정보가 필요하시면 연락 부탁드립니다.

- **프로젝트**: FPHPS_WEB_Example
- **클라이언트 ID**: FPHPS_WEB_Example

---

## 10. 테스트 결과 (2026-01-05 15:32)

### 10.1 요청 정보 (클라이언트 로그)

```
PA verification requested (V2 - API Gateway)
Extracted for V2: country=KOR, docNumber=M46139533, DGs=[1, 2, 3, 14]
Sending PA V2 verification request: SOD size=1857 bytes, DG count=4
SOD header (first 16 bytes): 77 82 07 3D 30 82 07 39 06 09 2A 86 48 86 F7 0D
  DG1 size: 93 bytes
  DG2 size: 11874 bytes
  DG14 size: 302 bytes
```

### 10.2 서버 응답

서버에서 HTTP 200 응답을 반환했으나, **모든 필드가 null**입니다:

```
PA V2 verification completed: status=null, id=null, duration=nullms
```

### 10.3 추가 확인 요청 사항

1. **응답 JSON 구조**: 서버가 반환하는 실제 JSON 구조 예시를 공유해 주세요
2. **필드명 매핑**: 응답 필드명이 다음과 일치하는지 확인:
   - `status` (문자열: "SUCCESS" 또는 "FAILURE")
   - `verificationId` (문자열)
   - `processingDurationMs` (숫자)
   - `certificateChainValidation` (객체)
   - `sodSignatureValidation` (객체)
   - `dataGroupValidation` (객체)
3. **성공 응답 예시**: 검증 성공 시 응답 JSON 샘플
4. **실패 응답 예시**: 검증 실패 시 응답 JSON 샘플

---

## 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 1.0 | 2026-01-05 | 최초 작성 |
| 1.1 | 2026-01-05 | 테스트 결과 및 추가 확인 요청 사항 추가 |

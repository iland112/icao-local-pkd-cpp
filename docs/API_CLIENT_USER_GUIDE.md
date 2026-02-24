# API Client 사용자 가이드 (외부 연동)

**Version**: 1.0.0
**Last Updated**: 2026-02-24
**대상**: 외부 클라이언트 애플리케이션 개발자
**API Version**: v2.21.0

---

## 개요

이 문서는 ICAO Local PKD 시스템에 **API Key로 연동하는 외부 클라이언트 애플리케이션** 개발자를 위한 가이드입니다.

API Key를 발급받으면 별도의 로그인 없이 `X-API-Key` HTTP 헤더 하나로 인증서 검색, PA 검증, AI 분석 등의 API를 사용할 수 있습니다.

### API Key 인증 흐름

```
외부 Agent                           ICAO Local PKD
   |                                      |
   |  HTTP Request                        |
   |  X-API-Key: icao_XXXXXXXX_...  ---->  |
   |                                      | → SHA-256 해시 비교
   |                                      | → Permission 확인
   |                                      | → IP 화이트리스트 확인
   |                                      | → Rate Limit 확인
   |  <----  200 OK (JSON Response)       |
   |                                      |
```

### Base URL

**HTTPS (운영 환경, 권장)**:
```
# Private CA 인증서(ca.crt) 설치 필요
https://pkd.smartcoreinc.com/api
```

**HTTP (내부 네트워크)**:
```
http://pkd.smartcoreinc.com/api
```

**WiFi 네트워크 (SC-WiFi)**:
```
http://192.168.1.70:8080/api
```

**유선 LAN (내부 네트워크)**:
```
http://192.168.100.10:8080/api
```

> **Note**: HTTPS 사용 시 Private CA 인증서(`ca.crt`)를 클라이언트에 설치해야 합니다. 관리자에게 인증서 파일을 요청하세요.

---

## 인증 방법

모든 API 요청에 `X-API-Key` 헤더를 포함하세요:

```
X-API-Key: icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
```

### API Key 형식

```
icao_{prefix}_{random}
 │      │        │
 │      │        └── 32자 랜덤 문자열 (Base62)
 │      └── 8자 키 접두사 (관리 UI에서 식별용)
 └── 고정 접두사
```

- 총 길이: 46자
- 대소문자 구분됨 (Case-Sensitive)
- API Key는 발급 시 **한 번만 표시**되며, 시스템에는 SHA-256 해시만 저장됩니다

### API Key 발급

API Key는 시스템 관리자가 발급합니다. 관리자에게 다음 정보를 전달하세요:
- **클라이언트 이름**: 시스템/서비스 이름 (예: "출입국관리 Agent")
- **필요한 기능**: PA 검증, 인증서 검색 등
- **접속 IP**: 클라이언트 서버의 IP 주소 또는 대역
- **예상 사용량**: 분당/시간당/일일 예상 요청 수

---

## 사용 가능 엔드포인트

API Key로 접근할 수 있는 주요 엔드포인트입니다. 관리자가 설정한 Permission에 따라 접근 가능 범위가 다를 수 있습니다.

### PA 검증 (Permission: `pa:verify`)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/pa/verify` | PA 검증 (8단계 전체 프로세스) |
| `POST` | `/api/pa/parse-sod` | SOD 메타데이터 파싱 |
| `POST` | `/api/pa/parse-dg1` | DG1 → MRZ 파싱 |
| `POST` | `/api/pa/parse-dg2` | DG2 → 얼굴 이미지 추출 |
| `POST` | `/api/pa/parse-mrz-text` | MRZ 텍스트 파싱 |

### 인증서 검색 (Permission: `cert:read`)

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/certificates/search` | 인증서 검색 (국가/유형/상태 필터) |
| `GET` | `/api/certificates/detail` | 인증서 상세 조회 |
| `GET` | `/api/certificates/validation` | 검증 결과 조회 (fingerprint) |
| `POST` | `/api/certificates/pa-lookup` | 간편 PA 조회 (Subject DN/Fingerprint) |
| `GET` | `/api/certificates/countries` | 국가 목록 조회 |

### AI 분석 (Permission: `ai:read`)

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/ai/certificate/{fingerprint}` | 인증서 AI 분석 결과 |
| `GET` | `/api/ai/certificate/{fingerprint}/forensic` | 포렌식 상세 (10개 카테고리) |
| `GET` | `/api/ai/anomalies` | 이상 인증서 목록 |
| `GET` | `/api/ai/statistics` | AI 분석 전체 통계 |

### 보고서 (Permission: `report:read`)

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/certificates/dsc-nc/report` | 표준 부적합 DSC 보고서 |
| `GET` | `/api/certificates/crl/report` | CRL 보고서 |
| `GET` | `/api/certificates/crl/{id}` | CRL 상세 |
| `GET` | `/api/certificates/doc9303-checklist` | Doc 9303 적합성 체크리스트 |

---

## 연동 예제

### curl

```bash
# API Key 변수 설정
API_KEY="icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6"
BASE_URL="https://pkd.smartcoreinc.com"

# 1. 인증서 검색 (한국, DSC)
curl -s "$BASE_URL/api/certificates/search?country=KR&certType=DSC&limit=10" \
  -H "X-API-Key: $API_KEY" | jq .

# 2. PA 간편 조회 (Subject DN)
curl -s -X POST "$BASE_URL/api/certificates/pa-lookup" \
  -H "X-API-Key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d '{"subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234"}' | jq .

# 3. PA 전체 검증 (SOD + DG)
curl -s -X POST "$BASE_URL/api/pa/verify" \
  -H "X-API-Key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d '{
    "sod": "'$(base64 -w0 sod.bin)'",
    "dataGroups": {
      "1": "'$(base64 -w0 dg1.bin)'",
      "2": "'$(base64 -w0 dg2.bin)'"
    }
  }' | jq .

# 4. AI 분석 결과 조회
curl -s "$BASE_URL/api/ai/certificate/920693cd1283824ffdf48a3579fc35528122f3de46bab2ecdaef402db6d92e4e" \
  -H "X-API-Key: $API_KEY" | jq .

# 5. 헬스 체크 (인증 불필요)
curl -s "$BASE_URL/api/health" | jq .
```

---

### Python (requests)

```python
"""
ICAO Local PKD — 외부 Agent 연동 예제 (Python)
pip install requests
"""
import requests
import base64
import json

# 설정
BASE_URL = "https://pkd.smartcoreinc.com"
API_KEY = "icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6"

# HTTPS Private CA 인증서 (ca.crt 경로 지정)
CA_CERT = "/path/to/ca.crt"  # 또는 verify=False (개발 환경)

headers = {
    "X-API-Key": API_KEY,
    "Content-Type": "application/json"
}


def search_certificates(country: str, cert_type: str = "DSC", limit: int = 10):
    """인증서 검색"""
    resp = requests.get(
        f"{BASE_URL}/api/certificates/search",
        params={"country": country, "certType": cert_type, "limit": limit},
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    print(f"[인증서 검색] {country} {cert_type}: {data.get('total', 0)}건")
    return data


def pa_lookup(subject_dn: str):
    """PA 간편 조회 (Subject DN)"""
    resp = requests.post(
        f"{BASE_URL}/api/certificates/pa-lookup",
        json={"subjectDn": subject_dn},
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    validation = data.get("validation")
    if validation:
        print(f"[PA 조회] Status: {validation['validationStatus']}, "
              f"Trust Chain: {validation['trustChainValid']}")
    else:
        print("[PA 조회] 검증 결과 없음")
    return data


def pa_verify(sod_path: str, dg1_path: str, dg2_path: str = None):
    """PA 전체 검증 (SOD + Data Groups)"""
    with open(sod_path, "rb") as f:
        sod_b64 = base64.b64encode(f.read()).decode()
    with open(dg1_path, "rb") as f:
        dg1_b64 = base64.b64encode(f.read()).decode()

    payload = {
        "sod": sod_b64,
        "dataGroups": {"1": dg1_b64}
    }
    if dg2_path:
        with open(dg2_path, "rb") as f:
            payload["dataGroups"]["2"] = base64.b64encode(f.read()).decode()

    resp = requests.post(
        f"{BASE_URL}/api/pa/verify",
        json=payload,
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    result = data.get("result", {})
    print(f"[PA 검증] Overall: {result.get('overallResult')}, "
          f"Country: {result.get('issuingCountry')}")
    return data


def get_ai_analysis(fingerprint: str):
    """AI 인증서 분석 결과 조회"""
    resp = requests.get(
        f"{BASE_URL}/api/ai/certificate/{fingerprint}",
        headers=headers,
        verify=CA_CERT
    )
    resp.raise_for_status()
    data = resp.json()
    result = data.get("result", {})
    print(f"[AI 분석] Risk: {result.get('risk_level')}, "
          f"Score: {result.get('risk_score')}")
    return data


# 사용 예시
if __name__ == "__main__":
    # 1. 한국 DSC 인증서 검색
    certs = search_certificates("KR", "DSC", limit=5)

    # 2. PA 간편 조회
    pa_lookup("/C=KR/O=Government of Korea/CN=Document Signer 1234")

    # 3. AI 분석 결과
    get_ai_analysis("920693cd1283824ffdf48a3579fc35528122f3de46bab2ecdaef402db6d92e4e")
```

---

### Java (HttpURLConnection)

```java
/**
 * ICAO Local PKD — 외부 Agent 연동 예제 (Java)
 * JDK 11+
 */
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Base64;

public class IcaoPkdClient {

    private static final String BASE_URL = "https://pkd.smartcoreinc.com";
    private static final String API_KEY = "icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6";

    private final HttpClient client;

    public IcaoPkdClient() {
        // Private CA 사용 시 SSLContext 설정 필요
        this.client = HttpClient.newBuilder()
                .version(HttpClient.Version.HTTP_1_1)
                .build();
    }

    /**
     * 인증서 검색
     */
    public String searchCertificates(String country, String certType, int limit)
            throws Exception {
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(String.format(
                    "%s/api/certificates/search?country=%s&certType=%s&limit=%d",
                    BASE_URL, country, certType, limit)))
                .header("X-API-Key", API_KEY)
                .GET()
                .build();

        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        System.out.printf("[인증서 검색] Status: %d%n", response.statusCode());
        return response.body();
    }

    /**
     * PA 간편 조회
     */
    public String paLookup(String subjectDn) throws Exception {
        String body = String.format("{\"subjectDn\": \"%s\"}", subjectDn);

        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(BASE_URL + "/api/certificates/pa-lookup"))
                .header("X-API-Key", API_KEY)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body))
                .build();

        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        System.out.printf("[PA 조회] Status: %d%n", response.statusCode());
        return response.body();
    }

    /**
     * PA 전체 검증
     */
    public String paVerify(Path sodPath, Path dg1Path) throws Exception {
        String sodB64 = Base64.getEncoder().encodeToString(Files.readAllBytes(sodPath));
        String dg1B64 = Base64.getEncoder().encodeToString(Files.readAllBytes(dg1Path));

        String body = String.format(
            "{\"sod\": \"%s\", \"dataGroups\": {\"1\": \"%s\"}}",
            sodB64, dg1B64);

        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(BASE_URL + "/api/pa/verify"))
                .header("X-API-Key", API_KEY)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body))
                .build();

        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        System.out.printf("[PA 검증] Status: %d%n", response.statusCode());
        return response.body();
    }

    public static void main(String[] args) throws Exception {
        IcaoPkdClient client = new IcaoPkdClient();

        // 인증서 검색
        String result = client.searchCertificates("KR", "DSC", 5);
        System.out.println(result);

        // PA 간편 조회
        String lookup = client.paLookup(
            "/C=KR/O=Government of Korea/CN=Document Signer 1234");
        System.out.println(lookup);
    }
}
```

---

### C# (HttpClient)

```csharp
/**
 * ICAO Local PKD — 외부 Agent 연동 예제 (C#)
 * .NET 6+
 */
using System.Net.Http;
using System.Text;
using System.Text.Json;

public class IcaoPkdClient
{
    private const string BaseUrl = "https://pkd.smartcoreinc.com";
    private const string ApiKey = "icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6";

    private readonly HttpClient _client;

    public IcaoPkdClient()
    {
        // Private CA 사용 시 HttpClientHandler에서 ServerCertificateCustomValidationCallback 설정
        _client = new HttpClient();
        _client.DefaultRequestHeaders.Add("X-API-Key", ApiKey);
    }

    /// <summary>인증서 검색</summary>
    public async Task<string> SearchCertificatesAsync(
        string country, string certType = "DSC", int limit = 10)
    {
        var url = $"{BaseUrl}/api/certificates/search" +
                  $"?country={country}&certType={certType}&limit={limit}";

        var response = await _client.GetAsync(url);
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[인증서 검색] {country} {certType}: Status {response.StatusCode}");
        return result;
    }

    /// <summary>PA 간편 조회</summary>
    public async Task<string> PaLookupAsync(string subjectDn)
    {
        var payload = JsonSerializer.Serialize(new { subjectDn });
        var content = new StringContent(payload, Encoding.UTF8, "application/json");

        var response = await _client.PostAsync(
            $"{BaseUrl}/api/certificates/pa-lookup", content);
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[PA 조회] Status {response.StatusCode}");
        return result;
    }

    /// <summary>PA 전체 검증</summary>
    public async Task<string> PaVerifyAsync(string sodPath, string dg1Path)
    {
        var sodB64 = Convert.ToBase64String(await File.ReadAllBytesAsync(sodPath));
        var dg1B64 = Convert.ToBase64String(await File.ReadAllBytesAsync(dg1Path));

        var payload = JsonSerializer.Serialize(new
        {
            sod = sodB64,
            dataGroups = new Dictionary<string, string> { { "1", dg1B64 } }
        });

        var content = new StringContent(payload, Encoding.UTF8, "application/json");
        var response = await _client.PostAsync($"{BaseUrl}/api/pa/verify", content);
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[PA 검증] Status {response.StatusCode}");
        return result;
    }

    /// <summary>AI 분석 결과 조회</summary>
    public async Task<string> GetAiAnalysisAsync(string fingerprint)
    {
        var response = await _client.GetAsync(
            $"{BaseUrl}/api/ai/certificate/{fingerprint}");
        response.EnsureSuccessStatusCode();

        var result = await response.Content.ReadAsStringAsync();
        Console.WriteLine($"[AI 분석] Status {response.StatusCode}");
        return result;
    }
}

// 사용 예시
var client = new IcaoPkdClient();
var certs = await client.SearchCertificatesAsync("KR", "DSC", 5);
var lookup = await client.PaLookupAsync(
    "/C=KR/O=Government of Korea/CN=Document Signer 1234");
```

---

## Rate Limiting 응답 처리

API Key에 설정된 Rate Limit을 초과하면 `429 Too Many Requests` 응답이 반환됩니다.

### 응답 예시

```
HTTP/1.1 429 Too Many Requests
X-RateLimit-Limit: 60
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1708776045
Retry-After: 45
```

### Python에서의 처리

```python
import time

def call_api_with_retry(url, headers, max_retries=3):
    """Rate Limit 대응 재시도 로직"""
    for attempt in range(max_retries):
        resp = requests.get(url, headers=headers, verify=CA_CERT)

        if resp.status_code == 429:
            retry_after = int(resp.headers.get("Retry-After", 60))
            print(f"Rate limit 초과. {retry_after}초 후 재시도... (시도 {attempt + 1}/{max_retries})")
            time.sleep(retry_after)
            continue

        resp.raise_for_status()
        return resp.json()

    raise Exception("최대 재시도 횟수 초과")
```

### Java에서의 처리

```java
public String callWithRetry(HttpRequest request, int maxRetries) throws Exception {
    for (int i = 0; i < maxRetries; i++) {
        HttpResponse<String> response = client.send(request,
                HttpResponse.BodyHandlers.ofString());

        if (response.statusCode() == 429) {
            String retryAfter = response.headers()
                    .firstValue("Retry-After").orElse("60");
            System.out.printf("Rate limit 초과. %s초 후 재시도...%n", retryAfter);
            Thread.sleep(Long.parseLong(retryAfter) * 1000);
            continue;
        }

        return response.body();
    }
    throw new RuntimeException("최대 재시도 횟수 초과");
}
```

---

## 에러 코드 및 대응

| HTTP Status | 원인 | 대응 |
|-------------|------|------|
| `401 Unauthorized` | API Key가 없거나 잘못됨 | `X-API-Key` 헤더 확인, 키 값 검증 |
| `403 Forbidden` | 비활성 클라이언트, 만료, IP 차단, 권한 부족 | 관리자에게 클라이언트 상태 확인 요청 |
| `429 Too Many Requests` | Rate Limit 초과 | `Retry-After` 헤더 값만큼 대기 후 재시도 |
| `500 Internal Server Error` | 서버 오류 | 관리자에게 보고 |

### 에러 응답 형식

```json
{
  "success": false,
  "error": "에러 유형",
  "message": "상세 설명"
}
```

---

## FAQ / 트러블슈팅

### Q. API Key를 분실했습니다.

API Key는 발급 시 한 번만 표시되며, 시스템에 저장되지 않습니다. 관리자에게 **키 재발급**을 요청하세요. 기존 키는 즉시 무효화됩니다.

### Q. 403 Forbidden 오류가 발생합니다.

가능한 원인:
1. **클라이언트 비활성화**: 관리자가 클라이언트를 비활성화했을 수 있습니다
2. **키 만료**: `expires_at`이 설정된 경우 만료되었을 수 있습니다
3. **IP 제한**: `allowed_ips`에 현재 IP가 포함되지 않을 수 있습니다
4. **Permission 부족**: 요청한 엔드포인트에 대한 Permission이 없습니다

관리자에게 클라이언트 상태와 설정을 확인 요청하세요.

### Q. 429 Too Many Requests 오류가 자주 발생합니다.

현재 Rate Limit 설정을 확인하세요 (관리자에게 문의). 필요한 경우 관리자가 Rate Limit을 상향할 수 있습니다.

요청 빈도를 줄이는 방법:
- 인증서 검색 시 `limit` 파라미터를 늘려 페이지 수를 줄이기
- PA 간편 조회(`/api/certificates/pa-lookup`)를 사용하여 전체 검증 호출 줄이기
- 응답 결과를 로컬에 캐싱하기

### Q. HTTPS 연결 시 인증서 오류가 발생합니다.

ICAO Local PKD 시스템은 **Private CA** 기반 TLS를 사용합니다. 공인 CA가 아니므로 클라이언트에 CA 인증서를 설치해야 합니다.

```bash
# Python: verify 파라미터에 ca.crt 경로 지정
requests.get(url, headers=headers, verify="/path/to/ca.crt")

# curl: --cacert 옵션 사용
curl --cacert /path/to/ca.crt -H "X-API-Key: $API_KEY" $URL

# Java: KeyStore에 CA 인증서 등록
keytool -import -alias pkd-ca -file ca.crt -keystore cacerts
```

관리자에게 `ca.crt` 파일을 요청하세요.

### Q. 헬스 체크는 어떻게 하나요?

인증 없이 호출 가능한 헬스 체크 엔드포인트:

```bash
# 서비스 상태
curl https://pkd.smartcoreinc.com/api/health

# 데이터베이스 연결 상태
curl https://pkd.smartcoreinc.com/api/health/database

# LDAP 연결 상태
curl https://pkd.smartcoreinc.com/api/health/ldap
```

### Q. 어떤 엔드포인트가 인증 없이 접근 가능한가요?

대부분의 조회 엔드포인트는 Public입니다 (인증 없이 사용 가능). API Key는 사용량 추적과 Rate Limit 개별 관리를 위해 권장됩니다.

인증이 **필수**인 엔드포인트:
- 파일 업로드 (`/api/upload/ldif`, `/api/upload/masterlist`, `/api/upload/certificate`)
- 사용자 관리 (`/api/auth/users`)
- 감사 로그 (`/api/auth/audit-log`, `/api/audit/operations`)
- 코드 관리 수정 (`POST/PUT/DELETE /api/code-master`)

---

## OpenAPI 스펙

자세한 API 명세는 OpenAPI (Swagger) 문서를 참고하세요:

- **Swagger UI**: `https://pkd.smartcoreinc.com/api-docs`
- **OpenAPI YAML**: `docs/openapi/pkd-management.yaml`
- **PA Service YAML**: `docs/openapi/pa-service.yaml`

---

## Changelog

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 1.0.0 | 2026-02-24 | 초기 작성 (v2.21.0 API Client 인증 기능) |

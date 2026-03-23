# Production ICAO PKD LDAP 배포 이슈 (2026-03-23)

## 개요

Production 서버(10.0.0.220)에 ICAO PKD LDAP 시뮬레이션 서버를 배포하면서 발생한 5건의 이슈와 해결 과정을 기록합니다.

---

## 이슈 1: pkd-management 크래시 — `std::future_error`

### 증상
- Production 서버 로그인 불가
- `podman ps`: pkd-management **running** 상태이나 **unhealthy**
- `curl http://localhost:8081/api/health` → 응답 없음 (000)

### 원인 분석
```
[2026-03-23 09:00:15.482] [error] [1] [HttpClient] Request timed out after 10 seconds
[2026-03-23 09:00:16.420] [info] [1] [HttpClient] Successfully fetched HTML (16530 bytes)
[2026-03-23 09:00:16.550] [error] [1] Application error: std::future_error: No associated state
```

**Root Cause**: `HttpClient::fetchHtml()`에서 `std::promise`를 스택 변수로 생성하고 콜백 람다에서 참조(`&promise`)로 캡처. HTTP 요청이 10초 타임아웃 → 함수 반환 → `promise` 파괴 → 늦게 도착한 응답 콜백이 파괴된 `promise`에 `set_value()` 호출 → **use-after-free crash**.

### 해결
**임시 조치**: `scripts/podman/restart.sh pkd-management` + `api-gateway` 재시작

**근본 수정** (`services/pkd-management/src/infrastructure/http/http_client.cpp`):
```cpp
// Before (BUG): 스택 변수 참조 캡처 → use-after-free
std::promise<std::optional<std::string>> promise;
auto future = promise.get_future();
client->sendRequest(req, [&promise](...) { promise.set_value(html); });

// After (FIX): shared_ptr 값 캡처 → promise 생명주기 보장
auto promise = std::make_shared<std::promise<std::optional<std::string>>>();
auto future = promise->get_future();
client->sendRequest(req, [promise](...) { promise->set_value(html); });
```

### 재발 방지
- 비동기 콜백에서 스택 변수를 참조 캡처하지 않을 것
- `std::promise`/`std::future` 패턴 사용 시 반드시 `shared_ptr`로 힙 할당
- Drogon 이벤트 루프 콜백은 다른 스레드에서 실행될 수 있음에 주의

---

## 이슈 2: Podman short-name 이미지 빌드 실패

### 증상
```
Error: creating build container: short-name resolution enforced but cannot prompt without a TTY
```

### 원인
RHEL 9 Podman은 보안 정책상 short-name 이미지(`osixia/openldap:1.5.0`)를 허용하지 않음. Docker Hub의 full registry path가 필요.

### 해결
`icao-pkd-ldap/Dockerfile`:
```dockerfile
# Before
FROM osixia/openldap:1.5.0
# After
FROM docker.io/osixia/openldap:1.5.0
```

### 재발 방지
- Podman 환경의 Dockerfile은 항상 `docker.io/` prefix 사용
- 기존 Dockerfile 중 short-name 사용하는 것이 있으면 선제 수정

---

## 이슈 3: LDIF populate 스크립트 0건 전송

### 증상
`populate-from-local.sh --type dsc --max 500` 실행 시 `Found 500 entries` 표시되지만 실제 전송 0건.

### 원인
LDIF line folding 문제:
```
# ldapsearch 출력 (긴 DN이 줄바꿈됨)
dn: cn=00e584...,o=dsc,
 c=KR,dc=data,...
```
- `grep -oP 'cn=\K[^,]+'`가 첫 줄만 매칭 → 불완전한 DN 추출
- DN 치환 결과: `dc=intc=KR` (줄바꿈이 제거되지 않아 이어붙여짐)
- `ldapadd`가 잘못된 DN으로 실패하지만 에러가 suppress됨
- 파이프 `| while` 서브쉘에서 카운터 변수가 부모 쉘에 반영되지 않음

### 해결
`sed` 기반 LDIF unfold + DN 문자열 치환으로 수동 처리:
```bash
# LDIF line folding 해제 (이어진 줄 합치기)
ldapsearch ... | sed -n '/:/{N;s/\n //;P;D}; p' | \
# DN 치환 (로컬 → ICAO)
sed 's|dc=ldap,dc=smartcoreinc,dc=com|dc=icao,dc=int|g' > /tmp/batch.ldif

# 일괄 추가
ldapadd -c -f /tmp/batch.ldif
```

### 재발 방지
- LDIF 처리 시 반드시 line unfolding 선행 (`sed -n '/:/{N;s/\n //;P;D}; p'`)
- `| while` 대신 process substitution 또는 임시 파일 사용
- populate 스크립트 향후 리팩토링 필요

---

## 이슈 4: ICAO LDAP TLS 연결 실패 — 전역 TLS 컨텍스트 충돌

### 증상
```
[IcaoLdapClient] Simple Bind over TLS failed to ldaps://icao-pkd-ldap:636: Can't contact LDAP server
```
- `ldapsearch -H ldaps://...` (CLI) → 성공
- C 코드 `ldap_sasl_bind_s()` → 실패

### 원인 분석

OpenLDAP C 라이브러리의 TLS 옵션은 **전역(프로세스 단위)** 으로 관리됨. pkd-relay 프로세스에서:

1. 서비스 시작 시 **로컬 LDAP 풀** 초기화 (평문 `ldap://openldap1:389`, TLS 없음)
2. ICAO 동기화 시 `ldap_set_option(nullptr, TLS_CERTFILE/KEYFILE/CACERTFILE)` 전역 설정
3. `LDAP_OPT_X_TLS_NEWCTX` 호출 → 전역 TLS 컨텍스트 재생성
4. 이때 로컬 LDAP 풀의 기존 연결에도 영향 → **충돌**

### 시도한 접근 (실패 순서)

| # | 접근 | 결과 | 이유 |
|---|------|------|------|
| 1 | `LDAP_OPT_X_TLS_ALLOW` | 실패 | CA 검증은 통과하지만 핸드셰이크 자체 실패 |
| 2 | `LDAP_OPT_X_TLS_NEVER` | 실패 | 전역 설정이 로컬 LDAP 풀과 충돌 |
| 3 | per-connection `ldap_set_option(ldap_, ...)` | 실패 | OpenLDAP은 TLS를 전역에서만 지원 |
| 4 | `LDAPTLS_REQCERT=never` 환경변수 | 실패 | C 코드의 `ldap_set_option(nullptr, ...)` 호출이 덮어씀 |
| 5 | `/etc/ldap/ldap.conf`에 `TLS_REQCERT never`만 | 실패 | C 코드가 여전히 전역 TLS 옵션 설정 |
| 6 | **C 코드 전역 TLS 옵션 완전 제거 + ldap.conf에 cert/key 설정** | **성공** | |

### 최종 해결

**Dockerfile** (`services/pkd-relay-service/Dockerfile`):
```dockerfile
# /etc/ldap/ldap.conf에 TLS 설정 (C 코드 대신 설정 파일로 관리)
RUN mkdir -p /etc/ldap && \
    printf "TLS_REQCERT never\nTLS_CACERT /app/icao-tls/ca.pem\nTLS_CERT /app/icao-tls/client.pem\nTLS_KEY /app/icao-tls/client-key.pem\n" \
    > /etc/ldap/ldap.conf
```

**C 코드** (`icao_ldap_client.cpp`):
```cpp
// Before: 전역 TLS 옵션 설정 (로컬 LDAP 풀과 충돌)
ldap_set_option(nullptr, LDAP_OPT_X_TLS_CACERTFILE, ...);
ldap_set_option(nullptr, LDAP_OPT_X_TLS_CERTFILE, ...);
ldap_set_option(nullptr, LDAP_OPT_X_TLS_KEYFILE, ...);
ldap_set_option(nullptr, LDAP_OPT_X_TLS_NEWCTX, ...);

// After: ldap.conf에 위임 (전역 충돌 없음)
// TLS options configured via /etc/ldap/ldap.conf
rc = ldap_initialize(&ldap_, uri.c_str());
```

### 핵심 교훈
- **OpenLDAP C 라이브러리의 TLS는 프로세스 전역**: `ldap_set_option(nullptr, TLS_*)` 호출은 모든 LDAP 연결에 영향
- **동일 프로세스에서 평문 LDAP + TLS LDAP 혼용 시**: C 코드에서 전역 TLS 옵션을 설정하면 안 됨
- **`/etc/ldap/ldap.conf` 설정 파일이 안전한 대안**: 라이브러리가 자체적으로 읽어 처리
- `LDAPTLS_REQCERT=never` 환경변수는 `ldapsearch` CLI에는 적용되지만, C 코드에서 `ldap_set_option(nullptr, TLS_REQUIRE_CERT)` 호출 시 덮어써짐

---

## 이슈 5: Oracle 테이블 누락 — `icao_ldap_sync_log`

### 증상
```
[IcaoLdapSync] getSyncHistory failed: OCI statement execution failed (code 942): ORA-00942: table or view does not exist
```

### 원인
v2.39.0에서 추가된 `icao_ldap_sync_log` 테이블이 Oracle에 미생성. Docker 초기화 스크립트(`17-icao-ldap-sync.sql`)는 있으나 기존 운영 DB에는 적용되지 않음.

### 해결
`sqlplus`로 직접 PL/SQL 실행:
```sql
DECLARE
    v_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_exists FROM user_tables WHERE table_name = 'ICAO_LDAP_SYNC_LOG';
    IF v_exists = 0 THEN
        EXECUTE IMMEDIATE 'CREATE TABLE icao_ldap_sync_log (...)';
        EXECUTE IMMEDIATE 'CREATE INDEX ...';
    END IF;
END;
/
```

### 재발 방지
- 새 DB 테이블 추가 시 **마이그레이션 스크립트** 별도 작성
- Production 배포 체크리스트에 "Oracle 스키마 마이그레이션 확인" 항목 추가
- `clean-and-init.sh`뿐 아니라 운영 DB 마이그레이션 경로도 검증

---

## 배포 체크리스트 (참고)

Production 서비스 배포 시 확인 사항:

- [ ] `.env` 환경변수 추가/변경 확인
- [ ] `docker-compose.podman.yaml` 서비스/환경변수 동기화
- [ ] Dockerfile `FROM` short-name → `docker.io/` prefix (Podman)
- [ ] Oracle 스키마 마이그레이션 (신규 테이블/컬럼)
- [ ] 서비스 리빌드: `podman compose build <service>`
- [ ] 서비스 재시작: `scripts/podman/restart.sh <service>`
- [ ] **api-gateway 반드시 재시작** (DNS 캐시 갱신)
- [ ] 로그인 테스트: `curl -X POST https://localhost/api/auth/login`
- [ ] 헬스체크: `podman ps` 전체 healthy 확인

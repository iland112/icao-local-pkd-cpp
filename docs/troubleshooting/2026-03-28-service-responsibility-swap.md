# v2.41.0 서비스 기능 재배치 (Service Responsibility Swap)

**Date**: 2026-03-27
**Version**: v2.41.0
**Branch**: `refactor/service-responsibility-swap`

---

## 1. 변경 사항 요약

### 변경 전 (v2.40.0)

| Service | 역할 |
|---------|------|
| PKD Management (:8081) | Upload (LDIF/ML/개별), Certificate Search, ICAO Sync, Auth |
| PKD Relay (:8083) | DB-LDAP Sync, Reconciliation |

### 변경 후 (v2.41.0)

| Service | 역할 |
|---------|------|
| PKD Management (:8081) | **로컬 PKD 운영/관리** — DB-LDAP Sync, Reconciliation, 개별 인증서 업로드, Search, Auth |
| PKD Relay (:8083) | **외부 ICAO PKD 연계** — LDIF/ML 파일 임포트, ICAO LDAP Sync, ICAO 버전 감지, Upload 통계 |

### 변경 이유

- **역할 분리 명확화**: "로컬 운영/관리"와 "외부 연계"를 서비스 경계로 분리
- **Sync는 로컬 운영**: DB-LDAP 동기화/보정은 로컬 데이터 일관성 유지 → management로 이동
- **Upload는 외부 연계**: LDIF/ML은 외부 ICAO PKD에서 받은 파일 처리 → relay로 이동
- Dead code 제거: -18,174줄

---

## 2. 주요 라우팅 변경

### nginx API Gateway

| Route | 변경 전 | 변경 후 |
|-------|--------|--------|
| `/api/upload/*` | PKD Management (:8081) | **PKD Relay (:8083)** |
| `/api/sync/*` | PKD Relay (:8083) | **PKD Management (:8081)** |
| `/api/icao/*` | PKD Management (:8081) | **PKD Relay (:8083)** |

3개 nginx 설정 파일 모두 업데이트:
- `nginx/api-gateway.conf`
- `nginx/api-gateway-ssl.conf`
- `nginx/api-gateway-luckfox.conf`

---

## 3. 발생한 이슈 및 해결

### 3.1 std::bad_alloc 크래시 (relay)

**증상**: LDIF 파일(30k+ 엔트리) 처리 시 relay 컨테이너가 크래시
**원인**: relay 메모리 제한이 512MB로 설정되어 있었음 (이전에는 sync만 처리하므로 충분했음)
**해결**: relay 메모리 제한을 512MB → 1GB → 2GB로 증가. docker-compose에서 `mem_limit: 2g` 설정

### 3.2 relay 누락 환경변수

**증상**: relay에서 upload 관련 기능 호출 시 환경변수 미설정 오류
**원인**: upload 관련 환경변수(`UPLOAD_*`, `MAX_BODY_SIZE_MB` 등)가 relay의 docker-compose environment에 없었음
**해결**: docker-compose.yaml에서 relay 서비스에 upload_config 관련 환경변수 추가

### 3.3 relay-logs/relay-uploads 디렉토리 미생성

**증상**: 파일 업로드 시 디렉토리 없음 오류
**원인**: clean-and-init 스크립트가 relay용 로그/업로드 디렉토리를 생성하지 않았음
**해결**: `clean-and-init.sh`에 `relay-logs/`, `relay-uploads/` 디렉토리 생성 + 권한 설정 추가

### 3.4 relay volumes 마운트 누락

**증상**: 컨테이너 내부에서 업로드 파일/로그 접근 불가
**원인**: docker-compose에서 relay 서비스에 볼륨 마운트가 없었음
**해결**: relay 서비스에 `relay-logs`, `relay-uploads`, trust anchor 볼륨 마운트 추가

### 3.5 SSL upload proxy_request_buffering

**증상**: SSL 환경에서 대용량 LDIF 파일 업로드 시 HTTP/2 413 (Request Entity Too Large) 오류
**원인**: nginx SSL 설정에서 upload 경로에 `proxy_request_buffering off` 미설정
**해결**: `api-gateway-ssl.conf`의 upload location에 `proxy_request_buffering off` 추가

---

## 4. Relay 전용 DB Pool

LDIF 처리(30k+ 엔트리)는 장시간 DB 커넥션을 점유하므로, relay에 **전용 upload DB pool**을 추가하여 일반 API 요청과 분리.

---

## 5. 테스트 현황

- PKD Relay: **129개 신규 유닛 테스트** (GTest)
- 전체 프로젝트: **1,975개 테스트**

---

## 6. Known Limitations (Tech Debt)

- 프론트엔드 API 호출은 URL 경로 기반이므로 영향 없음 (nginx가 라우팅 처리)
- 개별 인증서 업로드(`/api/upload/certificate`)는 현재 management에 잔류 (로컬 운영 성격)
- LDIF/ML 처리 코드가 relay로 이동했으므로, relay의 코드베이스가 이전보다 커짐
- `docs/CERTIFICATE_PROCESSING_GUIDE.md`의 파일 경로 참조가 `pkd-relay-service/` 기준으로 업데이트됨

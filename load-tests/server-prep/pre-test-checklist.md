# 부하 테스트 사전 점검 목록

## 테스트 전 (모든 Phase)

- [ ] 모든 컨테이너 정상 실행 확인: `podman-health.sh` (8/8 healthy)
- [ ] DB 데이터 확인: 31,212 certificates, 69 CRLs
- [ ] LDAP 데이터 확인: `ldap_count_all`
- [ ] 테스트 데이터 추출 완료: `./data/seed-data.sh https://pkd.smartcoreinc.com`
- [ ] k6 설치 확인: `k6 version`
- [ ] Smoke 테스트 통과: `k6 run --insecure-skip-tls-verify tests/00-smoke.js`
- [ ] 서버 메트릭 수집 시작: `./monitoring/collect-metrics.sh`
- [ ] nginx access log 초기화: `podman exec api-gateway sh -c '> /var/log/nginx/access.log'`
- [ ] 기준 Container 메모리 기록: `podman stats --no-stream`

## Phase 2+ (Ramp-Up 이상)

- [ ] nginx 부하 테스트 설정 적용: `sudo ./server-prep/tune-server.sh apply`
- [ ] nginx 설정 검증: `podman exec api-gateway nginx -t`
- [ ] OS 커널 튜닝 확인: `sudo ./server-prep/tune-server.sh status`

## Phase 3+ (Stress 이상) — 추가 튜닝

- [ ] OCI Session Pool 확장 (코드 변경 필요, sessMax 10→50)
- [ ] LDAP Pool 확장 (코드 변경 필요, maxSize 10→50)
- [ ] Drogon Thread 증가: `THREAD_NUM=16` (docker-compose 환경변수)
- [ ] 서비스 재시작 후 재검증

## 테스트 후 (복구)

- [ ] Production 설정 복구: `sudo ./server-prep/restore-production.sh`
- [ ] 모든 컨테이너 정상 확인: `podman-health.sh`
- [ ] nginx access log에서 결과 추출
- [ ] 서버 메트릭 수집 중지
- [ ] k6 리포트 저장: `reports/` 디렉토리 확인

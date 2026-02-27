# ICAO Local PKD - Podman Deployment Guide (Production RHEL 9)

## Overview

Production 서버(RHEL 9, 10.0.0.220)에서 Docker 대신 Podman을 사용하여 시스템을 운영합니다.

| 환경 | 컨테이너 런타임 | 스크립트 |
|------|----------------|----------|
| Local (WSL2) | Docker | `scripts/docker/`, `docker-start.sh` |
| Luckfox (ARM64) | Docker | `scripts/luckfox/` |
| **Production (RHEL 9)** | **Podman** | **`scripts/podman/`, `podman-start.sh`** |

---

## 사전 요구사항

### RHEL 9 Podman 설치

```bash
# Podman은 RHEL 9에 기본 포함
sudo dnf install -y podman

# podman-compose (pip 설치 — dnf 버전은 구버전)
pip3 install --user podman-compose
# ~/.local/bin이 PATH에 있는지 확인

# CNI DNS 플러그인 (컨테이너 간 호스트명 해석 필수)
sudo dnf install -y podman-plugins

# Rootless 모드에서 80/443 포트 바인딩 허용
echo 'net.ipv4.ip_unprivileged_port_start=80' | sudo tee -a /etc/sysctl.conf
sudo sysctl -w net.ipv4.ip_unprivileged_port_start=80

# 버전 확인
podman --version          # 5.6.0+
podman-compose --version  # 1.5.0+
```

### 기존 Docker에서 마이그레이션

```bash
# 1. Docker 컨테이너 중지
sudo docker compose -f docker/docker-compose.yaml down

# 2. Docker 이미지를 Podman으로 복사 (skopeo)
for img in docker-pkd-management docker-pa-service docker-pkd-relay \
           docker-frontend docker-monitoring-service docker-ai-analysis; do
    sudo skopeo copy docker-daemon:$img:latest \
         containers-storage:docker.io/library/$img:latest
done

# 3. Docker CE 제거
sudo systemctl stop docker
sudo dnf remove -y docker-ce docker-ce-cli containerd.io \
    docker-buildx-plugin docker-compose-plugin
sudo rm -rf /var/lib/docker /var/lib/containerd
```

---

## Docker와의 주요 차이점

| 항목 | Docker | Podman | 비고 |
|------|--------|--------|------|
| 데몬 | dockerd (root) | 없음 (daemonless) | 보안 향상 |
| CLI | `docker` | `podman` | 호환 |
| Compose | `docker compose` | `podman-compose` | 별도 설치 |
| DNS | `127.0.0.11` (내장) | dnsname CNI 플러그인 | `podman-plugins` 필수 |
| 의존성 조건 | `service_healthy` | 스크립트로 처리 | **스크립트 변경** |
| Init 컨테이너 | `service_completed_successfully` | 스크립트로 처리 | **스크립트 변경** |
| Rootless | 지원 | **기본값** | SELinux MCS 주의 |
| SELinux 볼륨 | `:Z` 라벨 자동 | `chcon` 사전 라벨링 | `:Z` 사용 불가 |
| 특권 포트 | root 데몬 | sysctl 설정 필요 | `ip_unprivileged_port_start=80` |
| 네트워크 이름 | `{프로젝트명}_{네트워크명}` | 동일 | 프로젝트명=compose 파일 디렉토리명 |

### nginx DNS 해결

Docker는 내장 DNS(127.0.0.11)를 사용하지만, Podman CNI는 dnsname 플러그인을 사용합니다.
`scripts/podman/start.sh`가 자동으로:
1. Podman 네트워크(`docker_pkd-network`)의 게이트웨이 IP 감지
2. Docker용 nginx 설정 복사 + resolver IP 치환 (예: `resolver 10.89.1.1`)
3. `.docker-data/nginx/api-gateway.conf`에 생성된 설정 마운트

> **주의**: nginx upstream 블록은 시작 시 한 번만 DNS 해석합니다. 네트워크 재생성 후에는 반드시 api-gateway 컨테이너도 재시작해야 합니다.

### SELinux + Rootless Podman

RHEL 9 SELinux Enforcing 모드에서 rootless Podman은 특별한 처리가 필요합니다:

1. **`:Z`/`:z` 볼륨 라벨 사용 불가** — rootless Podman에 `CAP_MAC_ADMIN`이 없어 `lsetxattr` 호출 실패
2. **MCS 카테고리 불일치** — 각 rootless 컨테이너는 고유 MCS 라벨(예: `s0:c737,c1010`)로 실행되므로, 호스트 파일의 MCS 카테고리와 불일치
3. **해결**: 사전 `chcon` 라벨링 (2단계)
   ```bash
   # Step 1: SELinux 타입을 container_file_t로 설정
   chcon -Rt container_file_t .docker-data/ docker/db-oracle/init ...
   # Step 2: MCS 카테고리 제거 (s0만 남김 — 모든 컨테이너 접근 허용)
   chcon -R -l s0 .docker-data/ docker/db-oracle/init ...
   ```
4. **OpenLDAP 데이터 디렉토리** — rootless 컨테이너가 생성한 파일은 호스트에서 `podman unshare rm`으로만 삭제 가능

### LDAP 초기화

Docker 버전은 `ldap-mmr-setup1`, `ldap-mmr-setup2`, `ldap-init` 컨테이너를 사용하지만,
Podman 버전은 `clean-and-init.sh` 스크립트가 `podman exec`로 직접 MMR + DIT 초기화를 수행합니다.

---

## 파일 구조

```
scripts/podman/
├── start.sh          # 컨테이너 시작 (nginx DNS 자동 설정)
├── stop.sh           # 컨테이너 중지
├── restart.sh        # 컨테이너 재시작 [서비스명]
├── health.sh         # 전체 헬스 체크
├── logs.sh           # 로그 확인 [서비스명] [줄수]
├── clean-and-init.sh # 완전 초기화 (데이터 삭제 + LDAP DIT + 서비스 시작)
├── backup.sh         # 데이터 백업
└── restore.sh        # 데이터 복구

# 루트 편의 래퍼
podman-start.sh
podman-stop.sh
podman-health.sh
podman-clean-and-init.sh

# Podman 전용 Compose 파일
docker/docker-compose.podman.yaml
```

---

## 빠른 시작

### 1. 최초 설치 (Clean Install)

```bash
# .env 파일 설정
cp .env.example .env
# DB_TYPE=oracle 확인

# SSL 인증서 생성
scripts/ssl/init-cert.sh --ip 10.0.0.220

# 완전 초기화 (Oracle + LDAP + 서비스)
./podman-clean-and-init.sh

# 헬스 체크
./podman-health.sh
```

### 2. 일상 운영

```bash
# 시작
./podman-start.sh

# 중지
./podman-stop.sh

# 재시작 (전체 또는 특정 서비스)
scripts/podman/restart.sh
scripts/podman/restart.sh pkd-management

# 로그 확인
scripts/podman/logs.sh                    # 전체
scripts/podman/logs.sh pkd-management 50  # 특정 서비스, 50줄

# 헬스 체크
./podman-health.sh

# 백업
scripts/podman/backup.sh

# 복구
scripts/podman/restore.sh ./backups/20260227_103000
```

### 3. 서비스 재빌드

```bash
# 특정 서비스 재빌드
podman-compose -f docker/docker-compose.podman.yaml build pkd-management
podman-compose -f docker/docker-compose.podman.yaml up -d pkd-management

# 캐시 없이 재빌드
podman-compose -f docker/docker-compose.podman.yaml build --no-cache pkd-management
```

---

## Compose 파일 비교

### Docker (`docker-compose.yaml`)
```yaml
depends_on:
  openldap1:
    condition: service_healthy
  openldap2:
    condition: service_healthy
```

### Podman (`docker-compose.podman.yaml`)
```yaml
depends_on:
  - openldap1
  - openldap2
```

> Podman 버전에서는 `condition:` 제거.
> 서비스 준비 상태는 스크립트의 wait 로직으로 보장.

---

## LDAP 초기화 비교

### Docker: Init 컨테이너 방식
```yaml
# docker-compose.yaml에 정의
ldap-mmr-setup1:
  depends_on:
    openldap1:
      condition: service_healthy
  ...
  restart: "no"

ldap-init:
  depends_on:
    ldap-mmr-setup1:
      condition: service_completed_successfully
  ...
```

### Podman: 스크립트 방식
```bash
# clean-and-init.sh Step 6
# 1. OpenLDAP 준비 대기 (podman exec ldapsearch)
# 2. MMR 설정 (podman exec ldapmodify — 양쪽 노드)
# 3. DIT 초기화 (podman exec — init-pkd-dit.sh 실행)
# 4. 검증 (ldapsearch로 엔트리 확인)
```

---

## 트러블슈팅

### nginx 502 Bad Gateway / Service Unavailable

```bash
# 1. DNS resolver 확인
grep resolver .docker-data/nginx/api-gateway.conf
# → resolver 10.89.x.x valid=10s ipv6=off;

# 2. 컨테이너 내 DNS 해석 확인
podman exec icao-local-pkd-api-gateway getent hosts pkd-management
# → 10.89.1.xxx  pkd-management.dns.podman

# 3. nginx upstream이 캐시된 옛 IP를 사용 중이면 재시작
podman restart icao-local-pkd-api-gateway

# 4. 전체 재시작 (DNS 재감지)
./podman-stop.sh && ./podman-start.sh
```

### DNS 해석 실패 (TNS:no listener, Host is unreachable)

```bash
# podman-plugins 설치 확인
rpm -q podman-plugins
# → 미설치 시: sudo dnf install -y podman-plugins

# 네트워크 DNS 활성화 확인
podman network inspect docker_pkd-network | python3 -c \
  "import sys,json; print('dns_enabled:', json.load(sys.stdin)[0]['dns_enabled'])"
# → dns_enabled: True 이어야 정상

# DNS 비활성화 시: 네트워크 재생성
podman-compose -f docker/docker-compose.podman.yaml down
podman network rm docker_pkd-network
podman network create docker_pkd-network  # podman-plugins 설치 후 자동 DNS 활성화
```

### Oracle 시작 시간 초과 / ORA-01017

```bash
# Oracle은 첫 실행 시 3-5분 소요
podman logs -f icao-local-pkd-oracle

# 수동 헬스 체크
podman inspect icao-local-pkd-oracle --format='{{.State.Health.Status}}'

# ORA-01017 (invalid username/password) — init 스크립트 미실행
# SELinux MCS 라벨 문제로 /opt/oracle/scripts/startup/ 읽기 실패 가능
podman exec icao-local-pkd-oracle ls /opt/oracle/scripts/startup/
# → Permission denied 시: 호스트에서 MCS 라벨 수정
chcon -R -l s0 docker/db-oracle/init/

# 수동 init 스크립트 실행
podman exec icao-local-pkd-oracle bash -c \
  'cd /opt/oracle/scripts/startup && for f in *.sql; do echo "@$f" | sqlplus -s sys/"$ORACLE_PWD"@//localhost:1521/XEPDB1 as sysdba; done'
```

### SELinux 권한 문제 (RHEL 9)

```bash
# 증상: Permission denied, lsetxattr operation not permitted

# Rootless Podman에서 :Z/:z 볼륨 라벨은 사용 불가 (CAP_MAC_ADMIN 없음)
# 대신 사전 라벨링 수행:
chcon -Rt container_file_t .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
chcon -R -l s0 .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi

# OpenLDAP 데이터 삭제 시 (rootless 컨테이너가 생성한 파일)
podman unshare rm -rf .docker-data/openldap1 .docker-data/openldap2
```

### 특권 포트 바인딩 (80/443)

```bash
# 증상: rootlessport cannot expose privileged port 80
# 해결:
sudo sysctl -w net.ipv4.ip_unprivileged_port_start=80
echo 'net.ipv4.ip_unprivileged_port_start=80' | sudo tee -a /etc/sysctl.conf
```

### 컨테이너 이름 충돌

```bash
# 기존 컨테이너 정리
podman ps -a --filter "name=icao-local-pkd" --format "{{.Names}}" | xargs -r podman rm -f
```

---

## 접속 정보

| 서비스 | URL |
|--------|-----|
| Frontend (HTTPS) | https://pkd.smartcoreinc.com |
| Frontend (HTTP) | http://pkd.smartcoreinc.com |
| API Gateway (HTTPS) | https://pkd.smartcoreinc.com/api |
| API Gateway (HTTP) | http://pkd.smartcoreinc.com/api |
| API Gateway (내부) | http://localhost:18080/api |
| Frontend (직접) | http://localhost:13080 |
| Swagger UI | http://localhost:18090 |
| Oracle | localhost:11521 (XEPDB1) |
| OpenLDAP 1 | ldap://localhost:13891 |
| OpenLDAP 2 | ldap://localhost:13892 |

---

## Production 환경 정보 (10.0.0.220)

| 항목 | 값 |
|------|-----|
| OS | RHEL 9.7 (SELinux Enforcing) |
| Podman | 5.6.0 |
| podman-compose | 1.5.0 (pip) |
| Network backend | CNI + dnsname plugin |
| podman-plugins | 5.6.0-14 |
| DB | Oracle XE 21c |
| 네트워크 이름 | docker_pkd-network |

---

## 버전 이력

- v1.1.0 (2026-02-27): Docker→Podman 마이그레이션 완료, SELinux MCS 해결, DNS 플러그인 설정
- v1.0.0 (2026-02-27): 최초 Podman 지원 — Docker 스크립트에서 분리

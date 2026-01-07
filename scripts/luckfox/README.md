# Luckfox Docker Management Scripts

Luckfox ARM64 환경에서 ICAO Local PKD 서비스를 운영하기 위한 스크립트 모음입니다.
JVM 버전과 CPP 버전을 모두 지원합니다.

**최근 업데이트 (2026-01-07)**:
- ✅ Frontend 포트 변경: 3000 → 80 (nginx.conf 업데이트)
- ✅ API Gateway 지원 (포트 8080)
- ✅ luckfox-clean.sh 스크립트 추가
- ✅ 모든 스크립트 테스트 완료 및 검증

## Prerequisites

- Luckfox 접속: `ssh luckfox@192.168.100.11` (user: luckfox, password: luckfox)
- Docker 및 Docker Compose 설치 완료
- 스크립트를 Luckfox `/home/luckfox/scripts/` 디렉토리에 복사

## Scripts

| Script | Status | Description |
|--------|--------|-------------|
| `luckfox-common.sh` | ✅ | 공통 설정 및 함수 |
| `luckfox-start.sh` | ✅ | 서비스 시작 |
| `luckfox-stop.sh` | ✅ | 서비스 중지 |
| `luckfox-restart.sh` | ✅ | 서비스 재시작 |
| `luckfox-logs.sh` | ✅ | 로그 확인 |
| `luckfox-health.sh` | ✅ | 헬스 체크 (포트 80 지원) |
| `luckfox-backup.sh` | ✅ | 데이터 백업 (PostgreSQL) |
| `luckfox-restore.sh` | ✅ | 데이터 복구 |
| `luckfox-update.sh` | ✅ | 서비스 업데이트 |
| `luckfox-clean.sh` | ✅ | **[신규]** 전체 데이터 정리 |

## Setup

### 1단계: 스크립트 복사

```bash
# 개발 PC에서 Luckfox로 스크립트 복사 (sshpass 사용)
sshpass -p "luckfox" scp -o StrictHostKeyChecking=accept-new \
  scripts/luckfox/*.sh luckfox@192.168.100.11:/home/luckfox/scripts/

# 또는 SSH 키 인증 사용
scp scripts/luckfox/*.sh luckfox@192.168.100.11:/home/luckfox/scripts/
```

### 2단계: 실행 권한 부여

```bash
# Luckfox에서 실행
ssh luckfox@192.168.100.11
chmod +x /home/luckfox/scripts/*.sh
```

### 3단계: 동작 확인

```bash
# 헬스 체크로 모든 서비스 상태 확인
/home/luckfox/scripts/luckfox-health.sh cpp
```

## Version Selection

모든 스크립트는 첫 번째 인자로 `jvm` 또는 `cpp` 버전을 선택할 수 있습니다.
버전을 지정하지 않으면 기본값은 `cpp`입니다.

```bash
# CPP 버전 (기본값)
./luckfox-start.sh
./luckfox-start.sh cpp

# JVM 버전
./luckfox-start.sh jvm
```

## Version Directories

| Version | Directory | Docker Compose |
|---------|-----------|----------------|
| JVM | /home/luckfox/icao-local-pkd | docker-compose.yml |
| CPP | /home/luckfox/icao-local-pkd-cpp-v2 | docker-compose-luckfox.yaml |

## Usage Examples

### 서비스 시작/중지

```bash
# CPP 버전 전체 서비스 시작
./luckfox-start.sh cpp

# JVM 버전 전체 서비스 시작
./luckfox-start.sh jvm

# 특정 서비스만 시작
./luckfox-start.sh cpp frontend sync-service

# 서비스 중지
./luckfox-stop.sh cpp

# 서비스 재시작
./luckfox-restart.sh cpp pa-service
```

### 로그 확인

```bash
# CPP 서비스 로그 확인
./luckfox-logs.sh cpp sync-service

# JVM 서비스 로그 확인
./luckfox-logs.sh jvm backend

# 실시간 로그 추적
./luckfox-logs.sh cpp sync-service -f

# 사용법 보기
./luckfox-logs.sh -h
```

### 헬스 체크

```bash
# CPP 버전 헬스 체크
./luckfox-health.sh cpp

# JVM 버전 헬스 체크
./luckfox-health.sh jvm
```

출력 예시 (CPP):
```
=== ICAO Local PKD - Health Check ===
Version: CPP (C++/Drogon)
Directory: /home/luckfox/icao-local-pkd-cpp-v2

=== Container Status ===
NAME                    STATUS
icao-pkd-frontend       Up 2 minutes
icao-pkd-api-gateway    Up 2 minutes
icao-pkd-management     Up 2 minutes (healthy)
icao-pkd-pa-service     Up 2 minutes (healthy)
icao-pkd-postgres       Up 2 minutes
icao-pkd-sync-service   Up 2 minutes (healthy)

=== API Health Checks ===
PKD Management (8081): UP
PA Service (8082):     UP
Sync Service (8083):   UP
Frontend (80):         OK

=== Database Check ===
PostgreSQL: /var/run/postgresql:5432 - accepting connections
READY

=== Sync Status ===
Status: DISCREPANCY
(Install jq for detailed output)
```

> **주의**: Frontend는 이제 **포트 80**에서 실행됩니다. (이전: 3000)

### 데이터 정리 (Clean)

```bash
# ⚠️ 주의: 모든 데이터가 삭제됩니다!
./luckfox-clean.sh cpp

# 프롬프트에서 'yes' 입력으로 확인
# 삭제되는 항목:
#  - 모든 Docker 컨테이너
#  - 모든 Docker 볼륨 (named volumes)
#  - PostgreSQL 데이터 (data/postgres bind mount)
#  - 로그 파일들 (pkd-logs, pa-logs, sync-logs)
#  - 업로드된 파일들 (data/pkd-uploads)

# 완전한 정리 후 서비스 재시작
./luckfox-clean.sh cpp
./luckfox-start.sh cpp
```

> **주의**: clean script는 `sudo` 권한이 필요합니다 (bind mount 디렉토리 삭제용)

### 백업/복구

```bash
# CPP 버전 백업 생성 (PostgreSQL만)
./luckfox-backup.sh cpp

# JVM 버전 백업 생성
./luckfox-backup.sh jvm

# 특정 디렉토리에 백업
./luckfox-backup.sh cpp /mnt/usb/backups

# 백업 확인
ls -lh /home/luckfox/backups/

# 복구
./luckfox-restore.sh cpp /home/luckfox/backups/icao-pkd-cpp-backup-20260107_210413.tar.gz
```

> **참고**: 현재 LDAP 백업/복구는 수동 작업이 필요합니다. PostgreSQL 백업만 자동화되어 있습니다.

### 서비스 업데이트

#### 옵션 1: GitHub Actions 빌드 사용 (권장)

```bash
# 1. GitHub에서 ARM64 이미지 빌드
#    - feature/openapi-support 또는 feature/arm64-support 브랜치에 push
#    - GitHub Actions 자동 실행
#    - arm64-docker-images-all.zip 다운로드

# 2. 로컬 PC에서 Luckfox로 이미지 전송
sshpass -p "luckfox" scp -o StrictHostKeyChecking=accept-new \
  frontend-arm64-fixed.tar.gz luckfox@192.168.100.11:/home/luckfox/

# 3. Luckfox에서 이미지 로드 및 업데이트
ssh luckfox@192.168.100.11
docker load < frontend-arm64-fixed.tar.gz
/home/luckfox/scripts/luckfox-update.sh cpp /home/luckfox/frontend-arm64-fixed.tar.gz frontend
```

#### 옵션 2: 로컬 빌드 (CPP는 Luckfox에서 빌드해야 함)

```bash
# ⚠️ ARM64는 반드시 Luckfox에서 직접 빌드하세요!
# 로컬 cross-compile 금지

# Luckfox에서 직접 빌드:
ssh luckfox@192.168.100.11
cd ~/icao-local-pkd-cpp-v2/services/sync-service
docker build -f Dockerfile.local -t icao-local-sync:arm64-v1.2.0 .
cd ~/icao-local-pkd-cpp-v2
docker compose -f docker-compose-luckfox.yaml up -d
```

## Services & Access URLs

### CPP Version (권장)

| Service | Port | Access URL | Description |
|---------|------|-----------|-------------|
| **Frontend** | **80** | http://192.168.100.11/ | React Web UI |
| **API Gateway** | **8080** | http://192.168.100.11:8080/api | 통합 API 진입점 |
| PKD Management | 8081 | http://192.168.100.11:8081/api | PKD 관리 API |
| PA Service | 8082 | http://192.168.100.11:8082/api | PA 검증 API |
| Sync Service | 8083 | http://192.168.100.11:8083/api | DB-LDAP 동기화 API |
| PostgreSQL | 5432 | - | Database (DB: localpkd) |
| HAProxy LDAP | 389 | ldap://localhost:389 | LDAP 로드 밸런서 |

### JVM Version (레거시)

| Service | Port | Description |
|---------|------|-------------|
| postgres | 5432 | PostgreSQL (DB: pkd) |
| openldap | 389 | OpenLDAP server |
| backend | 8080 | Spring Boot API |
| frontend | 3000 | React Web UI |

### 마이크로서비스 아키텍처

```
┌─────────────────────────────────────────┐
│   Browser: http://192.168.100.11 (80)  │
└────────────────┬────────────────────────┘
                 │
         ┌───────▼────────┐
         │  Frontend      │
         │  (nginx:80)    │
         └───────┬────────┘
                 │
         ┌───────▼──────────────┐
         │  API Gateway (8080)  │
         │  (nginx)             │
         └─┬──────┬──────┬──────┘
           │      │      │
    ┌──────▼─┐ ┌──▼──┐ ┌─▼────────┐
    │ PKD    │ │ PA  │ │ Sync     │
    │ Mgmt   │ │Svc  │ │ Svc      │
    │ (8081) │ │(8082) │ (8083)   │
    └────┬───┘ └──┬──┘ └─┬────────┘
         │        │      │
         └────────┼──────┤
                  │      │
          ┌───────▼──┐ ┌─▼──────┐
          │PostgreSQL│ │HAProxy  │
          │(localpkd)│ │LDAP:389 │
          └──────────┘ └─────────┘
```

## Troubleshooting

### 1. 서비스가 시작되지 않음

```bash
# 로그 확인
./luckfox-logs.sh cpp <service> -f

# 컨테이너 상태 확인
docker ps -a

# 헬스 체크
./luckfox-health.sh cpp
```

### 2. Frontend에 접속할 수 없음

```bash
# 포트 80이 리스닝 중인지 확인
netstat -tuln | grep :80

# nginx 설정 확인
docker exec icao-pkd-frontend cat /etc/nginx/conf.d/default.conf | head -5

# 예상 출력:
# server {
#     listen 80;
```

### 3. sync_status 테이블 오류 (CPP only)

```bash
# PostgreSQL 접속
docker exec -it icao-pkd-postgres psql -U pkd -d localpkd

# 테이블 존재 확인
\dt sync_status

# 테이블이 없으면 생성
CREATE TABLE sync_status (
    id SERIAL PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    db_csca_count INTEGER DEFAULT 0,
    ...
    status VARCHAR(20) DEFAULT 'UNKNOWN'
);
# (전체 스키마는 CLAUDE.md의 sync_status Table Schema 섹션 참조)
```

### 4. 디스크 공간 부족

```bash
# 디스크 사용량 확인
df -h

# Docker 정리
docker system prune -a --volumes -f

# 기존 이미지만 정리
docker image prune -a
```

### 5. LDAP 연결 오류

```bash
# HAProxy 통해 LDAP 테스트
ldapsearch -x -H ldap://localhost:389 \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin123 \
    -b "dc=ldap,dc=smartcoreinc,dc=com" "(objectClass=*)" dn | wc -l

# 결과: LDAP 엔트리 수 출력
```

### 6. API Gateway 응답 없음

```bash
# API Gateway 상태 확인
curl -v http://localhost:8080/

# PKD Management API 직접 테스트
curl http://localhost:8081/api/health

# 라우팅 확인
docker logs icao-pkd-api-gateway | tail -50
```

### 7. 이전 포트 3000 설정 제거 (마이그레이션)

Frontend가 이전에 포트 3000에서 실행 중이었다면:

```bash
# 설정 파일 확인
cat ./frontend/nginx-luckfox.conf | grep listen

# 수정 필요: listen 3000 → listen 80

# 또는 자동 수정
sed -i 's/listen 3000;/listen 80;/' ./frontend/nginx-luckfox.conf

# docker-compose에서도 포트 확인
grep -A 5 'frontend:' docker-compose-luckfox.yaml
```

## Database Names

| Version | Database Name | User |
|---------|---------------|------|
| JVM | pkd | pkd |
| CPP | localpkd | pkd |

## ARM64 Build Rules (절대 규칙)

```bash
# ✅ 올바른 방법: Luckfox에서 직접 빌드
ssh luckfox@192.168.100.11
cd ~/icao-local-pkd-cpp-v2/services/sync-service
docker build -f Dockerfile.local -t icao-local-sync:arm64-v1.2.0 .

# ❌ 절대 금지: 로컬에서 cross-compile
docker build --platform linux/arm64 -t ... .

# ❌ 절대 금지: 로컬에서 빌드 후 전송
docker build --platform linux/arm64 ... && docker save ... | scp ...
```

> **이유**: Luckfox는 ARM64 네이티브 시스템이므로, 로컬 cross-compile은 비효율적이고 환경 불일치 문제 발생 가능

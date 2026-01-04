# Luckfox Docker Management Scripts

Luckfox ARM64 환경에서 ICAO Local PKD 서비스를 운영하기 위한 스크립트 모음입니다.
JVM 버전과 CPP 버전을 모두 지원합니다.

## Prerequisites

- Luckfox 접속: `ssh luckfox@192.168.100.11`
- Docker 및 Docker Compose 설치 완료
- 스크립트를 Luckfox `/home/luckfox/scripts/` 디렉토리에 복사

## Scripts

| Script | Description |
|--------|-------------|
| `luckfox-common.sh` | 공통 설정 및 함수 |
| `luckfox-start.sh` | 서비스 시작 |
| `luckfox-stop.sh` | 서비스 중지 |
| `luckfox-restart.sh` | 서비스 재시작 |
| `luckfox-logs.sh` | 로그 확인 |
| `luckfox-health.sh` | 헬스 체크 |
| `luckfox-backup.sh` | 데이터 백업 |
| `luckfox-restore.sh` | 데이터 복구 |
| `luckfox-update.sh` | 서비스 업데이트 |

## Setup

```bash
# 개발 PC에서 Luckfox로 스크립트 복사
scp scripts/luckfox/*.sh luckfox@192.168.100.11:/home/luckfox/scripts/

# Luckfox에서 실행 권한 부여
ssh luckfox@192.168.100.11
chmod +x /home/luckfox/scripts/*.sh
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
NAME                     STATUS
icao-pkd-postgres        Up 2 hours
icao-pkd-openldap        Up 2 hours
...

=== API Health Checks ===
PKD Management (8081): UP
PA Service (8082):     UP
Sync Service (8083):   UP
Frontend (3000):        OK

=== Sync Status ===
{
  "status": "SYNCED",
  "db": {"csca": 525, "dsc": 29805, "dscNc": 502},
  "ldap": {"csca": 525, "dsc": 29805, "dscNc": 502}
}
```

### 백업/복구

```bash
# CPP 버전 백업 생성
./luckfox-backup.sh cpp

# JVM 버전 백업 생성
./luckfox-backup.sh jvm

# 특정 디렉토리에 백업
./luckfox-backup.sh cpp /mnt/usb/backups

# 복구
./luckfox-restore.sh cpp /home/luckfox/backups/icao-pkd-cpp-backup-20260104_120000.tar.gz
```

### 서비스 업데이트

```bash
# 1. 개발 PC에서 이미지 빌드 및 전송
docker build --platform linux/arm64 --no-cache -t icao-frontend:arm64 .
docker save icao-frontend:arm64 | gzip > icao-frontend-arm64.tar.gz
scp icao-frontend-arm64.tar.gz luckfox@192.168.100.11:/home/luckfox/

# 2. Luckfox에서 업데이트 적용
./luckfox-update.sh cpp /home/luckfox/icao-frontend-arm64.tar.gz frontend
```

## Services

### CPP Version

| Service | Port | Description |
|---------|------|-------------|
| postgres | 5432 | PostgreSQL (DB: localpkd) |
| openldap | 389 | OpenLDAP server |
| pkd-management | 8081 | PKD Management API |
| pa-service | 8082 | PA Verification API |
| sync-service | 8083 | DB-LDAP Sync API |
| frontend | 3000 | React Web UI |

### JVM Version

| Service | Port | Description |
|---------|------|-------------|
| postgres | 5432 | PostgreSQL (DB: pkd) |
| openldap | 389 | OpenLDAP server |
| backend | 8080 | Spring Boot API |
| frontend | 3000 | React Web UI |

## Troubleshooting

### 서비스가 시작되지 않음

```bash
# 로그 확인
./luckfox-logs.sh cpp <service> -f

# 컨테이너 상태 확인
docker ps -a
```

### sync_status 테이블 오류 (CPP only)

```bash
# PostgreSQL 접속
docker exec -it icao-pkd-postgres psql -U pkd -d localpkd

# 테이블 존재 확인
\dt sync_status

# 테이블이 없으면 생성 (docs/BUILD.md 참조)
```

### 이미지 로드 실패

```bash
# 디스크 공간 확인
df -h

# 기존 이미지 정리
docker image prune -a
```

### DB 이름 차이

| Version | Database Name |
|---------|---------------|
| JVM | pkd |
| CPP | localpkd |

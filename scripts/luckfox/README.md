# Luckfox Docker Management Scripts

Luckfox ARM64 환경에서 ICAO Local PKD 서비스를 운영하기 위한 스크립트 모음입니다.

## Prerequisites

- Luckfox 접속: `ssh luckfox@192.168.100.11`
- Docker 및 Docker Compose 설치 완료
- 스크립트를 Luckfox `/home/luckfox/scripts/` 디렉토리에 복사

## Scripts

| Script | Description |
|--------|-------------|
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

## Usage Examples

### 서비스 시작/중지

```bash
# 전체 서비스 시작
./luckfox-start.sh

# 특정 서비스만 시작
./luckfox-start.sh frontend sync-service

# 서비스 중지
./luckfox-stop.sh

# 서비스 재시작
./luckfox-restart.sh pa-service
```

### 로그 확인

```bash
# 서비스별 로그 확인
./luckfox-logs.sh sync-service

# 실시간 로그 추적
./luckfox-logs.sh sync-service -f

# 사용법 보기
./luckfox-logs.sh
```

### 헬스 체크

```bash
./luckfox-health.sh
```

출력 예시:
```
=== Container Status ===
NAME                     STATUS
icao-pkd-postgres        Up 2 hours
icao-pkd-openldap        Up 2 hours
icao-pkd-management      Up 2 hours
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
# 백업 생성
./luckfox-backup.sh

# 특정 디렉토리에 백업
./luckfox-backup.sh /mnt/usb/backups

# 복구
./luckfox-restore.sh /home/luckfox/backups/icao-pkd-backup-20260104_120000.tar.gz
```

### 서비스 업데이트

```bash
# 1. 개발 PC에서 이미지 빌드 및 전송
docker build --platform linux/arm64 --no-cache -t icao-frontend:arm64 .
docker save icao-frontend:arm64 | gzip > icao-frontend-arm64.tar.gz
scp icao-frontend-arm64.tar.gz luckfox@192.168.100.11:/home/luckfox/

# 2. Luckfox에서 업데이트 적용
./luckfox-update.sh /home/luckfox/icao-frontend-arm64.tar.gz frontend
```

## Services

| Service | Port | Description |
|---------|------|-------------|
| postgres | 5432 | PostgreSQL (DB: localpkd) |
| openldap | 389 | OpenLDAP server |
| pkd-management | 8081 | PKD Management API |
| pa-service | 8082 | PA Verification API |
| sync-service | 8083 | DB-LDAP Sync API |
| frontend | 3000 | React Web UI |

## Troubleshooting

### 서비스가 시작되지 않음

```bash
# 로그 확인
./luckfox-logs.sh <service> -f

# 컨테이너 상태 확인
docker ps -a
```

### sync_status 테이블 오류

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

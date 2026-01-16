# ICAO Local PKD - Luckfox 배포 가이드

**Device**: Luckfox Pico (ARM64)
**IP Address**: 192.168.100.11
**Project Directory**: `/home/luckfox/icao-local-pkd-cpp-v2`
**Current Version**: v1.6.1 (PKD Management), v1.3.0 (Sync Service)
**Last Deployed**: 2026-01-16 14:27:34 (KST)
**Updated**: 2026-01-16

---

## 🚀 빠른 시작

```bash
cd /home/luckfox/icao-local-pkd-cpp-v2

# 시스템 시작
./luckfox-start.sh

# 헬스체크
./luckfox-health.sh

# 로그 확인
./luckfox-logs.sh [서비스명]
```

---

## 📋 관리 스크립트

| 스크립트 | 설명 | 사용법 |
|---------|------|--------|
| **luckfox-start.sh** | 모든 컨테이너 시작 | `./luckfox-start.sh [--build]` |
| **luckfox-stop.sh** | 모든 컨테이너 중지 | `./luckfox-stop.sh` |
| **luckfox-restart.sh** | 컨테이너 재시작 | `./luckfox-restart.sh [서비스명]` |
| **luckfox-logs.sh** | 로그 확인 | `./luckfox-logs.sh [서비스명] [줄수]` |
| **luckfox-health.sh** | 시스템 헬스체크 | `./luckfox-health.sh` |
| **luckfox-clean.sh** | 완전 초기화 (⚠️ 데이터 삭제) | `./luckfox-clean.sh` |
| **luckfox-backup.sh** | 데이터 백업 | `./luckfox-backup.sh` |
| **luckfox-restore.sh** | 데이터 복구 | `./luckfox-restore.sh <백업파일>` |

---

## 🔧 서비스 구성

### 컨테이너 목록

| 컨테이너명 | 서비스 | 포트 | 이미지 | 현재 버전 |
|-----------|--------|------|--------|----------|
| **icao-pkd-postgres** | PostgreSQL DB | 5432 | postgres:15 | 15 |
| **icao-pkd-management** | PKD 관리 API | 8081 | icao-local-management:arm64 | **v1.6.1** |
| **icao-pkd-pa-service** | PA 검증 API | 8082 | icao-local-pa:arm64-v3 | v2.1.0 |
| **icao-pkd-sync-service** | DB-LDAP 동기화 | 8083 | icao-local-sync:arm64-v1.2.0 | **v1.3.0** |
| **icao-pkd-api-gateway** | Nginx 게이트웨이 | 8080 | nginx:alpine | alpine |
| **icao-pkd-frontend** | React 프론트엔드 | 3000 | icao-local-pkd-frontend:arm64-fixed | Latest |

### 접속 정보

| 서비스 | URL/주소 | 설명 |
|--------|----------|------|
| **Frontend** | http://192.168.100.11 | 웹 UI (포트 80) |
| **API Gateway** | http://192.168.100.11:8080/api | 통합 API 엔드포인트 |
| **PostgreSQL** | 127.0.0.1:5432 | DB: localpkd, User: pkd, Pass: pkd |

---

## 💡 사용 예제

### 1. 시스템 시작 (처음 또는 재부팅 후)

```bash
cd /home/luckfox/icao-local-pkd-cpp-v2
./luckfox-start.sh
```

### 2. 헬스체크

```bash
./luckfox-health.sh
```

**출력 예시**:
```
❤️  ICAO PKD 시스템 헬스체크 (Luckfox)
========================================

📊 컨테이너 상태:
icao-pkd-management     Up 5 minutes (healthy)
icao-pkd-pa-service     Up 5 minutes (healthy)
...

🗄️  PostgreSQL 연결 테스트:
   ✅ PostgreSQL: 정상
      - 테이블 수: 15
      - 인증서 수: 30637
```

### 3. 로그 확인

```bash
# 전체 로그 (최근 50줄)
./luckfox-logs.sh

# 특정 서비스 로그
./luckfox-logs.sh pkd-management 100

# 실시간 로그
./luckfox-logs.sh pkd-management -f
```

**서비스명**: `postgres`, `pkd-management`, `pa-service`, `sync-service`, `api-gateway`, `frontend`

### 4. 특정 서비스 재시작

```bash
# Frontend만 재시작
./luckfox-restart.sh frontend

# PKD Management 재시작
./luckfox-restart.sh pkd-management
```

### 5. 백업 및 복구

```bash
# 백업 생성
./luckfox-backup.sh
# 결과: backups/luckfox_20260113_105300.tar.gz

# 백업 목록 확인
ls -lh backups/

# 복구
./luckfox-restore.sh backups/luckfox_20260113_105300.tar.gz
```

### 6. 완전 초기화 (⚠️ 주의: 모든 데이터 삭제)

```bash
./luckfox-clean.sh
# 확인 프롬프트: "yes" 입력 필요
```

**초기화되는 항목**:
- PostgreSQL 데이터 (`.docker-data/postgres/*`)
- 업로드 파일 (`.docker-data/pkd-uploads/*`)
- 모든 컨테이너

---

## 🔄 새 버전 배포

### 자동화 배포 스크립트 사용 (권장) ⭐

**로컬 개발 환경에서 실행**:

```bash
# 1. GitHub Actions 빌드 완료 확인
# https://github.com/iland112/icao-local-pkd-cpp/actions

# 2. 자동 배포 스크립트 실행 (artifacts 자동 다운로드)
./scripts/deploy-from-github-artifacts.sh all

# 또는 개별 서비스만 배포
./scripts/deploy-from-github-artifacts.sh pkd-management
./scripts/deploy-from-github-artifacts.sh sync-service
./scripts/deploy-from-github-artifacts.sh frontend
```

**배포 스크립트 기능**:
- ✅ GitHub Actions artifacts 자동 다운로드 (main 브랜치)
- ✅ 자동 백업 생성 (`/home/luckfox/icao-backup-YYYYMMDD_HHMMSS/`)
- ✅ OCI 형식 → Docker 형식 자동 변환 (skopeo)
- ✅ sshpass를 통한 비대화형 SSH/SCP 인증
- ✅ 기존 컨테이너/이미지 자동 정리
- ✅ 이미지 전송 및 로드
- ✅ 서비스 시작 및 헬스체크

**최근 배포 이력**:
- **2026-01-16 14:27:34**: v1.6.1 (PKD Management), v1.3.0 (Sync Service)
  - GitHub Actions Run ID: 21053986767
  - 백업: `/home/luckfox/icao-backup-20260116_142626/`
  - 새 기능: Certificate Search, Countries API, Export

### 수동 배포 (대안)

```bash
cd /home/luckfox/icao-local-pkd-cpp-v2

# 1. 백업 생성
./luckfox-backup.sh

# 2. 기존 컨테이너 중지
./luckfox-stop.sh

# 3. 새 이미지 로드 (개발자가 전송한 파일)
docker load < /tmp/icao-management-arm64.tar

# 4. 서비스 시작
./luckfox-start.sh

# 5. 버전 확인
./luckfox-logs.sh pkd-management | grep "ICAO Local PKD"
```

### 이미지 버전 관리

**docker-compose-luckfox.yaml에 정의된 이미지 태그**:
- `icao-local-management:arm64` (PKD Management)
- `icao-local-pa:arm64-v3` (PA Service)
- `icao-local-sync:arm64-v1.2.0` (Sync Service)
- `icao-local-pkd-frontend:arm64-fixed` (Frontend)

---

## 🗂️ 디렉토리 구조

```
/home/luckfox/icao-local-pkd-cpp-v2/
├── docker-compose-luckfox.yaml    # Docker Compose 설정
├── luckfox-*.sh                   # 관리 스크립트들
├── LUCKFOX_README.md              # 이 파일
├── .docker-data/                  # 데이터 디렉토리 (gitignored)
│   ├── postgres/                  # PostgreSQL 데이터
│   └── pkd-uploads/               # 업로드된 LDIF/ML 파일
└── backups/                       # 백업 파일들
```

---

## ⚠️ 주의사항

### Host Network Mode

Luckfox는 **host network mode**를 사용합니다.
- 모든 컨테이너가 호스트 네트워크 네임스페이스 공유
- 포트 바인딩 없음 (127.0.0.1 직접 접근)
- 외부 접근: 192.168.100.11 (Luckfox IP)

### PostgreSQL 데이터베이스명

- **Luckfox**: `localpkd`
- **로컬 개발 환경**: `pkd`

주의: 로컬 환경의 백업을 Luckfox에 복구할 때 데이터베이스명 차이 확인 필요!

### 디스크 공간 관리

```bash
# 디스크 사용량 확인
df -h /home/luckfox/.docker-data

# 오래된 백업 삭제
rm -rf backups/luckfox_202601*.tar.gz

# Docker 이미지 정리
docker image prune -a
```

---

## 🐛 문제 해결

### 컨테이너가 시작되지 않을 때

```bash
# 1. 로그 확인
./luckfox-logs.sh [서비스명]

# 2. 컨테이너 상태 확인
docker ps -a

# 3. 컨테이너 재생성
./luckfox-stop.sh
docker compose -f docker-compose-luckfox.yaml rm -f [서비스명]
./luckfox-start.sh
```

### PostgreSQL 연결 실패

```bash
# PostgreSQL 컨테이너 상태 확인
docker logs icao-pkd-postgres

# 재시작
./luckfox-restart.sh postgres

# 완전 재시작
./luckfox-stop.sh
./luckfox-start.sh
```

### 디스크 공간 부족

```bash
# 1. 사용량 확인
du -sh .docker-data/*

# 2. 오래된 업로드 파일 삭제
rm -rf .docker-data/pkd-uploads/old_files

# 3. Docker 정리
docker system prune -a
```

### 버전 확인

```bash
# PKD Management 버전
docker logs icao-pkd-management 2>&1 | grep "ICAO Local PKD"

# 이미지 목록
docker images | grep icao
```

---

## 📞 지원

**프로젝트 저장소**: https://github.com/iland112/icao-local-pkd-cpp
**배포 가이드**: `docs/LUCKFOX_DEPLOYMENT.md`
**Docker 빌드 캐시**: `docs/DOCKER_BUILD_CACHE.md`

---

## 🆕 최신 기능 (v1.6.1)

### Certificate Search (v1.6.0)
- LDAP 기반 실시간 인증서 검색
- 국가별, 타입별, 검증 상태별 필터링
- Subject DN, Serial 텍스트 검색
- 페이지네이션 지원

### Countries API (v1.6.2)
- PostgreSQL DISTINCT 쿼리 사용 (40ms 응답)
- 92개 국가 목록 제공
- 프론트엔드 드롭다운에 국기 아이콘 표시

### Certificate Export (v1.6.0)
- 단일 인증서 Export (DER/PEM 형식)
- 국가별 전체 인증서 ZIP Export
- CSCA, DSC, DSC_NC, CRL 모두 지원

### Failed Upload Cleanup (v1.4.8)
- 실패한 업로드 삭제 기능
- DB 및 임시 파일 자동 정리

---

**Last Updated**: 2026-01-16
**Current Version**: v1.6.1 (PKD Management), v1.3.0 (Sync Service)
**Last Deployment**: 2026-01-16 14:27:34 (KST)

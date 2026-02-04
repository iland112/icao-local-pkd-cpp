# Development Environment Scripts

Development 환경에서 신규 기능 개발 및 테스트를 위한 스크립트입니다.

## 개요

**목적**: Production 환경을 건드리지 않고 안전하게 개발
**격리 범위**: pkd-management 서비스만 development 버전으로 실행
**공유 리소스**: PostgreSQL, LDAP, 기타 서비스는 production 공유

## 포트 구성

| Service | Production Port | Development Port |
|---------|----------------|------------------|
| pkd-management | 8081 | **8091** |
| pa-service | 8082 | 8092 (기존) |
| pkd-relay | 8083 | (공유) |
| API Gateway | 8080 | (공유) |
| Frontend | 3000 | (공유) |

## 사용 방법

### 1. Production 서비스 시작 (필수)

Development 환경은 PostgreSQL, LDAP 등 production 리소스를 공유합니다.

```bash
# 프로젝트 루트에서
./docker-start.sh
```

### 2. Development 서비스 시작

```bash
cd scripts/dev

# PKD Management 개발 서비스 시작
./start-pkd-dev.sh
```

### 3. 로그 확인

```bash
./logs-pkd-dev.sh
```

### 4. 코드 수정 후 재빌드

```bash
# 빠른 재빌드 (캐시 사용)
./rebuild-pkd-dev.sh

# Clean 빌드 (배포 전 필수)
./rebuild-pkd-dev.sh --no-cache
```

### 5. Development 서비스 중지

```bash
./stop-pkd-dev.sh
```

## API 엔드포인트

Development 서비스는 **port 8091**에서 실행됩니다.

```bash
# Health check
curl http://localhost:8091/api/health

# Upload history
curl http://localhost:8091/api/upload/history

# Certificate search
curl http://localhost:8091/api/certificates/search?country=KR
```

## 개발 워크플로우

### Step 1: Branch 생성 및 Development 환경 시작

```bash
git checkout -b feature/your-feature
./docker-start.sh                    # Production 환경
cd scripts/dev && ./start-pkd-dev.sh  # Development 환경
```

### Step 2: 코드 수정

```bash
# services/pkd-management/src/ 에서 코드 수정
vim services/pkd-management/src/main.cpp
```

### Step 3: 빌드 및 테스트

```bash
# 재빌드 (빠른 피드백)
./rebuild-pkd-dev.sh

# 로그 확인
./logs-pkd-dev.sh

# API 테스트
curl http://localhost:8091/api/...
```

### Step 4: Clean 빌드 및 검증

```bash
# Clean 빌드로 최종 검증
./rebuild-pkd-dev.sh --no-cache

# E2E 테스트
# ...
```

### Step 5: Commit 및 병합

```bash
git add -A
git commit -m "feat: ..."
git push origin feature/your-feature

# PR 생성 및 리뷰
# ...

# Main branch 병합
git checkout main
git merge feature/your-feature
```

## 주의사항

### ⚠️ 공유 리소스

- **PostgreSQL**: Production과 동일한 DB 사용 (테이블 공유)
- **LDAP**: Production과 동일한 LDAP 사용 (데이터 공유)
- **주의**: Development에서 DB/LDAP 수정 시 production에 영향

### ✅ 격리된 리소스

- **pkd-management-dev 컨테이너**: 독립 실행
- **포트 8091**: Production과 분리
- **로그 볼륨**: `pkd-management-dev-logs`

### 🔒 테스트 데이터 관리

Production DB/LDAP를 사용하므로:

1. 테스트 데이터는 명확히 구분 가능하게 생성
2. 테스트 후 데이터 정리 권장
3. 중요 데이터 수정 전 백업

## 파일 구조

```
scripts/dev/
├── README.md                   # 이 파일
├── start-pkd-dev.sh           # 개발 서비스 시작
├── stop-pkd-dev.sh            # 개발 서비스 중지
├── logs-pkd-dev.sh            # 로그 확인
├── rebuild-pkd-dev.sh         # 재빌드
└── (pa-service dev scripts)   # PA 서비스 개발 스크립트

docker/
└── docker-compose.dev.yaml    # Development Docker Compose
```

## 트러블슈팅

### 문제: "Production network 'pkd-network' not found"

**해결**: Production 환경을 먼저 시작하세요.

```bash
./docker-start.sh
```

### 문제: 포트 8091 already in use

**해결**: 기존 개발 서비스를 중지하세요.

```bash
./stop-pkd-dev.sh
```

### 문제: 빌드 실패

**해결**: Clean 빌드를 시도하세요.

```bash
./rebuild-pkd-dev.sh --no-cache
```

### 문제: Health check 실패

**해결**: 로그를 확인하세요.

```bash
./logs-pkd-dev.sh
```

## 참고 자료

- [DEVELOPMENT_GUIDE.md](../../docs/DEVELOPMENT_GUIDE.md) - 전체 개발 가이드
- [CERTIFICATE_FILE_UPLOAD_DESIGN_V2.md](../../docs/CERTIFICATE_FILE_UPLOAD_DESIGN_V2.md) - 신규 기능 설계
- [docker-compose.dev.yaml](../../docker/docker-compose.dev.yaml) - Development 환경 구성

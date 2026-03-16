# Frontend Build and Deployment Guide

**작성일**: 2026-01-14
**목적**: Frontend 빌드 및 배포 시 구 버전 이미지 문제 방지

---

## 문제점 분석

### 발생한 문제들

#### 1. `docker compose restart`의 함정
```bash
# ❌ 잘못된 방법
cd frontend && npm run build
docker compose restart frontend

# 결과: 구 이미지를 사용하는 컨테이너만 재시작됨
```

**원인**: `restart` 명령은 이미지를 다시 빌드하지 않음

#### 2. `docker compose up -d --build`의 부작용
```bash
# ❌ 문제 있는 방법
docker compose up -d --build frontend

# 결과: pa-service, pkd-management 등 모든 서비스가 함께 빌드됨
# frontend는 1분이면 되는데 pa-service vcpkg 때문에 10분+ 대기
```

**원인**: docker-compose.yaml의 서비스 의존성

#### 3. 빌드는 되었는데 컨테이너가 구 이미지 사용
```bash
# 빌드 로그: "exporting to image docker-frontend:latest"
# 하지만 컨테이너는 여전히 구 이미지의 SHA 사용

docker compose ps frontend
# IMAGE: sha256:c48cccb8d... (구 이미지)
```

**원인**: 컨테이너가 자동으로 재생성되지 않음

---

## 올바른 빌드 및 배포 방법

### 방법 1: 자동화 스크립트 사용 (권장) ⭐

```bash
# 한 번에 모든 단계 자동 실행
./scripts/frontend-rebuild.sh
```

**수행 작업**:
1. ✅ Frontend 로컬 빌드 (`npm run build`)
2. ✅ 구 컨테이너 중지 및 삭제
3. ✅ 구 이미지 삭제
4. ✅ 새 이미지 빌드 (다른 서비스 빌드 안 함)
5. ✅ 새 컨테이너 시작
6. ✅ 빌드 검증

**장점**:
- 다른 서비스에 영향 없음
- 구 이미지 완전 삭제로 확실한 최신 버전 보장
- 자동 검증으로 실수 방지

### 방법 2: 수동 단계별 실행

```bash
# Step 1: 로컬 빌드
cd frontend
npm run build
cd ..

# Step 2: 구 컨테이너/이미지 제거
docker compose -f docker/docker-compose.yaml rm -sf frontend
docker rmi -f docker-frontend:latest

# Step 3: 새 이미지 빌드 (frontend만)
docker build -t docker-frontend:latest -f frontend/Dockerfile frontend/

# Step 4: 새 컨테이너 시작
docker compose -f docker/docker-compose.yaml up -d frontend

# Step 5: 검증
./scripts/verify-frontend-build.sh
```

---

## 빌드 검증

### 자동 검증 스크립트

```bash
./scripts/verify-frontend-build.sh
```

**출력 예시**:
```
==================================
Frontend Build Verification
==================================

Local build:
  File: index-CD4LBpLv.js
  Size: 1702745 bytes
  Time: Tue Jan 14 13:46:00 KST 2026

Container build:
  File: index-CD4LBpLv.js
  Size: 1702745 bytes

Comparison:
✅ MATCH: Container is using the latest build

Container image: sha256:67789181cf88...
Image created: 2026-01-14T13:50:23.456789Z
```

### 수동 검증

```bash
# 로컬 빌드 파일 확인
ls -lh frontend/dist/assets/index-*.js

# 컨테이너 내 파일 확인
docker compose -f docker/docker-compose.yaml exec frontend \
    ls -lh /usr/share/nginx/html/assets/index-*.js

# 파일명과 크기가 일치해야 함
```

---

## 일반적인 함정 및 해결책

### 함정 1: npm build 후 docker restart만 실행
```bash
# ❌ 잘못된 방법
npm run build
docker compose restart frontend  # 구 이미지로 재시작됨
```

**해결책**: 이미지를 다시 빌드해야 함
```bash
# ✅ 올바른 방법
./scripts/frontend-rebuild.sh
```

### 함정 2: docker-compose.yaml에서 build context 잘못 이해
```yaml
# docker-compose.yaml
services:
  frontend:
    build:
      context: ../frontend  # ← 이 경로에서 빌드
    image: docker-frontend:latest
```

- `context`는 Dockerfile이 실행되는 작업 디렉토리
- `docker compose build`는 이 context를 사용
- 로컬 `npm run build`와는 별개!

### 함정 3: 브라우저 캐시 문제
새 빌드가 배포되어도 브라우저가 구 파일을 캐시

**해결책**:
```
Chrome/Edge: Ctrl + Shift + R
Firefox:     Ctrl + Shift + R
Safari:      Cmd + Shift + R
```

---

## Docker 이미지 레이어 캐시 이해

### Frontend Dockerfile 구조
```dockerfile
# Stage 1: Builder (Node.js 환경)
FROM node:20-alpine AS builder
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build  # ← dist/ 생성

# Stage 2: Runtime (Nginx)
FROM nginx:alpine
COPY --from=builder /app/dist /usr/share/nginx/html  # ← dist/ 복사
COPY nginx.conf /etc/nginx/conf.d/default.conf
```

### 캐시 무효화 조건
- `package.json` 변경 → `npm ci` 다시 실행
- 소스 파일 변경 → `npm run build` 다시 실행
- `nginx.conf` 변경 → runtime stage 다시 빌드

### 강제 캐시 무효화
```bash
# 모든 캐시 무시하고 빌드
docker build --no-cache -t docker-frontend:latest -f frontend/Dockerfile frontend/
```

**주의**: 시간이 오래 걸림 (npm ci 재실행). 일반적으로 불필요.

---

## 문제 해결 체크리스트

Frontend 수정 후 브라우저에 반영되지 않을 때:

- [ ] 1단계: 로컬 빌드 확인
  ```bash
  cd frontend && npm run build
  ls -lh dist/assets/index-*.js  # 새 파일명 확인
  ```

- [ ] 2단계: 컨테이너 내 파일 확인
  ```bash
  ./scripts/verify-frontend-build.sh
  ```

- [ ] 3단계: 불일치 시 재빌드
  ```bash
  ./scripts/frontend-rebuild.sh
  ```

- [ ] 4단계: 브라우저 강제 새로고침
  ```
  Ctrl + Shift + R
  ```

- [ ] 5단계: 브라우저 개발자 도구로 로드된 파일 확인
  ```
  F12 → Network → JS 파일 확인
  ```

---

## 추가 도구

### 로그 확인
```bash
# Frontend 컨테이너 로그
docker compose -f docker/docker-compose.yaml logs frontend

# Nginx 접근 로그 (실시간)
docker compose -f docker/docker-compose.yaml logs -f frontend | grep GET
```

### 이미지 히스토리 확인
```bash
# 이미지가 언제 생성되었는지 확인
docker images docker-frontend:latest

# 이미지 레이어 히스토리
docker history docker-frontend:latest
```

### 컨테이너 내부 접속
```bash
# 디버깅을 위해 컨테이너 내부 접속
docker compose -f docker/docker-compose.yaml exec frontend sh

# 내부에서 파일 직접 확인
ls -la /usr/share/nginx/html/assets/
cat /etc/nginx/conf.d/default.conf
```

---

## 베스트 프랙티스

### 개발 워크플로우
```bash
# 1. Frontend 코드 수정
vim frontend/src/pages/FileUpload.tsx

# 2. 자동 빌드 및 배포
./scripts/frontend-rebuild.sh

# 3. 브라우저 강제 새로고침
# Ctrl + Shift + R

# 4. 테스트
# 브라우저에서 기능 확인

# 5. 검증 (선택사항)
./scripts/verify-frontend-build.sh
```

### Git 커밋 전 체크리스트
- [ ] 로컬 빌드 성공: `cd frontend && npm run build`
- [ ] TypeScript 오류 없음: `npm run build`
- [ ] 컨테이너 빌드 성공: `./scripts/frontend-rebuild.sh`
- [ ] 브라우저 테스트 완료
- [ ] 개발자 콘솔 오류 없음

---

## 참고 자료

- [Docker Multi-stage Builds](https://docs.docker.com/build/building/multi-stage/)
- [Docker Compose Build](https://docs.docker.com/compose/compose-file/build/)
- [Vite Build Options](https://vitejs.dev/guide/build.html)

---

## 변경 이력

| 날짜 | 변경 내용 |
|------|----------|
| 2026-01-14 | 최초 작성: 빌드 문제 분석 및 자동화 스크립트 추가 |

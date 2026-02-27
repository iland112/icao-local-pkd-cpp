# RHEL 9 서버 설정 — 10.0.0.163

**문서 작성일**: 2026-02-27
**작업자**: kbjung

---

## 1. 서버 하드웨어 정보

| 항목 | 값 |
|------|-----|
| 하드웨어 | Dell PowerEdge R440 |
| Firmware | 1.6.11 |
| CPU | Intel Xeon Silver 4110 @ 2.10GHz |
| Socket / Core / Thread | 1 소켓 / 8 코어 / 16 스레드 (HT) |
| 메모리 | 16 GB (사용 2.1GB / 가용 12GB) |
| 스왑 | 7.7 GB |
| 디스크 | 558.4 GB (sda) |
| 아키텍처 | x86_64 |

### 디스크 파티션 (LVM)

| 파티션 | 크기 | 마운트 |
|--------|------|--------|
| sda1 | 1 GB | /boot |
| rhel_friendlyelec-root | 70 GB | / (사용 5.4GB, 여유 65GB) |
| rhel_friendlyelec-swap | 7.7 GB | [SWAP] |
| rhel_friendlyelec-home | 479.7 GB | /home |

---

## 2. OS 정보

| 항목 | 값 |
|------|-----|
| OS | Red Hat Enterprise Linux 9.7 (Plow) |
| 커널 | 5.14.0-611.5.1.el9_7.x86_64 |
| Machine ID | 4604d25bd0f04c788af3815ec535cada |
| SELinux | enforcing (targeted) |
| Firewall | running (firewalld) |

---

## 3. 네트워크

| 항목 | 값 |
|------|-----|
| 호스트명 | pkd.smartcoreinc.com |
| IP 주소 | 10.0.0.163/24 |
| 인터페이스 | eno1 (dynamic, noprefixroute) |
| 게이트웨이 | 10.0.0.255 (broadcast) |

### /etc/hosts

```
127.0.0.1   pkd.smartcoreinc.com pkd localhost localhost.localdomain localhost4 localhost4.localdomain4
::1         localhost localhost.localdomain localhost6 localhost6.localdomain6
```

---

## 4. 접속 정보

| 항목 | 값 |
|------|-----|
| SSH | `ssh scpkd@10.0.0.163` |
| 계정 | scpkd |
| 비밀번호 | core |
| sudo | 가능 (비밀번호: core) |

---

## 5. Red Hat 서버 등록

### 5.1 등록 전 상태

- 호스트명: `FriendlyELEC.smartcore.kr` (기존)
- Red Hat 구독: 미등록

### 5.2 호스트명 변경

```bash
sudo hostnamectl set-hostname pkd.smartcoreinc.com
```

- 변경 전: `FriendlyELEC.smartcore.kr`
- 변경 후: `pkd.smartcoreinc.com`
- `/etc/hosts` 업데이트: `127.0.0.1 pkd.smartcoreinc.com pkd localhost ...`

### 5.3 Red Hat Subscription Manager 등록

```bash
sudo subscription-manager register --username=kbjung112 --password='****'
```

**등록 결과**:

| 항목 | 값 |
|------|-----|
| 시스템 ID | `541a236f-c759-41c3-a911-527a0655ac22` |
| 시스템 이름 | pkd.smartcoreinc.com |
| 조직 이름 | 20252037 |
| 조직 ID | 20252037 |
| 등록 서버 | subscription.rhsm.redhat.com:443/subscription |
| 컨텐츠 접근 | Simple Content Access (SCA) |

### 5.4 구독 상태

```
+-------------------------------------------+
   시스템 상태 정보
+-------------------------------------------+
전체 상태: 등록됨
컨텐츠 접근 방식이 단순 컨텐츠 접근으로 설정되어 있습니다.
이 호스트는 서브스크립션 상태에 관계없이 컨텐츠에 접근 할 수 있습니다.
```

> **SCA (Simple Content Access)** 모드: 개별 서브스크립션 첨부 불요, `dnf`로 즉시 패키지 설치 가능

### 5.5 등록 확인 명령어

```bash
# 등록 상태 확인
sudo subscription-manager status

# 시스템 ID 확인
sudo subscription-manager identity

# 사용 가능한 리포지토리 확인
sudo subscription-manager repos --list-enabled
```

---

## 6. Docker CE 설치

```bash
sudo dnf config-manager --add-repo https://download.docker.com/linux/rhel/docker-ce.repo
sudo dnf install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo systemctl enable --now docker
sudo usermod -aG docker scpkd
```

| 패키지 | 버전 |
|--------|------|
| Docker CE | 29.2.1 |
| Docker Compose | v5.1.0 |
| Buildx | 0.31.1 |
| containerd | 2.2.1 |

---

## 7. 방화벽 설정

```bash
sudo firewall-cmd --permanent --add-service=http    # 80/tcp
sudo firewall-cmd --permanent --add-service=https   # 443/tcp
sudo firewall-cmd --permanent --add-port=8080/tcp   # API Gateway (internal)
sudo firewall-cmd --reload
```

열린 포트: `http`, `https`, `ssh`, `cockpit`, `8080/tcp`

---

## 8. 프로젝트 배포

```bash
sudo dnf install -y git
cd /home/scpkd
git clone https://github.com/iland112/icao-local-pkd-cpp.git icao-local-pkd
cd icao-local-pkd
git checkout main
```

### .env 파일 (`/home/scpkd/icao-local-pkd/.env`)

```
DB_TYPE=postgres
DB_HOST=postgres
DB_PORT=5432
DB_NAME=localpkd
DB_USER=pkd
DB_PASSWORD=pkd_test_password_123
LDAP_READ_HOSTS=openldap1:389,openldap2:389
LDAP_WRITE_HOST=openldap1
LDAP_WRITE_PORT=389
LDAP_BIND_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
LDAP_BIND_PASSWORD=ldap_test_password_123
LDAP_BASE_DN=dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
POSTGRES_USER=pkd
POSTGRES_PASSWORD=pkd_test_password_123
LDAP_ADMIN_PASSWORD=ldap_test_password_123
LDAP_CONFIG_PASSWORD=config_test_123
JWT_SECRET_KEY=ab19525f...
JWT_ISSUER=icao-pkd
JWT_EXPIRATION_SECONDS=3600
AUTH_ENABLED=true
NGINX_CONF=../nginx/api-gateway-ssl.conf
```

> `.env`는 `docker/` 디렉토리에도 복사 필요 (docker compose working directory 문제)

---

## 9. SSL 인증서

Private CA 기반 자체 서명 인증서 생성:

```bash
cd /home/scpkd/icao-local-pkd
mkdir -p .docker-data/ssl
# CA (RSA 4096, 10년) + Server cert (RSA 2048, 1년)
# SAN: pkd.smartcoreinc.com, localhost, 127.0.0.1, 10.0.0.163
```

| 파일 | 용도 |
|------|------|
| `.docker-data/ssl/ca.key` | CA 개인키 (비공개) |
| `.docker-data/ssl/ca.crt` | CA 인증서 (클라이언트 배포용) |
| `.docker-data/ssl/server.key` | 서버 개인키 |
| `.docker-data/ssl/server.crt` | 서버 인증서 (CA 서명) |

- CA 유효기간: 2026-02-27 ~ 2036-02-25
- 서버 유효기간: 2026-02-27 ~ 2027-02-27
- 갱신: `scripts/ssl/renew-cert.sh`

---

## 10. 시스템 기동

```bash
cd /home/scpkd/icao-local-pkd

# vcpkg-base 빌드 (최초 1회, 20-30분)
sudo docker compose -f docker/docker-compose.yaml --profile build-only build vcpkg-base

# 전체 서비스 빌드
sudo docker compose -f docker/docker-compose.yaml build

# 기동 (PostgreSQL 프로파일 포함)
sudo docker compose -f docker/docker-compose.yaml --profile postgres up -d
```

### 로그 디렉토리 권한 (필수)

```bash
sudo chmod 777 .docker-data/pkd-logs .docker-data/pkd-uploads .docker-data/pa-logs \
    .docker-data/sync-logs .docker-data/monitoring-logs .docker-data/ai-analysis-logs
```

> Drogon 프레임워크가 로그 경로에 쓰기 권한이 없으면 컨테이너가 즉시 종료됨

### 컨테이너 상태 (2026-02-27 기동 완료)

| 컨테이너 | 상태 |
|----------|------|
| icao-local-pkd-api-gateway | healthy |
| icao-local-pkd-management | healthy |
| icao-local-pkd-pa-service | healthy |
| icao-local-pkd-relay | healthy |
| icao-local-pkd-monitoring | healthy |
| icao-local-pkd-ai-analysis | healthy |
| icao-local-pkd-frontend | Up |
| icao-local-pkd-postgres | healthy |
| icao-local-pkd-openldap1 | healthy |
| icao-local-pkd-openldap2 | healthy |
| icao-local-pkd-swagger | healthy |

### 접속 정보

| 서비스 | URL |
|--------|-----|
| Frontend (HTTPS) | https://10.0.0.163/ |
| Frontend (HTTP) | http://10.0.0.163/ |
| API Gateway (HTTPS) | https://10.0.0.163/api |
| API Gateway (HTTP) | http://10.0.0.163:8080/api |
| Swagger UI | http://10.0.0.163:18081 |

---

## 11. 트러블슈팅

### LDAP Invalid credentials (49)
- **원인**: OpenLDAP 데이터가 다른 비밀번호로 초기화됨
- **해결**: `.docker-data/openldap1`, `.docker-data/openldap2` 삭제 후 재시작

### Unable to access log path! → 컨테이너 즉시 종료
- **원인**: `.docker-data/*-logs` 디렉토리 소유자가 root (docker가 생성)
- **해결**: `chmod 777 .docker-data/*-logs`

### stoi / Pydantic int_parsing 에러
- **원인**: `ORACLE_PORT=` 등 빈 환경변수가 int로 변환 실패
- **해결**: v2.23.1에서 수정됨 (C++ 빈 문자열 체크 + Python default 값 반환)

---

## 12. 작업 체크리스트

- [x] 호스트명 변경 (`pkd.smartcoreinc.com`)
- [x] `/etc/hosts` 업데이트
- [x] Red Hat Developer 계정 등록 (SCA 활성)
- [x] Docker CE 29.2.1 설치
- [x] Docker Compose v5.1.0 설치
- [x] 방화벽 포트 설정 (80, 443, 8080)
- [x] 프로젝트 소스 배포 (git clone main)
- [x] SSL 인증서 생성 (Private CA)
- [x] 전체 서비스 빌드 및 기동 (11 컨테이너)
- [x] API health check 확인
- [ ] 데이터 업로드 (LDIF/Master List)
- [ ] DNS 설정 (pkd.smartcoreinc.com → 10.0.0.163)

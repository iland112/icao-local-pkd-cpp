# RHEL 9 서버 설정 — 10.0.0.220 (static)

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
| IP 주소 | 10.0.0.220/24 (static) |
| 인터페이스 | eno1 |
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
| SSH | `ssh scpkd@10.0.0.220` |
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

## 6. 컨테이너 런타임: Docker → Podman 마이그레이션

### 6.1 초기 Docker CE 설치 (2026-02-27, 제거됨)

```bash
# 아래는 초기 설치 기록 — 현재 제거됨
sudo dnf config-manager --add-repo https://download.docker.com/linux/rhel/docker-ce.repo
sudo dnf install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

### 6.2 Podman 마이그레이션 (2026-02-27)

Docker CE를 제거하고 Podman (RHEL 9 기본 포함)으로 전환:

```bash
# Docker 이미지를 Podman으로 복사 (skopeo)
for img in docker-pkd-management docker-pa-service docker-pkd-relay \
           docker-frontend docker-monitoring-service docker-ai-analysis; do
    sudo skopeo copy docker-daemon:$img:latest \
         containers-storage:docker.io/library/$img:latest
done

# Docker CE 제거
sudo systemctl stop docker
sudo dnf remove -y docker-ce docker-ce-cli containerd.io \
    docker-buildx-plugin docker-compose-plugin
sudo rm -rf /var/lib/docker /var/lib/containerd

# podman-compose (pip 설치)
pip3 install --user podman-compose  # → ~/.local/bin/

# CNI DNS 플러그인 (컨테이너 간 호스트명 해석 필수)
sudo dnf install -y podman-plugins

# Rootless 모드에서 80/443 포트 바인딩 허용
echo 'net.ipv4.ip_unprivileged_port_start=80' | sudo tee -a /etc/sysctl.conf
sudo sysctl -w net.ipv4.ip_unprivileged_port_start=80
```

| 패키지 | 버전 | 비고 |
|--------|------|------|
| Podman | 5.6.0 | RHEL 9 기본 포함 |
| podman-compose | 1.5.0 | pip3 설치 |
| podman-plugins | 5.6.0-14 | dnsname CNI 플러그인 |
| aardvark-dns | 1.16.0 | 설치됨 (미사용, CNI 백엔드) |
| netavark | 1.16.0 | 설치됨 (미사용, CNI 백엔드) |

> **Network backend**: CNI (netavark 아님). `podman-plugins`의 dnsname 플러그인이 DNS 해석 담당

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
DB_TYPE=oracle
ORACLE_USER=pkd_user
ORACLE_PASSWORD=pkd_password
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
# SAN: pkd.smartcoreinc.com, localhost, 127.0.0.1, 10.0.0.220
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

## 10. 시스템 기동 (Podman)

```bash
cd /home/scpkd/icao-local-pkd

# 최초 설치: 완전 초기화 (Oracle + LDAP DIT + 서비스)
./podman-clean-and-init.sh

# 일반 시작
./podman-start.sh

# 헬스 체크
./podman-health.sh
```

### SELinux 볼륨 라벨링 (필수)

```bash
# Rootless Podman + SELinux Enforcing 모드:
# `:Z`/`:z` 볼륨 라벨 사용 불가 (CAP_MAC_ADMIN 없음)
# 대신 2단계 사전 라벨링:
chcon -Rt container_file_t .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
chcon -R -l s0 .docker-data/ docker/db-oracle/init data/cert nginx/ docs/openapi
# → clean-and-init.sh, start.sh가 자동으로 수행
```

### 컨테이너 상태 (2026-02-27 Podman 마이그레이션 완료)

| 컨테이너 | 상태 | 런타임 |
|----------|------|--------|
| icao-local-pkd-api-gateway | healthy | Podman |
| icao-local-pkd-management | healthy | Podman |
| icao-local-pkd-pa-service | healthy | Podman |
| icao-local-pkd-relay | healthy | Podman |
| icao-local-pkd-monitoring | healthy | Podman |
| icao-local-pkd-ai-analysis | healthy | Podman |
| icao-local-pkd-frontend | Up | Podman |
| icao-local-pkd-oracle | healthy | Podman |
| icao-local-pkd-openldap1 | healthy | Podman |
| icao-local-pkd-openldap2 | healthy | Podman |
| icao-local-pkd-swagger | healthy | Podman |

### 접속 정보

| 서비스 | URL |
|--------|-----|
| Frontend (HTTPS) | https://pkd.smartcoreinc.com |
| Frontend (HTTP) | http://pkd.smartcoreinc.com |
| API Gateway (HTTPS) | https://pkd.smartcoreinc.com/api |
| API Gateway (HTTP) | http://pkd.smartcoreinc.com/api |
| API Gateway (내부) | http://localhost:18080/api |
| Swagger UI | http://localhost:18090 |

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

## 12. 클라이언트 PC HTTPS 접속 설정

Private CA 기반 인증서이므로 클라이언트 PC에서 CA 인증서를 신뢰 등록해야 합니다.

### 자동 설정 스크립트

`scripts/client/` 폴더의 두 파일을 클라이언트 PC에 복사 후 실행:

| 파일 | 용도 |
|------|------|
| `setup-pkd-access.bat` | 더블클릭 실행 (관리자 권한 자동 요청) |
| `setup-pkd-access.ps1` | 실제 설정 스크립트 |

**사용법**: 두 파일을 같은 폴더에 복사 → `setup-pkd-access.bat` 더블클릭 → UAC "예"

**스크립트 동작**:
1. `hosts` 파일에 `10.0.0.220 pkd.smartcoreinc.com` 추가
2. Private CA 인증서를 Windows 신뢰할 수 있는 루트 인증 기관에 등록
3. DNS 캐시 초기화
4. `https://pkd.smartcoreinc.com/health` 접속 테스트

> **참고**: Firefox는 Windows 인증서 저장소를 사용하지 않으므로 별도로 인증서 가져오기 필요
> (설정 → 개인정보 및 보안 → 인증서 → 인증서 보기 → 인증 기관 → 가져오기)

### 수동 설정

1. `hosts` 파일 (`C:\Windows\System32\drivers\etc\hosts`):
   ```
   10.0.0.220    pkd.smartcoreinc.com
   ```

2. CA 인증서 등록: `.docker-data/ssl/ca.crt` 파일을 더블클릭 → "인증서 설치" → "로컬 컴퓨터" → "신뢰할 수 있는 루트 인증 기관"

---

## 13. 작업 체크리스트

- [x] 호스트명 변경 (`pkd.smartcoreinc.com`)
- [x] `/etc/hosts` 업데이트
- [x] Red Hat Developer 계정 등록 (SCA 활성)
- [x] Docker CE 29.2.1 설치 → **Podman 5.6.0으로 마이그레이션 완료**
- [x] Docker CE 제거, 이미지 skopeo 마이그레이션
- [x] podman-compose 1.5.0 설치 (pip)
- [x] podman-plugins 설치 (CNI dnsname DNS)
- [x] sysctl 특권 포트 허용 (80/443)
- [x] 방화벽 포트 설정 (80, 443, 8080)
- [x] 프로젝트 소스 배포 (git clone main)
- [x] SSL 인증서 생성 (Private CA, SAN: 10.0.0.220)
- [x] 전체 서비스 기동 (11 컨테이너, Podman rootless)
- [x] SELinux MCS 라벨링 해결
- [x] API health check 확인 (HTTP + HTTPS)
- [x] 클라이언트 HTTPS 접속 설정 스크립트 (`scripts/client/`)
- [ ] 데이터 업로드 (LDIF/Master List)
- [ ] DNS 설정 (pkd.smartcoreinc.com → 10.0.0.220)

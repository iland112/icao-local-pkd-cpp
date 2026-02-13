# Repository Pattern 완성 작업 계획

**현재 상태**: Phase 2 완료 (Docker 빌드 성공)
**날짜**: 2026-01-29
**버전**: v2.1.3

---

## 현재 구현 상태 요약

### ✅ 완료된 작업 (Phase 1-2)

| 컴포넌트 | 상태 | 구현률 | 설명 |
|---------|------|--------|------|
| **Repository 계층** | ✅ 완료 | 100% | 5개 Repository 모두 구현 완료 |
| **Service 초기화** | ✅ 완료 | 100% | main.cpp에서 DI 완료 |
| **Docker 빌드** | ✅ 완료 | 100% | 컴파일 에러 없음 |
| **UploadService** | ⚠️ 부분 | 60% | 핵심 메서드만 구현 |
| **ValidationService** | ⚠️ 부분 | 10% | 1개 메서드만 구현 |
| **AuditService** | ⚠️ 부분 | 30% | 기록 메서드만 구현 |
| **StatisticsService** | ❌ 미완 | 0% | 모두 TODO |
| **API 라우트 연결** | ❌ 미완 | 0% | Service 미사용 |

### 🔴 핵심 문제

**현재 상황**:
- Service와 Repository는 생성되고 초기화됨
- **하지만 API 엔드포인트들이 아직 Service를 사용하지 않음**
- main.cpp의 registerRoutes()에서 여전히 직접 SQL 실행

**예시**:
```cpp
// 현재 (Phase 2): Service는 있지만 사용 안함
app.registerHandler("/api/upload/ldif", [](req, callback) {
    // 직접 SQL 실행 (기존 코드)
    PGresult* res = PQexec(conn, "INSERT INTO ...");
    // ...
});

// 목표 (Phase 3): Service 사용
app.registerHandler("/api/upload/ldif", [](req, callback) {
    // Service 메서드 호출
    auto result = uploadService->uploadLdif(fileName, content, mode, user);
    // ...
});
```

---

## Phase 3: API 라우트 → Service 연결 (필수)

### 우선순위: 🔥 HIGH (즉시 필요)

이 단계를 완료해야 Repository 패턴이 실제로 작동합니다!

### 작업 목록

#### 3.1 Upload 엔드포인트 연결

| 엔드포인트 | 현재 | 변경 후 | 우선순위 |
|-----------|------|--------|---------|
| `POST /api/upload/ldif` | 직접 SQL | `uploadService->uploadLdif()` | 🔥 HIGH |
| `POST /api/upload/masterlist` | 직접 SQL | `uploadService->uploadMasterList()` | 🔥 HIGH |
| `GET /api/upload/history` | 직접 SQL | `uploadService->getUploadHistory()` | 🔥 HIGH |
| `GET /api/upload/detail/:id` | 직접 SQL | `uploadService->getUploadDetail()` | 🔥 HIGH |
| `DELETE /api/upload/:id` | 직접 SQL | `uploadService->deleteUpload()` | 🔥 HIGH |
| `GET /api/upload/statistics` | 직접 SQL | `uploadService->getUploadStatistics()` | 🔥 HIGH |

**예상 작업량**: 6개 엔드포인트, 약 300-400줄 수정

#### 3.2 Validation 엔드포인트 연결

| 엔드포인트 | 현재 | 변경 후 | 우선순위 |
|-----------|------|--------|---------|
| `GET /api/certificates/validation` | 직접 SQL | `validationService->getValidationByFingerprint()` | MEDIUM |
| `POST /api/validation/revalidate` | 직접 SQL | `validationService->revalidateDscCertificates()` | LOW (TODO) |

**예상 작업량**: 2개 엔드포인트, 약 100줄 수정

#### 3.3 Audit 엔드포인트 연결

| 엔드포인트 | 현재 | 변경 후 | 우선순위 |
|-----------|------|--------|---------|
| `GET /api/audit/operations` | 직접 SQL | `auditService->getOperationLogs()` | LOW (TODO) |
| `GET /api/audit/operations/stats` | 직접 SQL | `auditService->getOperationStatistics()` | LOW (TODO) |

**예상 작업량**: 2개 엔드포인트, 약 100줄 수정

#### 3.4 Statistics 엔드포인트 연결

| 엔드포인트 | 현재 | 변경 후 | 우선순위 |
|-----------|------|--------|---------|
| `GET /api/upload/countries` | 직접 SQL | `statisticsService->getCountryStatistics()` | LOW (TODO) |
| `GET /api/upload/countries/detailed` | 직접 SQL | `statisticsService->getDetailedCountryStatistics()` | LOW (TODO) |

**예상 작업량**: 2개 엔드포인트, 약 100줄 수정

### Phase 3 작업 전략

**단계별 접근**:

1. **Step 1**: UploadService 엔드포인트만 먼저 연결
   - 가장 많이 사용되는 핵심 기능
   - 이미 60% 구현되어 있음
   - 테스트 후 다음 단계로

2. **Step 2**: ValidationService 엔드포인트 연결
   - 1개만 구현되어 있음 (`getValidationByFingerprint`)
   - 나머지는 Phase 4에서 구현

3. **Step 3**: Audit/Statistics 엔드포인트 연결
   - 대부분 TODO 상태
   - Phase 4에서 구현 후 연결

**예상 소요 시간**: 2-4시간

---

## Phase 4: Service 메서드 완전 구현 (선택)

### 우선순위: MEDIUM-LOW (시간 있을 때)

현재 TODO로 남아있는 Service 메서드들을 구현합니다.

### 4.1 UploadService 나머지 메서드

```cpp
// 미구현 메서드 (우선순위 순)
1. getUploadValidations()      // HIGH - 프론트엔드에서 사용
2. getUploadIssues()            // MEDIUM - 중복 추적
3. triggerParsing()             // LOW - 수동 파싱
4. triggerValidation()          // LOW - 수동 검증
```

**작업 필요**:
- main.cpp에서 기존 로직 추출
- Repository 패턴 적용
- 에러 처리 추가

**예상 작업량**: 4개 메서드, 약 400-500줄

### 4.2 ValidationService 핵심 메서드

```cpp
// 미구현 메서드 (우선순위 순)
1. revalidateDscCertificates()          // HIGH - 대량 검증
2. validateCertificate()                 // HIGH - 단일 검증
3. buildTrustChain()                     // HIGH - 신뢰 체인
4. findCscaByIssuerDn()                  // HIGH - CSCA 조회
5. validateLinkCertificate()             // MEDIUM
6. getValidationStatistics()             // MEDIUM
```

**작업 필요**:
- main.cpp의 복잡한 검증 로직 추출
- OpenSSL 사용하는 암호화 검증 로직
- CRL 체크 로직
- 신뢰 체인 빌더 로직

**예상 작업량**: 6개 메서드, 약 1,000-1,500줄 (가장 복잡)

### 4.3 AuditService 나머지 메서드

```cpp
// 미구현 메서드 (우선순위 순)
1. getOperationLogs()            // HIGH - 로그 조회
2. getOperationStatistics()      // MEDIUM - 통계
3. getOperationLogById()         // LOW
4. getUserActivity()             // LOW
5. deleteOldAuditLogs()          // LOW - 정리
```

**작업 필요**:
- Repository에 filter 지원 추가
- 집계 쿼리 구현
- 페이지네이션 처리

**예상 작업량**: 5개 메서드, 약 400-500줄

### 4.4 StatisticsService 모든 메서드

```cpp
// 미구현 메서드 (전부!)
1. getUploadStatistics()           // HIGH
2. getCertificateStatistics()      // HIGH
3. getCountryStatistics()          // HIGH
4. getDetailedCountryStatistics()  // MEDIUM
5. getValidationStatistics()       // MEDIUM
6. getSystemStatistics()           // LOW
```

**작업 필요**:
- StatisticsRepository에 쿼리 구현
- 복잡한 집계 로직
- 캐싱 전략 (선택)

**예상 작업량**: 6개 메서드, 약 600-800줄

---

## 전략적 선택: 무엇을 먼저 할 것인가?

### 옵션 A: 최소 작동 버전 (추천 ⭐)

**목표**: Repository 패턴이 실제로 작동하는 것을 증명

**작업**:
1. Phase 3.1만 수행 (UploadService 엔드포인트 연결)
2. 나머지는 기존 코드 그대로 사용

**장점**:
- ✅ 빠르게 결과 확인 가능 (2-3시간)
- ✅ Repository 패턴 작동 검증
- ✅ 점진적 마이그레이션 가능

**단점**:
- ⚠️ 일부 엔드포인트는 여전히 직접 SQL 사용

### 옵션 B: 완전 마이그레이션

**목표**: 모든 SQL을 Repository로 이동

**작업**:
1. Phase 3 전체 수행 (모든 엔드포인트 연결)
2. Phase 4 전체 수행 (모든 메서드 구현)

**장점**:
- ✅ 100% Repository 패턴
- ✅ main.cpp에서 SQL 완전 제거

**단점**:
- ⚠️ 작업량 많음 (2,000-3,000줄, 10-15시간)
- ⚠️ 테스트 시간 많이 필요

### 옵션 C: 하이브리드 (균형 ⚖️)

**목표**: 핵심 기능만 Repository 패턴 적용

**작업**:
1. Phase 3.1 완료 (Upload 엔드포인트)
2. Phase 3.2 완료 (Validation 엔드포인트 중 구현된 것만)
3. Phase 4.1 부분 (UploadService 핵심 메서드만)

**장점**:
- ✅ 핵심 기능은 Repository 패턴
- ✅ 합리적인 작업량 (4-6시간)
- ✅ 나머지는 점진적 마이그레이션

**단점**:
- ⚠️ 일부 TODO 남음

---

## 권장 로드맵

### 1단계: Phase 3.1 완료 (즉시, 2-3시간)

**작업**: UploadService 6개 엔드포인트 연결

```bash
# 작업 순서
1. POST /api/upload/ldif
2. POST /api/upload/masterlist
3. GET /api/upload/history
4. GET /api/upload/detail/:id
5. DELETE /api/upload/:id
6. GET /api/upload/statistics
```

**검증**:
- Docker 재빌드
- 각 엔드포인트 테스트
- 로그 확인

### 2단계: 통합 테스트 (즉시 후, 1-2시간)

**테스트 항목**:
- [ ] LDIF 업로드 정상 작동
- [ ] Master List 업로드 정상 작동
- [ ] 업로드 히스토리 조회
- [ ] 업로드 상세 조회
- [ ] 업로드 삭제
- [ ] 통계 조회

### 3단계: Phase 4.1 부분 (선택, 3-4시간)

**작업**: UploadService 핵심 메서드 구현

```cpp
1. getUploadValidations()  // 프론트엔드 필요
2. getUploadIssues()        // 중복 추적 필요
```

### 4단계: 나머지 점진적 구현 (장기)

**우선순위**:
1. ValidationService 핵심 (revalidate, buildTrustChain)
2. AuditService 조회 메서드
3. StatisticsService 통계 메서드

---

## 성공 기준

### Phase 3 성공 기준

- [ ] 최소 6개 Upload 엔드포인트가 Service 사용
- [ ] 기존 기능 모두 정상 작동
- [ ] 성능 저하 없음
- [ ] 로그에서 Service 메서드 호출 확인

### 전체 완료 기준

- [ ] main.cpp에서 PQexec 호출 0개
- [ ] 모든 SQL이 Repository에 있음
- [ ] 모든 API 엔드포인트가 Service 사용
- [ ] 모든 테스트 통과

---

## 위험 관리

### 리스크

1. **기존 기능 손상**
   - 완화: 점진적 마이그레이션
   - 대응: Git 커밋 자주, 롤백 가능하게

2. **성능 저하**
   - 완화: Service 레이어 오버헤드 최소화
   - 대응: 프로파일링, 최적화

3. **복잡성 증가**
   - 완화: 단계별 검증
   - 대응: 문서화, 테스트

### 롤백 전략

```bash
# 각 단계마다 Git 태그
git tag phase-3.1-upload-endpoints
git tag phase-3.2-validation-endpoints
git tag phase-4.1-upload-methods

# 문제 발생 시 롤백
git checkout phase-3.1-upload-endpoints
```

---

## 다음 행동

**즉시 시작** (옵션 A 추천):

1. `POST /api/upload/ldif` 엔드포인트부터 시작
2. uploadService->uploadLdif() 연결
3. 테스트
4. 다음 엔드포인트로 진행

**명령어**:
```bash
# main.cpp 수정
vim services/pkd-management/src/main.cpp

# 빌드
cd docker && docker-compose build pkd-management

# 테스트
docker-compose up -d
curl -X POST http://localhost:8080/api/upload/ldif ...
```

---

## 요약

**현재 위치**: Phase 2 완료 ✅
- Repository 패턴 구조 완성
- Docker 빌드 성공
- 하지만 API가 아직 Service를 사용하지 않음

**다음 단계**: Phase 3 시작 🚀
- API 엔드포인트를 Service와 연결
- UploadService 6개 엔드포인트부터 시작
- 점진적으로 나머지 구현

**최종 목표**:
- main.cpp에서 SQL 완전 제거
- 100% Repository 패턴
- Oracle 마이그레이션 준비 완료

---

**Document Version**: 1.0
**Last Updated**: 2026-01-29
**Status**: Phase 3 대기 중

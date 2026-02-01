# Duplicate Certificate UI/UX Design

**Document Version**: 1.0.0
**Date**: 2026-01-25
**Status**: Design Proposal

---

## 1. Problem Statement

### 1.1 Current Situation

Master List 업로드 시 동일한 Subject DN + Serial Number를 가진 인증서가 여러 개 존재할 수 있습니다:

- **Database**: 모든 인증서를 개별 레코드로 저장 (fingerprint로 구분)
- **LDAP**: DN 중복 시 마지막 인증서로 덮어쓰기
- **사용자**: 중복 발생 사실을 알 수 없음

### 1.2 User Pain Points

1. **투명성 부족**: 536개 업로드 → 531개 LDAP 저장, 5개 차이의 원인을 모름
2. **데이터 무결성 우려**: LDAP에서 일부 인증서가 "사라진" 것처럼 보임
3. **디버깅 어려움**: 중복 발생 시 어떤 인증서가 최종 저장되었는지 불명확

---

## 2. Design Goals

### 2.1 Transparency

- 중복 인증서 발생 사실을 명확히 표시
- 어떤 인증서가 최종적으로 LDAP에 저장되었는지 표시

### 2.2 Traceability

- 중복 인증서의 fingerprint, validity period, 기타 차이점 표시
- LDAP 덮어쓰기 이력 추적

### 2.3 User Control

- 중복 인증서 중 어떤 것을 우선할지 사용자가 선택 가능 (선택적)
- 중복 경고를 무시하거나 확인할 수 있는 옵션

---

## 3. Database Schema Extension

### 3.1 New Table: certificate_duplicates

중복 인증서 이력을 추적하기 위한 테이블:

```sql
CREATE TABLE certificate_duplicates (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    upload_id UUID NOT NULL REFERENCES uploaded_file(id),

    -- DN 정보
    subject_dn TEXT NOT NULL,
    serial_number VARCHAR(100) NOT NULL,
    certificate_type VARCHAR(20) NOT NULL,
    country_code VARCHAR(3) NOT NULL,

    -- 중복 인증서 정보
    primary_certificate_id UUID NOT NULL REFERENCES certificate(id),
    duplicate_certificate_id UUID NOT NULL REFERENCES certificate(id),

    -- 차이점
    primary_fingerprint VARCHAR(64) NOT NULL,
    duplicate_fingerprint VARCHAR(64) NOT NULL,
    primary_not_before TIMESTAMP WITH TIME ZONE,
    primary_not_after TIMESTAMP WITH TIME ZONE,
    duplicate_not_before TIMESTAMP WITH TIME ZONE,
    duplicate_not_after TIMESTAMP WITH TIME ZONE,

    -- LDAP 저장 상태
    ldap_stored_certificate_id UUID REFERENCES certificate(id),
    ldap_action VARCHAR(20), -- 'REPLACED', 'SKIPPED', 'FAILED'
    ldap_dn TEXT,

    -- 타임스탬프
    detected_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    UNIQUE(upload_id, subject_dn, serial_number, duplicate_certificate_id)
);

CREATE INDEX idx_cert_dup_upload_id ON certificate_duplicates(upload_id);
CREATE INDEX idx_cert_dup_primary_cert ON certificate_duplicates(primary_certificate_id);
CREATE INDEX idx_cert_dup_duplicate_cert ON certificate_duplicates(duplicate_certificate_id);
```

### 3.2 Backend Detection Logic

```cpp
// In saveCertificateToLdap() function
std::string saveCertificateToLdap(...) {
    // ... existing code ...

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // LOG: Duplicate DN detected
        spdlog::warn("[DUPLICATE] DN already exists in LDAP: {}", dn);
        spdlog::warn("[DUPLICATE] Subject DN: {}, Serial: {}", subjectDn, serialNumber);
        spdlog::warn("[DUPLICATE] Previous certificate will be replaced");
        spdlog::warn("[DUPLICATE] New certificate fingerprint: {}", fingerprint);

        // Try to update the certificate
        LDAPMod modCertReplace;
        modCertReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modCertReplace.mod_type = const_cast<char*>("userCertificate;binary");
        modCertReplace.mod_bvalues = certBvVals;

        LDAPMod* replaceMods[] = {&modCertReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);

        if (rc == LDAP_SUCCESS) {
            spdlog::info("[DUPLICATE] Certificate replaced successfully in LDAP");

            // Record duplicate in database
            recordDuplicateCertificate(conn, uploadId, subjectDn, serialNumber,
                                       certType, countryCode, fingerprint,
                                       dn, "REPLACED");
        }
    }

    // ... existing code ...
}

void recordDuplicateCertificate(
    PGconn* conn,
    const std::string& uploadId,
    const std::string& subjectDn,
    const std::string& serialNumber,
    const std::string& certType,
    const std::string& countryCode,
    const std::string& newFingerprint,
    const std::string& ldapDn,
    const std::string& action
) {
    // Find primary certificate (first one with same DN+Serial)
    const char* findPrimaryQuery =
        "SELECT id, fingerprint_sha256, not_before, not_after "
        "FROM certificate "
        "WHERE upload_id = $1 AND subject_dn = $2 AND serial_number = $3 "
        "ORDER BY created_at ASC LIMIT 1";

    const char* findPrimaryParams[3] = {
        uploadId.c_str(),
        subjectDn.c_str(),
        serialNumber.c_str()
    };

    PGresult* primaryRes = PQexecParams(conn, findPrimaryQuery, 3, nullptr,
                                        findPrimaryParams, nullptr, nullptr, 0);

    if (PQresultStatus(primaryRes) != PGRES_TUPLES_OK || PQntuples(primaryRes) == 0) {
        PQclear(primaryRes);
        return;
    }

    std::string primaryCertId = PQgetvalue(primaryRes, 0, 0);
    std::string primaryFingerprint = PQgetvalue(primaryRes, 0, 1);
    std::string primaryNotBefore = PQgetvalue(primaryRes, 0, 2);
    std::string primaryNotAfter = PQgetvalue(primaryRes, 0, 3);
    PQclear(primaryRes);

    // Find duplicate certificate (current one being saved)
    const char* findDuplicateQuery =
        "SELECT id, not_before, not_after "
        "FROM certificate "
        "WHERE upload_id = $1 AND fingerprint_sha256 = $2";

    const char* findDuplicateParams[2] = {
        uploadId.c_str(),
        newFingerprint.c_str()
    };

    PGresult* duplicateRes = PQexecParams(conn, findDuplicateQuery, 2, nullptr,
                                          findDuplicateParams, nullptr, nullptr, 0);

    if (PQresultStatus(duplicateRes) != PGRES_TUPLES_OK || PQntuples(duplicateRes) == 0) {
        PQclear(duplicateRes);
        return;
    }

    std::string duplicateCertId = PQgetvalue(duplicateRes, 0, 0);
    std::string duplicateNotBefore = PQgetvalue(duplicateRes, 0, 1);
    std::string duplicateNotAfter = PQgetvalue(duplicateRes, 0, 2);
    PQclear(duplicateRes);

    // Insert duplicate record
    const char* insertQuery =
        "INSERT INTO certificate_duplicates ("
        "upload_id, subject_dn, serial_number, certificate_type, country_code, "
        "primary_certificate_id, duplicate_certificate_id, "
        "primary_fingerprint, duplicate_fingerprint, "
        "primary_not_before, primary_not_after, "
        "duplicate_not_before, duplicate_not_after, "
        "ldap_stored_certificate_id, ldap_action, ldap_dn"
        ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)";

    const char* insertParams[16] = {
        uploadId.c_str(),
        subjectDn.c_str(),
        serialNumber.c_str(),
        certType.c_str(),
        countryCode.c_str(),
        primaryCertId.c_str(),
        duplicateCertId.c_str(),
        primaryFingerprint.c_str(),
        newFingerprint.c_str(),
        primaryNotBefore.c_str(),
        primaryNotAfter.c_str(),
        duplicateNotBefore.c_str(),
        duplicateNotAfter.c_str(),
        duplicateCertId.c_str(),  // Last one is stored in LDAP
        action.c_str(),
        ldapDn.c_str()
    };

    PGresult* insertRes = PQexecParams(conn, insertQuery, 16, nullptr,
                                       insertParams, nullptr, nullptr, 0);

    if (PQresultStatus(insertRes) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to record duplicate certificate: {}", PQerrorMessage(conn));
    }

    PQclear(insertRes);
}
```

---

## 4. Backend API Extension

### 4.1 New API Endpoint: GET /api/upload/{uploadId}/duplicates

```cpp
// Get duplicate certificates for an upload
app.registerHandler(
    "/api/upload/:uploadId/duplicates",
    [](const drogon::HttpRequestPtr& req,
       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
       const std::string& uploadId) {
        spdlog::info("GET /api/upload/{}/duplicates", uploadId);

        try {
            // Connect to database
            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            // Query duplicates
            const char* query =
                "SELECT "
                "id, subject_dn, serial_number, certificate_type, country_code, "
                "primary_certificate_id, duplicate_certificate_id, "
                "primary_fingerprint, duplicate_fingerprint, "
                "primary_not_before, primary_not_after, "
                "duplicate_not_before, duplicate_not_after, "
                "ldap_stored_certificate_id, ldap_action, ldap_dn, detected_at "
                "FROM certificate_duplicates "
                "WHERE upload_id = $1 "
                "ORDER BY detected_at DESC";

            const char* paramValues[1] = {uploadId.c_str()};
            PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                         nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Query failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQclear(res);
                PQfinish(conn);
                return;
            }

            Json::Value result;
            result["success"] = true;
            result["uploadId"] = uploadId;
            result["count"] = PQntuples(res);

            Json::Value duplicates(Json::arrayValue);
            for (int i = 0; i < PQntuples(res); i++) {
                Json::Value dup;
                dup["id"] = PQgetvalue(res, i, 0);
                dup["subjectDn"] = PQgetvalue(res, i, 1);
                dup["serialNumber"] = PQgetvalue(res, i, 2);
                dup["certificateType"] = PQgetvalue(res, i, 3);
                dup["countryCode"] = PQgetvalue(res, i, 4);

                Json::Value primary;
                primary["certificateId"] = PQgetvalue(res, i, 5);
                primary["fingerprint"] = PQgetvalue(res, i, 7);
                primary["notBefore"] = PQgetvalue(res, i, 9);
                primary["notAfter"] = PQgetvalue(res, i, 10);
                dup["primary"] = primary;

                Json::Value duplicate;
                duplicate["certificateId"] = PQgetvalue(res, i, 6);
                duplicate["fingerprint"] = PQgetvalue(res, i, 8);
                duplicate["notBefore"] = PQgetvalue(res, i, 11);
                duplicate["notAfter"] = PQgetvalue(res, i, 12);
                dup["duplicate"] = duplicate;

                dup["ldapStoredCertificateId"] = PQgetvalue(res, i, 13);
                dup["ldapAction"] = PQgetvalue(res, i, 14);
                dup["ldapDn"] = PQgetvalue(res, i, 15);
                dup["detectedAt"] = PQgetvalue(res, i, 16);

                duplicates.append(dup);
            }

            result["duplicates"] = duplicates;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);

            PQclear(res);
            PQfinish(conn);

        } catch (const std::exception& e) {
            spdlog::error("Error fetching duplicates: {}", e.what());
            Json::Value error;
            error["success"] = false;
            error["message"] = e.what();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
        }
    },
    {drogon::Get}
);
```

### 4.2 Update uploaded_file Statistics

```sql
-- Add duplicate_count column to uploaded_file table
ALTER TABLE uploaded_file
ADD COLUMN duplicate_count INTEGER DEFAULT 0;
```

```cpp
// Update duplicate count after processing
void updateDuplicateCount(PGconn* conn, const std::string& uploadId) {
    const char* query =
        "UPDATE uploaded_file "
        "SET duplicate_count = ("
        "  SELECT COUNT(*) FROM certificate_duplicates WHERE upload_id = $1"
        ") "
        "WHERE id = $1";

    const char* paramValues[1] = {uploadId.c_str()};
    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    PQclear(res);
}
```

---

## 5. Frontend UI Design

### 5.1 Upload History Page - Duplicate Badge

**Location**: `/upload-history` page, Upload History table

**Design**:

```tsx
// In UploadHistory.tsx
{upload.duplicateCount > 0 && (
  <div className="flex items-center gap-1 text-xs">
    <AlertTriangle className="w-3 h-3 text-amber-500" />
    <span className="text-amber-600 dark:text-amber-400">
      {upload.duplicateCount} duplicate{upload.duplicateCount > 1 ? 's' : ''}
    </span>
  </div>
)}
```

**Visual**:
- Amber warning icon + badge
- Shows duplicate count next to certificate statistics

### 5.2 Upload Detail Page - Duplicates Section

**Location**: `/upload-detail/:id` page, new "Duplicates" section

**Design**:

```tsx
// In UploadDetail.tsx
{upload.duplicateCount > 0 && (
  <div className="bg-white dark:bg-gray-800 rounded-xl shadow-lg p-6">
    <div className="flex items-center justify-between mb-4">
      <h2 className="text-lg font-semibold text-gray-900 dark:text-white flex items-center gap-2">
        <AlertTriangle className="w-5 h-5 text-amber-500" />
        Duplicate Certificates ({upload.duplicateCount})
      </h2>
      <button
        onClick={() => setShowDuplicatesDialog(true)}
        className="text-sm text-blue-600 dark:text-blue-400 hover:underline"
      >
        View Details
      </button>
    </div>

    <div className="p-3 bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800 rounded-lg">
      <div className="flex items-start gap-3">
        <Info className="w-5 h-5 text-amber-600 dark:text-amber-400 flex-shrink-0 mt-0.5" />
        <div className="text-sm text-amber-900 dark:text-amber-100">
          <p className="font-semibold mb-1">Why duplicates occur</p>
          <p>
            ICAO Master List may contain re-issued certificates with identical Subject DN + Serial Number.
            The latest certificate replaces the previous one in LDAP.
          </p>
        </div>
      </div>
    </div>

    {/* Quick summary */}
    <div className="mt-4 grid grid-cols-3 gap-4 text-sm">
      <div>
        <span className="text-gray-600 dark:text-gray-400">Total Certificates:</span>
        <p className="font-medium text-gray-900 dark:text-white">{upload.statistics.totalCount}</p>
      </div>
      <div>
        <span className="text-gray-600 dark:text-gray-400">Unique DN+Serial:</span>
        <p className="font-medium text-gray-900 dark:text-white">
          {upload.statistics.totalCount - upload.duplicateCount}
        </p>
      </div>
      <div>
        <span className="text-gray-600 dark:text-gray-400">LDAP Entries:</span>
        <p className="font-medium text-gray-900 dark:text-white">
          {upload.statistics.totalCount - upload.duplicateCount}
        </p>
      </div>
    </div>
  </div>
)}
```

### 5.3 Duplicates Dialog

**Component**: New dialog showing duplicate certificate details

**Design**:

```tsx
// DuplicatesDialog.tsx
interface Duplicate {
  id: string;
  subjectDn: string;
  serialNumber: string;
  certificateType: string;
  countryCode: string;
  primary: {
    certificateId: string;
    fingerprint: string;
    notBefore: string;
    notAfter: string;
  };
  duplicate: {
    certificateId: string;
    fingerprint: string;
    notBefore: string;
    notAfter: string;
  };
  ldapStoredCertificateId: string;
  ldapAction: string;
  detectedAt: string;
}

export function DuplicatesDialog({
  uploadId,
  open,
  onClose
}: {
  uploadId: string;
  open: boolean;
  onClose: () => void;
}) {
  const [duplicates, setDuplicates] = useState<Duplicate[]>([]);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (open && uploadId) {
      fetchDuplicates();
    }
  }, [open, uploadId]);

  const fetchDuplicates = async () => {
    setLoading(true);
    try {
      const response = await fetch(`/api/upload/${uploadId}/duplicates`);
      const data = await response.json();
      if (data.success) {
        setDuplicates(data.duplicates);
      }
    } catch (err) {
      console.error('Failed to fetch duplicates:', err);
    } finally {
      setLoading(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={onClose}>
      <DialogContent className="max-w-5xl max-h-[80vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <AlertTriangle className="w-5 h-5 text-amber-500" />
            Duplicate Certificates ({duplicates.length})
          </DialogTitle>
          <DialogDescription>
            Certificates with identical Subject DN + Serial Number detected during upload.
            The latest certificate replaces the previous one in LDAP.
          </DialogDescription>
        </DialogHeader>

        {loading ? (
          <div className="flex justify-center items-center h-64">
            <Loader2 className="w-8 h-8 animate-spin text-blue-500" />
          </div>
        ) : (
          <div className="space-y-4">
            {duplicates.map((dup) => (
              <div
                key={dup.id}
                className="p-4 border border-amber-200 dark:border-amber-800 rounded-lg bg-amber-50 dark:bg-amber-900/10"
              >
                {/* Header */}
                <div className="flex items-start justify-between mb-3">
                  <div>
                    <div className="flex items-center gap-2 mb-1">
                      <span className="px-2 py-0.5 text-xs font-medium rounded bg-blue-100 dark:bg-blue-900 text-blue-800 dark:text-blue-200">
                        {dup.certificateType}
                      </span>
                      <span className="px-2 py-0.5 text-xs font-medium rounded bg-gray-100 dark:bg-gray-700">
                        {dup.countryCode}
                      </span>
                      <span className="px-2 py-0.5 text-xs font-medium rounded bg-amber-100 dark:bg-amber-900 text-amber-800 dark:text-amber-200">
                        {dup.ldapAction}
                      </span>
                    </div>
                    <p className="text-sm font-mono text-gray-700 dark:text-gray-300">
                      Serial: {dup.serialNumber}
                    </p>
                  </div>
                  <span className="text-xs text-gray-500 dark:text-gray-400">
                    {new Date(dup.detectedAt).toLocaleString()}
                  </span>
                </div>

                {/* Subject DN */}
                <div className="mb-3">
                  <p className="text-xs text-gray-600 dark:text-gray-400 mb-1">Subject DN:</p>
                  <p className="text-sm font-mono text-gray-900 dark:text-white break-all">
                    {dup.subjectDn}
                  </p>
                </div>

                {/* Comparison Table */}
                <div className="grid grid-cols-2 gap-4">
                  {/* Primary Certificate */}
                  <div className="p-3 bg-white dark:bg-gray-800 rounded border border-gray-200 dark:border-gray-700">
                    <div className="flex items-center justify-between mb-2">
                      <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">
                        First Certificate
                      </h4>
                      {dup.ldapStoredCertificateId !== dup.primary.certificateId && (
                        <span className="px-2 py-0.5 text-xs rounded bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-300">
                          Replaced
                        </span>
                      )}
                    </div>
                    <div className="space-y-2 text-xs">
                      <div>
                        <span className="text-gray-600 dark:text-gray-400">Fingerprint:</span>
                        <p className="font-mono text-gray-900 dark:text-white break-all">
                          {dup.primary.fingerprint.substring(0, 16)}...
                        </p>
                      </div>
                      <div>
                        <span className="text-gray-600 dark:text-gray-400">Validity:</span>
                        <p className="text-gray-900 dark:text-white">
                          {new Date(dup.primary.notBefore).toLocaleDateString()} ~{' '}
                          {new Date(dup.primary.notAfter).toLocaleDateString()}
                        </p>
                      </div>
                    </div>
                  </div>

                  {/* Duplicate Certificate */}
                  <div className="p-3 bg-white dark:bg-gray-800 rounded border border-amber-200 dark:border-amber-700">
                    <div className="flex items-center justify-between mb-2">
                      <h4 className="text-sm font-semibold text-gray-700 dark:text-gray-300">
                        Duplicate Certificate
                      </h4>
                      {dup.ldapStoredCertificateId === dup.duplicate.certificateId && (
                        <span className="px-2 py-0.5 text-xs rounded bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-300">
                          In LDAP
                        </span>
                      )}
                    </div>
                    <div className="space-y-2 text-xs">
                      <div>
                        <span className="text-gray-600 dark:text-gray-400">Fingerprint:</span>
                        <p className="font-mono text-gray-900 dark:text-white break-all">
                          {dup.duplicate.fingerprint.substring(0, 16)}...
                        </p>
                      </div>
                      <div>
                        <span className="text-gray-600 dark:text-gray-400">Validity:</span>
                        <p className="text-gray-900 dark:text-white">
                          {new Date(dup.duplicate.notBefore).toLocaleDateString()} ~{' '}
                          {new Date(dup.duplicate.notAfter).toLocaleDateString()}
                        </p>
                      </div>
                    </div>
                  </div>
                </div>

                {/* LDAP DN */}
                <div className="mt-3 p-2 bg-gray-50 dark:bg-gray-900/50 rounded">
                  <p className="text-xs text-gray-600 dark:text-gray-400 mb-1">LDAP DN:</p>
                  <p className="text-xs font-mono text-gray-900 dark:text-white break-all">
                    {dup.ldapDn}
                  </p>
                </div>
              </div>
            ))}
          </div>
        )}

        <DialogFooter>
          <button
            onClick={onClose}
            className="px-4 py-2 text-sm font-medium text-gray-700 dark:text-gray-200 bg-gray-100 dark:bg-gray-700 rounded hover:bg-gray-200 dark:hover:bg-gray-600"
          >
            Close
          </button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
```

### 5.4 Upload Statistics Enhancement

**Location**: Upload Detail page, Statistics section

**Design**:

```tsx
// Enhanced statistics cards
<div className="grid grid-cols-2 md:grid-cols-4 gap-4">
  {/* Total Certificates */}
  <div className="p-4 bg-blue-50 dark:bg-blue-900/20 rounded-lg">
    <div className="flex items-center justify-between">
      <div>
        <p className="text-sm text-blue-600 dark:text-blue-400">Total Certificates</p>
        <p className="text-2xl font-bold text-blue-900 dark:text-blue-100">
          {upload.statistics.totalCount}
        </p>
      </div>
      <FileText className="w-8 h-8 text-blue-500 opacity-50" />
    </div>
  </div>

  {/* Unique DN+Serial */}
  <div className="p-4 bg-green-50 dark:bg-green-900/20 rounded-lg">
    <div className="flex items-center justify-between">
      <div>
        <p className="text-sm text-green-600 dark:text-green-400">Unique DN+Serial</p>
        <p className="text-2xl font-bold text-green-900 dark:text-green-100">
          {upload.statistics.totalCount - (upload.duplicateCount || 0)}
        </p>
      </div>
      <CheckCircle className="w-8 h-8 text-green-500 opacity-50" />
    </div>
  </div>

  {/* Duplicates */}
  {upload.duplicateCount > 0 && (
    <div className="p-4 bg-amber-50 dark:bg-amber-900/20 rounded-lg">
      <div className="flex items-center justify-between">
        <div>
          <p className="text-sm text-amber-600 dark:text-amber-400">Duplicates</p>
          <p className="text-2xl font-bold text-amber-900 dark:text-amber-100">
            {upload.duplicateCount}
          </p>
        </div>
        <AlertTriangle className="w-8 h-8 text-amber-500 opacity-50" />
      </div>
      <button
        onClick={() => setShowDuplicatesDialog(true)}
        className="mt-2 text-xs text-amber-600 dark:text-amber-400 hover:underline"
      >
        View details →
      </button>
    </div>
  )}

  {/* LDAP Entries */}
  <div className="p-4 bg-purple-50 dark:bg-purple-900/20 rounded-lg">
    <div className="flex items-center justify-between">
      <div>
        <p className="text-sm text-purple-600 dark:text-purple-400">LDAP Entries</p>
        <p className="text-2xl font-bold text-purple-900 dark:text-purple-100">
          {upload.statistics.totalCount - (upload.duplicateCount || 0)}
        </p>
      </div>
      <Database className="w-8 h-8 text-purple-500 opacity-50" />
    </div>
  </div>
</div>
```

---

## 6. Implementation Plan

### Phase 1: Backend Foundation (Week 1)

1. ✅ Create `certificate_duplicates` table
2. ✅ Implement `recordDuplicateCertificate()` function
3. ✅ Update `saveCertificateToLdap()` to detect and record duplicates
4. ✅ Add `duplicate_count` column to `uploaded_file` table
5. ✅ Implement `GET /api/upload/:uploadId/duplicates` endpoint

### Phase 2: Frontend UI (Week 2)

1. ✅ Add duplicate badge to Upload History table
2. ✅ Add Duplicates section to Upload Detail page
3. ✅ Create DuplicatesDialog component
4. ✅ Enhance upload statistics cards
5. ✅ Add API client for duplicates endpoint

### Phase 3: Testing & Refinement (Week 3)

1. ✅ Test with real Master List uploads
2. ✅ Verify duplicate detection accuracy
3. ✅ Test UI/UX with various duplicate scenarios
4. ✅ Performance testing (large uploads)
5. ✅ Documentation update

---

## 7. Future Enhancements

### 7.1 User-Controlled LDAP Storage

**Feature**: Allow users to choose which certificate to store in LDAP when duplicates are detected.

**Design**:
- During upload, pause processing when duplicate is detected
- Show comparison UI in real-time
- User selects preferred certificate
- Resume processing with selected certificate

### 7.2 Duplicate Prevention

**Feature**: Warn users before uploading if duplicate DN+Serial exists.

**Design**:
- Pre-check uploaded file for duplicates with existing certificates
- Show warning dialog before processing
- Allow user to cancel or proceed

### 7.3 Duplicate Merge Tool

**Feature**: Merge duplicate certificates by combining metadata.

**Design**:
- Identify duplicates across multiple uploads
- Provide merge UI
- Create consolidated certificate record
- Update LDAP with merged data

---

## 8. Metrics & Monitoring

### 8.1 Dashboard Metrics

Add to system dashboard:

- **Total Duplicates Detected**: Count over time
- **Duplicate Rate**: Percentage of uploads with duplicates
- **Most Duplicated Countries**: Top 10 countries by duplicate count

### 8.2 Alerts

Configure alerts for:

- High duplicate rate (> 5% of total certificates)
- Unexpected duplicate pattern (e.g., all certificates duplicated)
- LDAP replace failures

---

## 9. References

- **ICAO Doc 9303**: Certificate re-issuance guidelines
- **RFC 5280**: X.509 Certificate validity periods
- **OpenLDAP Admin Guide**: Entry modification operations

---

**Document Owner**: PKD Development Team
**Review Date**: 2026-01-25
**Status**: Approved for Implementation

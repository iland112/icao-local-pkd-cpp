-- =============================================================================
-- ICAO Local PKD - Code Master Table
-- =============================================================================
-- Version: 2.16.0
-- Created: 2026-02-20
-- Description: Centralized code/status/enum management table
--              Replaces hardcoded string literals across backend and frontend
-- =============================================================================

CREATE TABLE IF NOT EXISTS code_master (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    category    VARCHAR(50)  NOT NULL,
    code        VARCHAR(100) NOT NULL,
    name_ko     VARCHAR(255) NOT NULL,
    name_en     VARCHAR(255),
    description TEXT,
    severity    VARCHAR(20),
    sort_order  INTEGER DEFAULT 0,
    is_active   BOOLEAN DEFAULT TRUE,
    metadata    JSONB,
    created_at  TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at  TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    CONSTRAINT uk_code_master UNIQUE (category, code)
);

CREATE INDEX IF NOT EXISTS idx_code_master_category ON code_master(category);
CREATE INDEX IF NOT EXISTS idx_code_master_active   ON code_master(category, is_active);

-- =============================================================================
-- Seed Data (~150 codes across 21 categories)
-- =============================================================================

-- 1. PA_ERROR_CODE — PA Trust Chain error codes
INSERT INTO code_master (category, code, name_ko, name_en, severity, sort_order) VALUES
  ('PA_ERROR_CODE', 'CSCA_NOT_FOUND', 'CSCA 인증서 미등록', 'CSCA Certificate Not Found', 'CRITICAL', 1),
  ('PA_ERROR_CODE', 'CSCA_DN_MISMATCH', 'CSCA DN 불일치', 'CSCA DN Mismatch', 'CRITICAL', 2),
  ('PA_ERROR_CODE', 'CSCA_SELF_SIGNATURE_FAILED', 'CSCA 자체 서명 검증 실패', 'CSCA Self-Signature Verification Failed', 'CRITICAL', 3)
ON CONFLICT (category, code) DO NOTHING;

-- 2. VALIDATION_STATUS — Certificate validation statuses
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('VALIDATION_STATUS', 'VALID', '유효', 'Valid', 1),
  ('VALIDATION_STATUS', 'EXPIRED_VALID', '만료-유효', 'Expired but Valid', 2),
  ('VALIDATION_STATUS', 'INVALID', '무효', 'Invalid', 3),
  ('VALIDATION_STATUS', 'PENDING', '보류', 'Pending', 4),
  ('VALIDATION_STATUS', 'ERROR', '오류', 'Error', 5)
ON CONFLICT (category, code) DO NOTHING;

-- 3. CRL_STATUS — CRL check statuses
INSERT INTO code_master (category, code, name_ko, name_en, severity, sort_order) VALUES
  ('CRL_STATUS', 'VALID', '유효', 'Valid', 'INFO', 1),
  ('CRL_STATUS', 'REVOKED', '폐기됨', 'Revoked', 'CRITICAL', 2),
  ('CRL_STATUS', 'CRL_UNAVAILABLE', 'CRL 없음', 'CRL Unavailable', 'INFO', 3),
  ('CRL_STATUS', 'CRL_EXPIRED', 'CRL 만료', 'CRL Expired', 'WARNING', 4),
  ('CRL_STATUS', 'CRL_INVALID', 'CRL 무효', 'CRL Invalid', 'WARNING', 5),
  ('CRL_STATUS', 'NOT_CHECKED', '미검사', 'Not Checked', 'INFO', 6)
ON CONFLICT (category, code) DO NOTHING;

-- 4. CRL_REVOCATION_REASON — RFC 5280 revocation reasons
INSERT INTO code_master (category, code, name_ko, name_en, sort_order, metadata) VALUES
  ('CRL_REVOCATION_REASON', 'unspecified', '미지정', 'Unspecified', 0, '{"rfc5280Code": 0}'),
  ('CRL_REVOCATION_REASON', 'keyCompromise', '키 손상', 'Key Compromise', 1, '{"rfc5280Code": 1}'),
  ('CRL_REVOCATION_REASON', 'cACompromise', 'CA 손상', 'CA Compromise', 2, '{"rfc5280Code": 2}'),
  ('CRL_REVOCATION_REASON', 'affiliationChanged', '소속 변경', 'Affiliation Changed', 3, '{"rfc5280Code": 3}'),
  ('CRL_REVOCATION_REASON', 'superseded', '대체됨', 'Superseded', 4, '{"rfc5280Code": 4}'),
  ('CRL_REVOCATION_REASON', 'cessationOfOperation', '운영 중단', 'Cessation of Operation', 5, '{"rfc5280Code": 5}'),
  ('CRL_REVOCATION_REASON', 'certificateHold', '인증서 보류', 'Certificate Hold', 6, '{"rfc5280Code": 6}'),
  ('CRL_REVOCATION_REASON', 'removeFromCRL', 'CRL에서 제거', 'Remove from CRL', 8, '{"rfc5280Code": 8}'),
  ('CRL_REVOCATION_REASON', 'privilegeWithdrawn', '권한 철회', 'Privilege Withdrawn', 9, '{"rfc5280Code": 9}'),
  ('CRL_REVOCATION_REASON', 'aACompromise', 'AA 손상', 'AA Compromise', 10, '{"rfc5280Code": 10}'),
  ('CRL_REVOCATION_REASON', 'unknown', '알 수 없음', 'Unknown', 99, NULL)
ON CONFLICT (category, code) DO NOTHING;

-- 5. CERTIFICATE_TYPE — Certificate types
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('CERTIFICATE_TYPE', 'CSCA', 'CSCA', 'Country Signing CA', 1),
  ('CERTIFICATE_TYPE', 'DSC', 'DSC', 'Document Signer Certificate', 2),
  ('CERTIFICATE_TYPE', 'DSC_NC', 'DSC (비표준)', 'Non-Conformant DSC', 3),
  ('CERTIFICATE_TYPE', 'MLSC', 'MLSC', 'Master List Signer Certificate', 4),
  ('CERTIFICATE_TYPE', 'CRL', 'CRL', 'Certificate Revocation List', 5),
  ('CERTIFICATE_TYPE', 'ML', 'Master List', 'Master List', 6)
ON CONFLICT (category, code) DO NOTHING;

-- 6. UPLOAD_STATUS — Upload processing statuses
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('UPLOAD_STATUS', 'PENDING', '대기', 'Pending', 1),
  ('UPLOAD_STATUS', 'PARSING', '파싱 중', 'Parsing', 2),
  ('UPLOAD_STATUS', 'PARSED', '파싱 완료', 'Parsed', 3),
  ('UPLOAD_STATUS', 'PROCESSING', '처리 중', 'Processing', 4),
  ('UPLOAD_STATUS', 'VALIDATING', '검증 중', 'Validating', 5),
  ('UPLOAD_STATUS', 'COMPLETED', '완료', 'Completed', 6),
  ('UPLOAD_STATUS', 'FAILED', '실패', 'Failed', 7)
ON CONFLICT (category, code) DO NOTHING;

-- 7. PROCESSING_STAGE — Detailed processing stages (SSE progress)
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('PROCESSING_STAGE', 'UPLOAD_COMPLETED', '파일 업로드 완료', 'Upload Completed', 1),
  ('PROCESSING_STAGE', 'PARSING_STARTED', '파일 파싱 시작', 'Parsing Started', 2),
  ('PROCESSING_STAGE', 'PARSING_IN_PROGRESS', '파일 파싱 중', 'Parsing In Progress', 3),
  ('PROCESSING_STAGE', 'PARSING_COMPLETED', '파일 파싱 완료', 'Parsing Completed', 4),
  ('PROCESSING_STAGE', 'VALIDATION_STARTED', '인증서 검증 시작', 'Validation Started', 5),
  ('PROCESSING_STAGE', 'VALIDATION_EXTRACTING_METADATA', '메타데이터 추출 중', 'Extracting Metadata', 6),
  ('PROCESSING_STAGE', 'VALIDATION_VERIFYING_SIGNATURE', '서명 검증 중', 'Verifying Signature', 7),
  ('PROCESSING_STAGE', 'VALIDATION_CHECKING_TRUST_CHAIN', 'Trust Chain 검증 중', 'Checking Trust Chain', 8),
  ('PROCESSING_STAGE', 'VALIDATION_CHECKING_CRL', 'CRL 검증 중', 'Checking CRL', 9),
  ('PROCESSING_STAGE', 'VALIDATION_CHECKING_ICAO_COMPLIANCE', 'ICAO 준수 검증 중', 'Checking ICAO Compliance', 10),
  ('PROCESSING_STAGE', 'VALIDATION_IN_PROGRESS', '인증서 검증 중', 'Validation In Progress', 11),
  ('PROCESSING_STAGE', 'VALIDATION_COMPLETED', '인증서 검증 완료', 'Validation Completed', 12),
  ('PROCESSING_STAGE', 'DB_SAVING_STARTED', 'DB 저장 시작', 'DB Saving Started', 13),
  ('PROCESSING_STAGE', 'DB_SAVING_IN_PROGRESS', 'DB 저장 중', 'DB Saving In Progress', 14),
  ('PROCESSING_STAGE', 'DB_SAVING_COMPLETED', 'DB 저장 완료', 'DB Saving Completed', 15),
  ('PROCESSING_STAGE', 'LDAP_SAVING_STARTED', 'LDAP 저장 시작', 'LDAP Saving Started', 16),
  ('PROCESSING_STAGE', 'LDAP_SAVING_IN_PROGRESS', 'LDAP 저장 중', 'LDAP Saving In Progress', 17),
  ('PROCESSING_STAGE', 'LDAP_SAVING_COMPLETED', 'LDAP 저장 완료', 'LDAP Saving Completed', 18),
  ('PROCESSING_STAGE', 'COMPLETED', '처리 완료', 'Completed', 19),
  ('PROCESSING_STAGE', 'FAILED', '처리 실패', 'Failed', 20)
ON CONFLICT (category, code) DO NOTHING;

-- 8. SOURCE_TYPE — Certificate source types
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('SOURCE_TYPE', 'LDIF_PARSED', 'LDIF 업로드', 'LDIF Upload', 1),
  ('SOURCE_TYPE', 'ML_PARSED', 'Master List', 'Master List', 2),
  ('SOURCE_TYPE', 'FILE_UPLOAD', '파일 업로드', 'File Upload', 3),
  ('SOURCE_TYPE', 'PA_EXTRACTED', 'PA 검증 추출', 'PA Verification Extracted', 4),
  ('SOURCE_TYPE', 'DL_PARSED', '편차 목록', 'Deviation List', 5)
ON CONFLICT (category, code) DO NOTHING;

-- 9. EXPIRATION_STATUS — Certificate expiration statuses
INSERT INTO code_master (category, code, name_ko, name_en, severity, sort_order) VALUES
  ('EXPIRATION_STATUS', 'VALID', '유효', 'Valid', 'INFO', 1),
  ('EXPIRATION_STATUS', 'WARNING', '만료 임박', 'Expiring Soon', 'WARNING', 2),
  ('EXPIRATION_STATUS', 'EXPIRED', '만료', 'Expired', 'CRITICAL', 3)
ON CONFLICT (category, code) DO NOTHING;

-- 10. CERTIFICATE_VALIDITY — Certificate validity statuses
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('CERTIFICATE_VALIDITY', 'VALID', '유효', 'Valid', 1),
  ('CERTIFICATE_VALIDITY', 'EXPIRED', '만료', 'Expired', 2),
  ('CERTIFICATE_VALIDITY', 'NOT_YET_VALID', '유효 전', 'Not Yet Valid', 3),
  ('CERTIFICATE_VALIDITY', 'UNKNOWN', '알 수 없음', 'Unknown', 4)
ON CONFLICT (category, code) DO NOTHING;

-- 11. ICAO_COMPLIANCE_LEVEL — ICAO 9303 compliance levels
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('ICAO_COMPLIANCE_LEVEL', 'CONFORMANT', 'ICAO 준수', 'ICAO Conformant', 1),
  ('ICAO_COMPLIANCE_LEVEL', 'WARNING', 'ICAO 경고', 'ICAO Warning', 2),
  ('ICAO_COMPLIANCE_LEVEL', 'NON_CONFORMANT', 'ICAO 미준수', 'ICAO Non-Conformant', 3)
ON CONFLICT (category, code) DO NOTHING;

-- 12. CRL_STATUS_SEVERITY — CRL status severity levels
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('CRL_STATUS_SEVERITY', 'INFO', '정보', 'Info', 1),
  ('CRL_STATUS_SEVERITY', 'WARNING', '경고', 'Warning', 2),
  ('CRL_STATUS_SEVERITY', 'CRITICAL', '심각', 'Critical', 3)
ON CONFLICT (category, code) DO NOTHING;

-- 13. AUTH_EVENT_TYPE — Authentication event types
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('AUTH_EVENT_TYPE', 'LOGIN', '로그인 성공', 'Login Success', 1),
  ('AUTH_EVENT_TYPE', 'LOGIN_FAILED', '로그인 실패', 'Login Failed', 2),
  ('AUTH_EVENT_TYPE', 'LOGOUT', '로그아웃', 'Logout', 3),
  ('AUTH_EVENT_TYPE', 'TOKEN_REFRESH', '토큰 갱신', 'Token Refresh', 4)
ON CONFLICT (category, code) DO NOTHING;

-- 14. OPERATION_TYPE — Audit operation types
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('OPERATION_TYPE', 'FILE_UPLOAD', '파일 업로드', 'File Upload', 1),
  ('OPERATION_TYPE', 'CERT_EXPORT', '인증서 내보내기', 'Certificate Export', 2),
  ('OPERATION_TYPE', 'UPLOAD_DELETE', '업로드 삭제', 'Upload Delete', 3),
  ('OPERATION_TYPE', 'CERTIFICATE_SEARCH', '인증서 검색', 'Certificate Search', 4),
  ('OPERATION_TYPE', 'PA_VERIFY', 'PA 검증', 'PA Verification', 5),
  ('OPERATION_TYPE', 'PA_PARSE_SOD', 'SOD 파싱', 'SOD Parse', 6),
  ('OPERATION_TYPE', 'PA_PARSE_DG1', 'DG1 파싱', 'DG1 Parse', 7),
  ('OPERATION_TYPE', 'PA_PARSE_DG2', 'DG2 파싱', 'DG2 Parse', 8),
  ('OPERATION_TYPE', 'SYNC_TRIGGER', '동기화 트리거', 'Sync Trigger', 9),
  ('OPERATION_TYPE', 'SYNC_CHECK', '동기화 확인', 'Sync Check', 10),
  ('OPERATION_TYPE', 'RECONCILE', '재조정', 'Reconciliation', 11),
  ('OPERATION_TYPE', 'REVALIDATE', '재검증', 'Revalidation', 12),
  ('OPERATION_TYPE', 'CONFIG_UPDATE', '설정 변경', 'Config Update', 13),
  ('OPERATION_TYPE', 'SYSTEM_HEALTH', '시스템 상태', 'System Health', 14),
  ('OPERATION_TYPE', 'UNKNOWN', '알 수 없음', 'Unknown', 15)
ON CONFLICT (category, code) DO NOTHING;

-- 15. SYNC_STATUS — DB-LDAP sync statuses
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('SYNC_STATUS', 'SYNCED', '동기화됨', 'Synced', 1),
  ('SYNC_STATUS', 'DISCREPANCY', '불일치 감지', 'Discrepancy', 2),
  ('SYNC_STATUS', 'ERROR', '오류', 'Error', 3),
  ('SYNC_STATUS', 'PENDING', '대기 중', 'Pending', 4),
  ('SYNC_STATUS', 'NO_DATA', '데이터 없음', 'No Data', 5)
ON CONFLICT (category, code) DO NOTHING;

-- 16. RECONCILIATION_STATUS — Reconciliation result statuses
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('RECONCILIATION_STATUS', 'COMPLETED', '완료', 'Completed', 1),
  ('RECONCILIATION_STATUS', 'PARTIAL', '부분 완료', 'Partial', 2),
  ('RECONCILIATION_STATUS', 'FAILED', '실패', 'Failed', 3),
  ('RECONCILIATION_STATUS', 'IN_PROGRESS', '진행 중', 'In Progress', 4)
ON CONFLICT (category, code) DO NOTHING;

-- 17. RECONCILIATION_TRIGGER — Reconciliation trigger types
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('RECONCILIATION_TRIGGER', 'MANUAL', '수동', 'Manual', 1),
  ('RECONCILIATION_TRIGGER', 'AUTO', '자동', 'Auto', 2),
  ('RECONCILIATION_TRIGGER', 'DAILY_SYNC', '일일 동기화', 'Daily Sync', 3)
ON CONFLICT (category, code) DO NOTHING;

-- 18. ICAO_VERSION_STATUS — ICAO PKD version statuses
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('ICAO_VERSION_STATUS', 'DETECTED', '감지됨', 'Detected', 1),
  ('ICAO_VERSION_STATUS', 'NOTIFIED', '알림 전송', 'Notified', 2),
  ('ICAO_VERSION_STATUS', 'DOWNLOADED', '다운로드 완료', 'Downloaded', 3),
  ('ICAO_VERSION_STATUS', 'IMPORTED', '가져오기 완료', 'Imported', 4),
  ('ICAO_VERSION_STATUS', 'FAILED', '실패', 'Failed', 5)
ON CONFLICT (category, code) DO NOTHING;

-- 19. FILE_FORMAT — Supported file formats
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('FILE_FORMAT', 'LDIF', 'LDIF', 'LDAP Data Interchange Format', 1),
  ('FILE_FORMAT', 'ML', 'Master List', 'ICAO Master List', 2),
  ('FILE_FORMAT', 'PEM', 'PEM', 'Privacy-Enhanced Mail', 3),
  ('FILE_FORMAT', 'DER', 'DER', 'Distinguished Encoding Rules', 4),
  ('FILE_FORMAT', 'CER', 'CER', 'Certificate (DER)', 5),
  ('FILE_FORMAT', 'P7B', 'P7B', 'PKCS#7 Bundle', 6),
  ('FILE_FORMAT', 'DL', 'DL', 'Deviation List', 7),
  ('FILE_FORMAT', 'CRL', 'CRL', 'Certificate Revocation List', 8)
ON CONFLICT (category, code) DO NOTHING;

-- 20. ICAO_VIOLATION_CATEGORY — ICAO compliance violation categories
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('ICAO_VIOLATION_CATEGORY', 'keyUsage', 'Key Usage', 'Key Usage', 1),
  ('ICAO_VIOLATION_CATEGORY', 'algorithm', '서명 알고리즘', 'Signature Algorithm', 2),
  ('ICAO_VIOLATION_CATEGORY', 'keySize', '키 크기', 'Key Size', 3),
  ('ICAO_VIOLATION_CATEGORY', 'validityPeriod', '유효 기간', 'Validity Period', 4),
  ('ICAO_VIOLATION_CATEGORY', 'dnFormat', 'DN 형식', 'DN Format', 5),
  ('ICAO_VIOLATION_CATEGORY', 'extensions', '확장 필드', 'Extensions', 6)
ON CONFLICT (category, code) DO NOTHING;

-- 21. VALIDATION_REASON — Trust chain validation failure reasons
INSERT INTO code_master (category, code, name_ko, name_en, sort_order) VALUES
  ('VALIDATION_REASON', 'TRUST_CHAIN_SIGNATURE_FAILED', '서명 검증 실패', 'Trust Chain Signature Verification Failed', 1),
  ('VALIDATION_REASON', 'CHAIN_BROKEN', 'Trust Chain 끊김', 'Chain Broken', 2),
  ('VALIDATION_REASON', 'CSCA_NOT_FOUND', 'CSCA 미등록', 'CSCA Not Found', 3),
  ('VALIDATION_REASON', 'NOT_YET_VALID', '유효기간 미도래', 'Not Yet Valid', 4),
  ('VALIDATION_REASON', 'CERTIFICATES_EXPIRED', '인증서 만료 (서명 유효)', 'Certificates Expired (Signature Valid)', 5)
ON CONFLICT (category, code) DO NOTHING;

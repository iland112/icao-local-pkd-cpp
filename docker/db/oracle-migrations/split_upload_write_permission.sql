-- =============================================================================
-- Migration: Split upload:write → upload:file + upload:cert
-- Date: 2026-03-24
-- Description: upload:write를 upload:file (ML/LDIF 파일 업로드)과
--              upload:cert (개별 인증서 업로드)로 분리
-- Supports: Oracle
-- =============================================================================

-- users 테이블: upload:write → upload:file, upload:cert
UPDATE users
SET permissions = REPLACE(permissions, '"upload:write"', '"upload:file","upload:cert"')
WHERE permissions LIKE '%upload:write%';
COMMIT;

-- api_clients 테이블: upload:write → upload:file, upload:cert
UPDATE api_clients
SET permissions = REPLACE(permissions, '"upload:write"', '"upload:file","upload:cert"')
WHERE permissions LIKE '%upload:write%';
COMMIT;

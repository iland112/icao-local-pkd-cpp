-- Migration: Expand PII columns for AES-256-GCM encryption (v2.31.2)
-- Encrypted format: "ENC:" + hex(IV[12] + ciphertext + tag[16]) = ~78+ bytes
-- Required for: 개인정보보호법 제24조/제29조 PII encryption compliance
--
-- PostgreSQL: Already handled by init scripts (VARCHAR → VARCHAR(1024))
-- Oracle: Existing tables need ALTER since init scripts only run on fresh DB

-- ============================================================
-- PostgreSQL
-- ============================================================
-- Run these only if columns are still small (idempotent check not needed — ALTER to same size is safe)
--
-- ALTER TABLE pa_verification ALTER COLUMN document_number TYPE VARCHAR(1024);
-- ALTER TABLE pa_verification ALTER COLUMN client_ip TYPE VARCHAR(1024);
-- ALTER TABLE api_client_requests ALTER COLUMN requester_name TYPE VARCHAR(1024);
-- ALTER TABLE api_client_requests ALTER COLUMN requester_org TYPE VARCHAR(1024);
-- ALTER TABLE api_client_requests ALTER COLUMN requester_contact_phone TYPE VARCHAR(1024);
-- ALTER TABLE api_client_requests ALTER COLUMN requester_contact_email TYPE VARCHAR(1024);

-- ============================================================
-- Oracle
-- ============================================================
-- PA_VERIFICATION PII columns (document_number: 50→1024, client_ip: 45→1024)
ALTER TABLE pa_verification MODIFY document_number VARCHAR2(1024);
ALTER TABLE pa_verification MODIFY client_ip VARCHAR2(1024);

-- API_CLIENT_REQUESTS PII columns (requester_*: 50~255→1024)
ALTER TABLE api_client_requests MODIFY requester_name VARCHAR2(1024);
ALTER TABLE api_client_requests MODIFY requester_org VARCHAR2(1024);
ALTER TABLE api_client_requests MODIFY requester_contact_phone VARCHAR2(1024);
ALTER TABLE api_client_requests MODIFY requester_contact_email VARCHAR2(1024);

COMMIT;

-- =============================================================================
-- Migration: Fix operation_audit_log schema to match logOperation() function
-- =============================================================================
-- The original schema had: action, status, details (unused)
-- The code (audit_log.h logOperation) uses: operation_subtype, success,
--   status_code, metadata, duration_ms, request_method, request_path
-- =============================================================================

-- Remove unused columns
ALTER TABLE operation_audit_log DROP COLUMN IF EXISTS action;
ALTER TABLE operation_audit_log DROP COLUMN IF EXISTS status;
ALTER TABLE operation_audit_log DROP COLUMN IF EXISTS details;

-- Add missing columns used by logOperation()
ALTER TABLE operation_audit_log ADD COLUMN IF NOT EXISTS operation_subtype VARCHAR(100);
ALTER TABLE operation_audit_log ADD COLUMN IF NOT EXISTS request_method VARCHAR(10);
ALTER TABLE operation_audit_log ADD COLUMN IF NOT EXISTS request_path TEXT;
ALTER TABLE operation_audit_log ADD COLUMN IF NOT EXISTS success BOOLEAN DEFAULT true;
ALTER TABLE operation_audit_log ADD COLUMN IF NOT EXISTS status_code INTEGER;
ALTER TABLE operation_audit_log ADD COLUMN IF NOT EXISTS metadata JSONB;
ALTER TABLE operation_audit_log ADD COLUMN IF NOT EXISTS duration_ms INTEGER;

-- Update indexes
DROP INDEX IF EXISTS idx_op_audit_status;
CREATE INDEX IF NOT EXISTS idx_op_audit_success ON operation_audit_log(success);

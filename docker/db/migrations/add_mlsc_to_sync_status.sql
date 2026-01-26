-- =============================================================================
-- Database Migration: Add MLSC Support to sync_status Table
-- =============================================================================
-- Date: 2026-01-26
-- Version: v2.1.0
-- Purpose: Add Master List Signer Certificate (MLSC) columns for sync tracking
-- =============================================================================

-- Add MLSC columns to sync_status table
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS db_mlsc_count INTEGER DEFAULT 0,
ADD COLUMN IF NOT EXISTS ldap_mlsc_count INTEGER DEFAULT 0,
ADD COLUMN IF NOT EXISTS mlsc_discrepancy INTEGER DEFAULT 0;

-- Update existing rows to have 0 for MLSC counts
UPDATE sync_status
SET
    db_mlsc_count = 0,
    ldap_mlsc_count = 0,
    mlsc_discrepancy = 0
WHERE db_mlsc_count IS NULL;

-- Add comment to document the change
COMMENT ON COLUMN sync_status.db_mlsc_count IS 'Master List Signer Certificate count in PostgreSQL (Sprint 3)';
COMMENT ON COLUMN sync_status.ldap_mlsc_count IS 'Master List Signer Certificate count in LDAP (Sprint 3)';
COMMENT ON COLUMN sync_status.mlsc_discrepancy IS 'MLSC discrepancy between DB and LDAP (Sprint 3)';

-- Verify the migration
SELECT column_name, data_type, column_default, is_nullable
FROM information_schema.columns
WHERE table_name = 'sync_status'
  AND column_name IN ('db_mlsc_count', 'ldap_mlsc_count', 'mlsc_discrepancy')
ORDER BY column_name;

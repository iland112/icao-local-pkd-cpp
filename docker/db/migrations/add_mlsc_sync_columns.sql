-- =============================================================================
-- Database Migration: Add MLSC columns to sync_status Table
-- =============================================================================
-- Date: 2026-01-28
-- Version: v2.1.2.6
-- Purpose: Track Master List Signer Certificate (MLSC) in sync status
-- Related Issue: PKD Relay Service MLSC support
-- =============================================================================

-- Add mlsc columns to sync_status table
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS db_mlsc_count INTEGER DEFAULT 0;

ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS ldap_mlsc_count INTEGER DEFAULT 0;

ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS mlsc_discrepancy INTEGER DEFAULT 0;

-- Update existing rows to have 0 for MLSC counts
UPDATE sync_status
SET db_mlsc_count = 0,
    ldap_mlsc_count = 0,
    mlsc_discrepancy = 0
WHERE db_mlsc_count IS NULL;

-- Add NOT NULL constraints after setting default values
ALTER TABLE sync_status
ALTER COLUMN db_mlsc_count SET NOT NULL;

ALTER TABLE sync_status
ALTER COLUMN ldap_mlsc_count SET NOT NULL;

ALTER TABLE sync_status
ALTER COLUMN mlsc_discrepancy SET NOT NULL;

-- Add comments to document the changes
COMMENT ON COLUMN sync_status.db_mlsc_count IS 'Master List Signer Certificate count in PostgreSQL database';
COMMENT ON COLUMN sync_status.ldap_mlsc_count IS 'Master List Signer Certificate count in LDAP directory';
COMMENT ON COLUMN sync_status.mlsc_discrepancy IS 'Discrepancy count between DB and LDAP for MLSC certificates';

-- Verify the migration
SELECT column_name, data_type, column_default, is_nullable
FROM information_schema.columns
WHERE table_name = 'sync_status'
  AND column_name IN ('db_mlsc_count', 'ldap_mlsc_count', 'mlsc_discrepancy');

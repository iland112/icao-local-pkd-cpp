-- =============================================================================
-- Database Migration: Update reconciliation_summary Table Schema
-- =============================================================================
-- Date: 2026-01-28
-- Version: v2.1.2.6
-- Purpose: Add missing columns and rename columns for PKD Relay Service compatibility
-- Related Issue: PKD Relay Service reconciliation_summary schema mismatch
-- =============================================================================

-- Step 1: Add dry_run column
ALTER TABLE reconciliation_summary
ADD COLUMN IF NOT EXISTS dry_run BOOLEAN DEFAULT FALSE;

-- Step 2: Rename columns to match PKD Relay Service expectations
-- Check if old column names exist before renaming
DO $$
BEGIN
    -- Rename total_success to success_count
    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_name = 'reconciliation_summary' AND column_name = 'total_success'
    ) THEN
        ALTER TABLE reconciliation_summary RENAME COLUMN total_success TO success_count;
    END IF;

    -- Rename total_failed to failed_count
    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_name = 'reconciliation_summary' AND column_name = 'total_failed'
    ) THEN
        ALTER TABLE reconciliation_summary RENAME COLUMN total_failed TO failed_count;
    END IF;
END $$;

-- Step 3: Add deleted columns for tracking certificate removal
ALTER TABLE reconciliation_summary
ADD COLUMN IF NOT EXISTS csca_deleted INTEGER DEFAULT 0;

ALTER TABLE reconciliation_summary
ADD COLUMN IF NOT EXISTS dsc_deleted INTEGER DEFAULT 0;

ALTER TABLE reconciliation_summary
ADD COLUMN IF NOT EXISTS dsc_nc_deleted INTEGER DEFAULT 0;

ALTER TABLE reconciliation_summary
ADD COLUMN IF NOT EXISTS crl_deleted INTEGER DEFAULT 0;

-- Step 4: Update existing rows with default values
UPDATE reconciliation_summary
SET dry_run = FALSE
WHERE dry_run IS NULL;

UPDATE reconciliation_summary
SET csca_deleted = 0, dsc_deleted = 0, dsc_nc_deleted = 0, crl_deleted = 0
WHERE csca_deleted IS NULL OR dsc_deleted IS NULL OR dsc_nc_deleted IS NULL OR crl_deleted IS NULL;

-- Step 5: Add NOT NULL constraints
ALTER TABLE reconciliation_summary
ALTER COLUMN dry_run SET NOT NULL;

ALTER TABLE reconciliation_summary
ALTER COLUMN csca_deleted SET NOT NULL;

ALTER TABLE reconciliation_summary
ALTER COLUMN dsc_deleted SET NOT NULL;

ALTER TABLE reconciliation_summary
ALTER COLUMN dsc_nc_deleted SET NOT NULL;

ALTER TABLE reconciliation_summary
ALTER COLUMN crl_deleted SET NOT NULL;

-- Step 6: Add comments to document the changes
COMMENT ON COLUMN reconciliation_summary.dry_run IS 'Whether this was a dry run (true) or actual reconciliation (false)';
COMMENT ON COLUMN reconciliation_summary.success_count IS 'Number of certificates successfully reconciled';
COMMENT ON COLUMN reconciliation_summary.failed_count IS 'Number of certificates that failed reconciliation';
COMMENT ON COLUMN reconciliation_summary.csca_deleted IS 'Number of CSCA certificates deleted during reconciliation';
COMMENT ON COLUMN reconciliation_summary.dsc_deleted IS 'Number of DSC certificates deleted during reconciliation';
COMMENT ON COLUMN reconciliation_summary.dsc_nc_deleted IS 'Number of DSC_NC certificates deleted during reconciliation';
COMMENT ON COLUMN reconciliation_summary.crl_deleted IS 'Number of CRLs deleted during reconciliation';

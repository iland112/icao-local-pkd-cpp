-- =============================================================================
-- Database Migration: Fix reconciliation_summary Schema for PKD Relay v2.4.0
-- =============================================================================
-- Date: 2026-02-03
-- Version: v2.4.0
-- Purpose: Rename started_at to triggered_at and add total_added column
-- Related Issue: PKD Relay Service reconciliation_summary schema mismatch
-- =============================================================================

-- Step 1: Rename started_at to triggered_at
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_name = 'reconciliation_summary' AND column_name = 'started_at'
    ) THEN
        ALTER TABLE reconciliation_summary RENAME COLUMN started_at TO triggered_at;
        RAISE NOTICE 'Renamed started_at to triggered_at';
    ELSE
        RAISE NOTICE 'Column started_at does not exist, skipping rename';
    END IF;
END $$;

-- Step 2: Add total_added column
ALTER TABLE reconciliation_summary
ADD COLUMN IF NOT EXISTS total_added INTEGER DEFAULT 0 NOT NULL;

-- Step 3: Calculate and populate total_added from existing data
UPDATE reconciliation_summary
SET total_added = COALESCE(csca_added, 0) + COALESCE(dsc_added, 0) +
                  COALESCE(dsc_nc_added, 0) + COALESCE(crl_added, 0)
WHERE total_added = 0 OR total_added IS NULL;

-- Step 4: Update index name if it exists
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM pg_indexes
        WHERE tablename = 'reconciliation_summary' AND indexname = 'idx_recon_summary_started_at'
    ) THEN
        ALTER INDEX idx_recon_summary_started_at RENAME TO idx_recon_summary_triggered_at;
        RAISE NOTICE 'Renamed index idx_recon_summary_started_at to idx_recon_summary_triggered_at';
    END IF;
END $$;

-- Step 5: Add comments to document the changes
COMMENT ON COLUMN reconciliation_summary.triggered_at IS 'Timestamp when the reconciliation was triggered';
COMMENT ON COLUMN reconciliation_summary.total_added IS 'Total number of certificates added (sum of csca_added + dsc_added + dsc_nc_added + crl_added)';

-- Verify the migration
SELECT column_name, data_type, is_nullable, column_default
FROM information_schema.columns
WHERE table_name = 'reconciliation_summary'
  AND column_name IN ('triggered_at', 'completed_at', 'total_added')
ORDER BY column_name;

-- =============================================================================
-- Database Migration: Add mlsc_count to uploaded_file Table
-- =============================================================================
-- Date: 2026-01-27
-- Version: v2.1.1
-- Purpose: Track Master List Signer Certificate (MLSC) extraction count
-- Related Issue: MLSC Extraction Fix - All 28 Master Lists failed to extract MLSC
-- =============================================================================

-- Add mlsc_count column to uploaded_file table
ALTER TABLE uploaded_file
ADD COLUMN IF NOT EXISTS mlsc_count INTEGER DEFAULT 0;

-- Update existing rows to have 0 for MLSC count
UPDATE uploaded_file
SET mlsc_count = 0
WHERE mlsc_count IS NULL;

-- Add NOT NULL constraint after setting default values
ALTER TABLE uploaded_file
ALTER COLUMN mlsc_count SET NOT NULL;

-- Add comment to document the change
COMMENT ON COLUMN uploaded_file.mlsc_count IS 'Master List Signer Certificate count extracted from Master Lists (v2.1.1)';

-- Verify the migration
SELECT column_name, data_type, column_default, is_nullable
FROM information_schema.columns
WHERE table_name = 'uploaded_file'
  AND column_name = 'mlsc_count';

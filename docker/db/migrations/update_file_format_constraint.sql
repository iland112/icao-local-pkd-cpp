-- =============================================================================
-- Update File Format Constraint
-- =============================================================================
-- Date: 2026-02-11
-- Purpose: Extend allowed file formats to include individual cert files
-- =============================================================================

-- Drop existing constraint
ALTER TABLE uploaded_file DROP CONSTRAINT IF EXISTS chk_file_format;

-- Add new constraint with extended formats
ALTER TABLE uploaded_file ADD CONSTRAINT chk_file_format
    CHECK (file_format IN ('LDIF', 'ML', 'PEM', 'DER', 'CER', 'P7B', 'DL', 'CRL'));

-- Verify constraint
SELECT conname, pg_get_constraintdef(oid)
FROM pg_constraint
WHERE conrelid = 'uploaded_file'::regclass
  AND conname = 'chk_file_format';

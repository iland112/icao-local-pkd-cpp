-- =============================================================================
-- Add Certificate Source Tracking
-- =============================================================================
-- Date: 2026-02-04
-- Purpose: Add source tracking to certificate table for PA auto-registration
-- Feature: v2.5.0 - Certificate File Upload & PA Auto-Registration
-- =============================================================================

-- Add source tracking columns to certificate table
ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS source_type VARCHAR(50) DEFAULT 'FILE_UPLOAD',
ADD COLUMN IF NOT EXISTS source_context JSONB,
ADD COLUMN IF NOT EXISTS extracted_from VARCHAR(100),
ADD COLUMN IF NOT EXISTS registered_at TIMESTAMP DEFAULT NOW();

-- Add constraint for certificate source types
ALTER TABLE certificate
DROP CONSTRAINT IF EXISTS chk_cert_source_type;

ALTER TABLE certificate
ADD CONSTRAINT chk_cert_source_type CHECK (
    source_type IN (
        'FILE_UPLOAD',       -- Direct file upload (PEM, DER, CER, BIN)
        'PA_EXTRACTED',      -- Extracted from passport during PA verification
        'LDIF_PARSED',       -- Parsed from LDIF file
        'ML_PARSED',         -- Parsed from Master List
        'DVL_PARSED',        -- Parsed from Deviation List
        'API_REGISTERED',    -- Direct API call
        'SYSTEM_GENERATED'   -- System generated (testing)
    )
);

-- Create indexes for efficient source-based queries
CREATE INDEX IF NOT EXISTS idx_certificate_source
    ON certificate(source_type);

CREATE INDEX IF NOT EXISTS idx_certificate_extracted_from
    ON certificate(extracted_from);

CREATE INDEX IF NOT EXISTS idx_certificate_registered_at
    ON certificate(registered_at);

-- Update uploaded_file source types to include PA_EXTRACTED
ALTER TABLE uploaded_file
DROP CONSTRAINT IF EXISTS chk_source_type;

ALTER TABLE uploaded_file
ADD CONSTRAINT chk_source_type CHECK (
    source_type IN (
        'ICAO_PKD',          -- ICAO Public Key Directory
        'NATIONAL_CA',       -- National Certificate Authority
        'DIPLOMATIC',        -- Diplomatic channel
        'BSI_GERMANY',       -- Germany BSI
        'FOREIGN_AFFAIRS',   -- Ministry of Foreign Affairs
        'EMBASSY',           -- Embassy/Consulate
        'PA_EXTRACTED',      -- Extracted from passport during PA (for reference)
        'MANUAL_UPLOAD',     -- Manual upload by admin
        'UNKNOWN'            -- Unknown source
    )
);

-- Add comments for documentation
COMMENT ON COLUMN certificate.source_type IS 'How the certificate entered the system';
COMMENT ON COLUMN certificate.source_context IS 'Additional metadata about certificate source (JSON)';
COMMENT ON COLUMN certificate.extracted_from IS 'Reference ID (PA verification ID, Upload ID, etc.)';
COMMENT ON COLUMN certificate.registered_at IS 'Timestamp when certificate was first registered';

-- Verify migration
SELECT
    'certificate' as table_name,
    column_name,
    data_type,
    is_nullable
FROM information_schema.columns
WHERE table_name = 'certificate'
  AND column_name IN ('source_type', 'source_context', 'extracted_from', 'registered_at')
ORDER BY ordinal_position;

-- Show constraint
SELECT
    conname,
    pg_get_constraintdef(oid) as definition
FROM pg_constraint
WHERE conrelid = 'certificate'::regclass
  AND conname = 'chk_cert_source_type';

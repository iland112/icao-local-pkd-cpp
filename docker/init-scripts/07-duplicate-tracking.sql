-- ============================================================================
-- Duplicate Certificate Tracking Table
-- ============================================================================
-- Purpose: Track duplicate certificates detected during upload processing
-- Usage: Used by upload detail page to display duplicate statistics
-- Created: 2026-01-31 (v2.2.1)

CREATE TABLE IF NOT EXISTS duplicate_certificate (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),

    -- Upload reference
    upload_id UUID NOT NULL REFERENCES uploaded_file(id) ON DELETE CASCADE,
    first_upload_id UUID REFERENCES uploaded_file(id) ON DELETE SET NULL,

    -- Certificate identification
    fingerprint_sha256 VARCHAR(64) NOT NULL,
    certificate_type VARCHAR(20) NOT NULL CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC', 'CRL')),

    -- Certificate metadata
    subject_dn TEXT,
    issuer_dn TEXT,
    country_code VARCHAR(2),
    serial_number VARCHAR(255),

    -- Duplicate detection info
    duplicate_count INTEGER DEFAULT 1,
    detection_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    -- Indexes for performance
    CONSTRAINT duplicate_certificate_upload_fingerprint_unique UNIQUE (upload_id, fingerprint_sha256, certificate_type)
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_duplicate_certificate_upload_id ON duplicate_certificate(upload_id);
CREATE INDEX IF NOT EXISTS idx_duplicate_certificate_fingerprint ON duplicate_certificate(fingerprint_sha256);
CREATE INDEX IF NOT EXISTS idx_duplicate_certificate_first_upload ON duplicate_certificate(first_upload_id);
CREATE INDEX IF NOT EXISTS idx_duplicate_certificate_country ON duplicate_certificate(country_code);
CREATE INDEX IF NOT EXISTS idx_duplicate_certificate_type ON duplicate_certificate(certificate_type);

-- Comments
COMMENT ON TABLE duplicate_certificate IS 'Tracks duplicate certificates detected during upload processing';
COMMENT ON COLUMN duplicate_certificate.upload_id IS 'Current upload that detected this duplicate';
COMMENT ON COLUMN duplicate_certificate.first_upload_id IS 'Original upload that first introduced this certificate';
COMMENT ON COLUMN duplicate_certificate.fingerprint_sha256 IS 'SHA-256 fingerprint of the certificate';
COMMENT ON COLUMN duplicate_certificate.duplicate_count IS 'Number of times this certificate appeared in current upload';

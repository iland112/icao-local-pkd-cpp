-- =============================================================================
-- ICAO PKD Versions Tracking Table
-- =============================================================================
-- Purpose: Track ICAO PKD file versions detected from public portal
-- Related: Tier 1 Auto Sync implementation
-- Date: 2026-01-19
-- =============================================================================

CREATE TABLE IF NOT EXISTS icao_pkd_versions (
    id SERIAL PRIMARY KEY,

    -- File identification
    collection_type VARCHAR(50) NOT NULL,  -- 'DSC_CRL' or 'MASTERLIST'
    file_name VARCHAR(255) NOT NULL UNIQUE,  -- 'icaopkd-001-dsccrl-005973.ldif'
    file_version INTEGER NOT NULL,  -- Extracted number: 5973

    -- Timestamps
    detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,  -- When we detected this version on ICAO
    downloaded_at TIMESTAMP,  -- When admin downloaded from ICAO
    imported_at TIMESTAMP,  -- When uploaded to our system

    -- Status tracking
    status VARCHAR(50) NOT NULL DEFAULT 'DETECTED',
    -- Values: 'DETECTED', 'NOTIFIED', 'DOWNLOADED', 'IMPORTED', 'FAILED'

    -- Metadata
    notification_sent BOOLEAN DEFAULT FALSE,
    notification_sent_at TIMESTAMP,
    import_upload_id UUID REFERENCES uploaded_file(id),  -- Link to existing upload (UUID type)
    certificate_count INTEGER,  -- From upload stats
    error_message TEXT,

    -- Constraints
    CONSTRAINT unique_file_name UNIQUE(file_name),
    CONSTRAINT unique_collection_version UNIQUE(collection_type, file_version)
);

-- Indexes for performance
CREATE INDEX IF NOT EXISTS idx_icao_versions_status ON icao_pkd_versions(status);
CREATE INDEX IF NOT EXISTS idx_icao_versions_detected_at ON icao_pkd_versions(detected_at DESC);
CREATE INDEX IF NOT EXISTS idx_icao_versions_collection ON icao_pkd_versions(collection_type);

-- Comments for documentation
COMMENT ON TABLE icao_pkd_versions IS 'Tracks ICAO PKD file versions detected from public portal';
COMMENT ON COLUMN icao_pkd_versions.collection_type IS 'DSC_CRL (001) or MASTERLIST (002)';
COMMENT ON COLUMN icao_pkd_versions.file_name IS 'Full LDIF filename from ICAO portal';
COMMENT ON COLUMN icao_pkd_versions.file_version IS 'Extracted version number for comparison';
COMMENT ON COLUMN icao_pkd_versions.status IS 'DETECTED → NOTIFIED → DOWNLOADED → IMPORTED or FAILED';
COMMENT ON COLUMN icao_pkd_versions.import_upload_id IS 'Link to uploaded_file table when admin uploads';

-- =============================================================================
-- Alter uploaded_file table to link ICAO versions
-- =============================================================================

ALTER TABLE uploaded_file
ADD COLUMN IF NOT EXISTS icao_version_id INTEGER REFERENCES icao_pkd_versions(id),
ADD COLUMN IF NOT EXISTS is_icao_official BOOLEAN DEFAULT FALSE;

CREATE INDEX IF NOT EXISTS idx_uploaded_file_icao_version ON uploaded_file(icao_version_id);

COMMENT ON COLUMN uploaded_file.icao_version_id IS 'Link to ICAO PKD version if official file';
COMMENT ON COLUMN uploaded_file.is_icao_official IS 'True if file downloaded from ICAO portal';

-- =============================================================================
-- Sample data (for testing only - comment out in production)
-- =============================================================================

-- Example: Insert a detected version
-- INSERT INTO icao_pkd_versions (collection_type, file_name, file_version, status)
-- VALUES ('DSC_CRL', 'icaopkd-001-dsccrl-005973.ldif', 5973, 'DETECTED');

-- Example: Update after notification sent
-- UPDATE icao_pkd_versions
-- SET status = 'NOTIFIED', notification_sent = TRUE, notification_sent_at = CURRENT_TIMESTAMP
-- WHERE file_version = 5973 AND collection_type = 'DSC_CRL';

-- Example: Link to upload after admin uploads
-- UPDATE icao_pkd_versions
-- SET status = 'IMPORTED',
--     imported_at = CURRENT_TIMESTAMP,
--     import_upload_id = 1,
--     certificate_count = 30500
-- WHERE file_version = 5973 AND collection_type = 'DSC_CRL';

-- =============================================================================
-- Rollback (if needed)
-- =============================================================================

-- To rollback this migration:
-- ALTER TABLE uploaded_file DROP COLUMN IF EXISTS icao_version_id;
-- ALTER TABLE uploaded_file DROP COLUMN IF EXISTS is_icao_official;
-- DROP INDEX IF EXISTS idx_uploaded_file_icao_version;
-- DROP TABLE IF EXISTS icao_pkd_versions CASCADE;

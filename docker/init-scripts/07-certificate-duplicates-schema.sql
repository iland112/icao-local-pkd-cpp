-- =============================================================================
-- Certificate Duplicate Tracking Schema
-- =============================================================================
-- Purpose: Track CSCA certificate duplicates across multiple sources
--          (ML files, Collection 001, Collection 002, Collection 003)
-- Related: Collection 002 CSCA extraction implementation
-- Version: 2.0.0
-- Date: 2026-01-23
-- =============================================================================

-- =============================================================================
-- Step 1: Create certificate_duplicates table
-- =============================================================================

CREATE TABLE IF NOT EXISTS certificate_duplicates (
    id SERIAL PRIMARY KEY,
    certificate_id UUID NOT NULL REFERENCES certificate(id) ON DELETE CASCADE,
    upload_id UUID NOT NULL REFERENCES uploaded_file(id) ON DELETE CASCADE,
    source_type VARCHAR(20) NOT NULL,  -- 'ML_FILE', 'LDIF_001', 'LDIF_002', 'LDIF_003'
    source_country VARCHAR(3),         -- Country code from source
    source_entry_dn TEXT,              -- LDIF entry DN (for LDIF sources)
    source_file_name VARCHAR(255),     -- Original filename
    detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    -- Constraints
    CONSTRAINT unique_cert_upload_source UNIQUE(certificate_id, upload_id, source_type)
);

-- Indexes for performance
CREATE INDEX IF NOT EXISTS idx_cert_dup_certificate_id ON certificate_duplicates(certificate_id);
CREATE INDEX IF NOT EXISTS idx_cert_dup_upload_id ON certificate_duplicates(upload_id);
CREATE INDEX IF NOT EXISTS idx_cert_dup_source_type ON certificate_duplicates(source_type);
CREATE INDEX IF NOT EXISTS idx_cert_dup_detected_at ON certificate_duplicates(detected_at DESC);

-- Comments for documentation
COMMENT ON TABLE certificate_duplicates IS 'Tracks all sources of each certificate for duplicate analysis';
COMMENT ON COLUMN certificate_duplicates.source_type IS 'ML_FILE (ICAO Master List .ml), LDIF_001 (DSC/CRL), LDIF_002 (Master List), LDIF_003 (DSC_NC)';
COMMENT ON COLUMN certificate_duplicates.source_entry_dn IS 'For LDIF sources, the DN of the entry that contained this certificate';

-- =============================================================================
-- Step 2: Add duplicate tracking columns to certificate table
-- =============================================================================

ALTER TABLE certificate
ADD COLUMN IF NOT EXISTS duplicate_count INTEGER NOT NULL DEFAULT 0,
ADD COLUMN IF NOT EXISTS first_upload_id UUID REFERENCES uploaded_file(id),
ADD COLUMN IF NOT EXISTS last_seen_upload_id UUID REFERENCES uploaded_file(id),
ADD COLUMN IF NOT EXISTS last_seen_at TIMESTAMP;

-- Create indexes for new columns
CREATE INDEX IF NOT EXISTS idx_certificate_duplicate_count ON certificate(duplicate_count);
CREATE INDEX IF NOT EXISTS idx_certificate_first_upload_id ON certificate(first_upload_id);
CREATE INDEX IF NOT EXISTS idx_certificate_last_seen_upload_id ON certificate(last_seen_upload_id);

-- Comments for documentation
COMMENT ON COLUMN certificate.duplicate_count IS 'Number of times this certificate was seen across all uploads (0 = unique, N = seen N additional times)';
COMMENT ON COLUMN certificate.first_upload_id IS 'Upload ID where this certificate was first stored';
COMMENT ON COLUMN certificate.last_seen_upload_id IS 'Upload ID where this certificate was most recently seen';
COMMENT ON COLUMN certificate.last_seen_at IS 'Timestamp when this certificate was most recently seen';

-- =============================================================================
-- Step 3: Add CSCA extraction statistics to uploaded_file table
-- =============================================================================

ALTER TABLE uploaded_file
ADD COLUMN IF NOT EXISTS csca_extracted_from_ml INTEGER NOT NULL DEFAULT 0,
ADD COLUMN IF NOT EXISTS csca_duplicates INTEGER NOT NULL DEFAULT 0;

-- Comments for documentation
COMMENT ON COLUMN uploaded_file.csca_extracted_from_ml IS 'Number of CSCAs extracted from Collection 002 Master List entries';
COMMENT ON COLUMN uploaded_file.csca_duplicates IS 'Number of duplicate CSCAs detected during this upload';

-- =============================================================================
-- Step 4: Create view for duplicate statistics
-- =============================================================================

CREATE OR REPLACE VIEW certificate_duplicate_stats AS
SELECT
    c.id,
    c.certificate_type,
    c.country_code,
    c.subject_dn,
    c.serial_number,
    c.fingerprint_sha256,
    c.duplicate_count,
    c.first_upload_id,
    uf_first.file_name AS first_seen_file,
    uf_first.file_format AS first_seen_format,
    c.last_seen_upload_id,
    uf_last.file_name AS last_seen_file,
    uf_last.file_format AS last_seen_format,
    c.last_seen_at,
    (
        SELECT COUNT(DISTINCT cd.source_type)
        FROM certificate_duplicates cd
        WHERE cd.certificate_id = c.id
    ) AS unique_source_count,
    (
        SELECT json_agg(
            json_build_object(
                'source_type', cd.source_type,
                'source_country', cd.source_country,
                'source_file', cd.source_file_name,
                'detected_at', cd.detected_at
            ) ORDER BY cd.detected_at
        )
        FROM certificate_duplicates cd
        WHERE cd.certificate_id = c.id
    ) AS all_sources
FROM certificate c
LEFT JOIN uploaded_file uf_first ON c.first_upload_id = uf_first.id
LEFT JOIN uploaded_file uf_last ON c.last_seen_upload_id = uf_last.id
WHERE c.duplicate_count > 0
ORDER BY c.duplicate_count DESC, c.last_seen_at DESC;

COMMENT ON VIEW certificate_duplicate_stats IS 'Provides comprehensive duplicate statistics with all source information';

-- =============================================================================
-- Step 5: Create helper function for duplicate analysis
-- =============================================================================

-- Function to get duplicate summary by source type
CREATE OR REPLACE FUNCTION get_duplicate_summary_by_source()
RETURNS TABLE (
    source_type VARCHAR(20),
    total_certificates BIGINT,
    unique_certificates BIGINT,
    duplicate_certificates BIGINT,
    avg_duplicate_count NUMERIC
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        cd.source_type,
        COUNT(*) AS total_certificates,
        COUNT(DISTINCT cd.certificate_id) AS unique_certificates,
        COUNT(*) - COUNT(DISTINCT cd.certificate_id) AS duplicate_certificates,
        ROUND(AVG(c.duplicate_count), 2) AS avg_duplicate_count
    FROM certificate_duplicates cd
    JOIN certificate c ON cd.certificate_id = c.id
    GROUP BY cd.source_type
    ORDER BY total_certificates DESC;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION get_duplicate_summary_by_source() IS 'Returns duplicate statistics grouped by source type (ML_FILE, LDIF_001, LDIF_002, LDIF_003)';

-- =============================================================================
-- Step 6: Sample queries (commented out - for reference only)
-- =============================================================================

-- Example 1: Find all CSCAs from Collection 002 with their duplicate status
/*
SELECT
    c.country_code,
    c.subject_dn,
    c.duplicate_count,
    uf.file_name,
    (
        SELECT COUNT(*)
        FROM certificate_duplicates cd
        WHERE cd.certificate_id = c.id AND cd.source_type = 'LDIF_002'
    ) AS seen_in_ldif_002_count,
    (
        SELECT COUNT(*)
        FROM certificate_duplicates cd
        WHERE cd.certificate_id = c.id AND cd.source_type = 'ML_FILE'
    ) AS seen_in_ml_file_count
FROM certificate c
JOIN uploaded_file uf ON c.first_upload_id = uf.id
WHERE c.certificate_type = 'CSCA'
  AND EXISTS (
      SELECT 1 FROM certificate_duplicates cd
      WHERE cd.certificate_id = c.id AND cd.source_type = 'LDIF_002'
  )
ORDER BY c.duplicate_count DESC;
*/

-- Example 2: Get duplicate summary by source type
/*
SELECT * FROM get_duplicate_summary_by_source();
*/

-- Example 3: Find CSCAs that appear in both ML_FILE and LDIF_002
/*
SELECT
    c.id,
    c.country_code,
    c.subject_dn,
    c.duplicate_count,
    cds.all_sources
FROM certificate_duplicate_stats cds
JOIN certificate c ON cds.id = c.id
WHERE c.certificate_type = 'CSCA'
  AND EXISTS (
      SELECT 1 FROM certificate_duplicates cd1
      WHERE cd1.certificate_id = c.id AND cd1.source_type = 'ML_FILE'
  )
  AND EXISTS (
      SELECT 1 FROM certificate_duplicates cd2
      WHERE cd2.certificate_id = c.id AND cd2.source_type = 'LDIF_002'
  )
ORDER BY c.duplicate_count DESC
LIMIT 20;
*/

-- Example 4: Get duplicate rate statistics
/*
SELECT
    certificate_type,
    COUNT(*) AS total_certificates,
    SUM(CASE WHEN duplicate_count > 0 THEN 1 ELSE 0 END) AS duplicate_certificates,
    ROUND(100.0 * SUM(CASE WHEN duplicate_count > 0 THEN 1 ELSE 0 END) / COUNT(*), 2) AS duplicate_rate_percent,
    ROUND(AVG(duplicate_count), 2) AS avg_duplicate_count,
    MAX(duplicate_count) AS max_duplicate_count
FROM certificate
WHERE certificate_type = 'CSCA'
GROUP BY certificate_type;
*/

-- =============================================================================
-- Rollback (if needed)
-- =============================================================================

-- To rollback this migration:
/*
DROP VIEW IF EXISTS certificate_duplicate_stats CASCADE;
DROP FUNCTION IF EXISTS get_duplicate_summary_by_source() CASCADE;

ALTER TABLE uploaded_file
    DROP COLUMN IF EXISTS csca_extracted_from_ml,
    DROP COLUMN IF EXISTS csca_duplicates;

ALTER TABLE certificate
    DROP COLUMN IF EXISTS duplicate_count,
    DROP COLUMN IF EXISTS first_upload_id,
    DROP COLUMN IF EXISTS last_seen_upload_id,
    DROP COLUMN IF EXISTS last_seen_at;

DROP TABLE IF EXISTS certificate_duplicates CASCADE;
*/

-- =============================================================================
-- End of migration script
-- =============================================================================

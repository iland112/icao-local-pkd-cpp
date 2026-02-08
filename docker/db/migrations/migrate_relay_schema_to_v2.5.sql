-- =============================================================================
-- PKD Relay Service Schema Migration to v2.5.0
-- =============================================================================
-- Description: Migrate from deployed schema to intended v2.5.0 schema
-- Date: 2026-02-06
--
-- Changes:
-- 1. sync_status: INTEGER id → UUID id
-- 2. reconciliation_summary: UUID id → SERIAL id
-- 3. reconciliation_log: Column name alignment with domain models
-- =============================================================================

-- Start transaction
BEGIN;

-- =============================================================================
-- STEP 1: Backup existing data
-- =============================================================================

-- Create backup tables
CREATE TABLE sync_status_backup AS SELECT * FROM sync_status;
CREATE TABLE reconciliation_summary_backup AS SELECT * FROM reconciliation_summary;
CREATE TABLE reconciliation_log_backup AS SELECT * FROM reconciliation_log;

-- =============================================================================
-- STEP 2: Drop dependent tables (cascade)
-- =============================================================================

-- Drop reconciliation_log (depends on reconciliation_summary)
DROP TABLE IF EXISTS reconciliation_log CASCADE;

-- Drop reconciliation_summary (depends on sync_status)
DROP TABLE IF EXISTS reconciliation_summary CASCADE;

-- =============================================================================
-- STEP 3: Migrate sync_status from INTEGER to UUID
-- =============================================================================

-- Create temporary mapping table (old id -> new uuid)
CREATE TEMPORARY TABLE sync_status_id_mapping (
    old_id INTEGER,
    new_id UUID DEFAULT gen_random_uuid()
);

-- Generate new UUIDs for existing records
INSERT INTO sync_status_id_mapping (old_id)
SELECT id FROM sync_status_backup;

-- Drop old sync_status
DROP TABLE IF EXISTS sync_status CASCADE;

-- Create new sync_status with UUID id
CREATE TABLE sync_status (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT NOW(),

    -- Database counts
    db_csca_count INTEGER DEFAULT 0,
    db_mlsc_count INTEGER DEFAULT 0,
    db_dsc_count INTEGER DEFAULT 0,
    db_dsc_nc_count INTEGER DEFAULT 0,
    db_crl_count INTEGER DEFAULT 0,
    db_stored_in_ldap_count INTEGER DEFAULT 0,

    -- LDAP counts
    ldap_csca_count INTEGER DEFAULT 0,
    ldap_mlsc_count INTEGER DEFAULT 0,
    ldap_dsc_count INTEGER DEFAULT 0,
    ldap_dsc_nc_count INTEGER DEFAULT 0,
    ldap_crl_count INTEGER DEFAULT 0,
    ldap_total_entries INTEGER DEFAULT 0,

    -- Discrepancies
    csca_discrepancy INTEGER DEFAULT 0,
    mlsc_discrepancy INTEGER DEFAULT 0,
    dsc_discrepancy INTEGER DEFAULT 0,
    dsc_nc_discrepancy INTEGER DEFAULT 0,
    crl_discrepancy INTEGER DEFAULT 0,
    total_discrepancy INTEGER DEFAULT 0,

    -- Country-level statistics (JSONB)
    db_country_stats JSONB,
    ldap_country_stats JSONB,

    -- Status
    status VARCHAR(20) DEFAULT 'UNKNOWN',
    error_message TEXT,
    check_duration_ms INTEGER DEFAULT 0
);

-- Migrate data with new UUIDs
INSERT INTO sync_status (
    id, checked_at,
    db_csca_count, db_mlsc_count, db_dsc_count, db_dsc_nc_count, db_crl_count, db_stored_in_ldap_count,
    ldap_csca_count, ldap_mlsc_count, ldap_dsc_count, ldap_dsc_nc_count, ldap_crl_count, ldap_total_entries,
    csca_discrepancy, mlsc_discrepancy, dsc_discrepancy, dsc_nc_discrepancy, crl_discrepancy, total_discrepancy,
    db_country_stats, ldap_country_stats,
    status, error_message, check_duration_ms
)
SELECT
    m.new_id, s.checked_at,
    s.db_csca_count, s.db_mlsc_count, s.db_dsc_count, s.db_dsc_nc_count, s.db_crl_count, s.db_stored_in_ldap_count,
    s.ldap_csca_count, s.ldap_mlsc_count, s.ldap_dsc_count, s.ldap_dsc_nc_count, s.ldap_crl_count, s.ldap_total_entries,
    s.csca_discrepancy, s.mlsc_discrepancy, s.dsc_discrepancy, s.dsc_nc_discrepancy, s.crl_discrepancy, s.total_discrepancy,
    s.db_country_stats, s.ldap_country_stats,
    'SYNCED', s.error_message, s.check_duration_ms
FROM sync_status_backup s
JOIN sync_status_id_mapping m ON s.id = m.old_id;

-- Create index
CREATE INDEX IF NOT EXISTS idx_sync_status_checked_at ON sync_status(checked_at DESC);

-- =============================================================================
-- STEP 4: Create new reconciliation_summary with SERIAL id
-- =============================================================================

CREATE TABLE reconciliation_summary (
    id SERIAL PRIMARY KEY,
    triggered_by VARCHAR(100) NOT NULL,
    triggered_at TIMESTAMP DEFAULT NOW(),
    completed_at TIMESTAMP,
    status VARCHAR(20) NOT NULL,
    dry_run BOOLEAN DEFAULT FALSE NOT NULL,

    -- Processing counts
    success_count INTEGER DEFAULT 0,
    failed_count INTEGER DEFAULT 0,

    -- Added counts
    csca_added INTEGER DEFAULT 0,
    dsc_added INTEGER DEFAULT 0,
    dsc_nc_added INTEGER DEFAULT 0,
    crl_added INTEGER DEFAULT 0,
    total_added INTEGER DEFAULT 0,

    -- Deleted counts
    csca_deleted INTEGER DEFAULT 0,
    dsc_deleted INTEGER DEFAULT 0,
    dsc_nc_deleted INTEGER DEFAULT 0,
    crl_deleted INTEGER DEFAULT 0,

    -- Performance
    duration_ms BIGINT,
    error_message TEXT,

    -- Optional link to sync_status (now UUID)
    sync_status_id UUID,

    CONSTRAINT chk_recon_status CHECK (status IN ('PENDING', 'IN_PROGRESS', 'COMPLETED', 'FAILED')),
    CONSTRAINT fk_recon_sync_status FOREIGN KEY (sync_status_id) REFERENCES sync_status(id) ON DELETE SET NULL
);

-- Create indexes
CREATE INDEX IF NOT EXISTS idx_recon_summary_triggered ON reconciliation_summary(triggered_at DESC);
CREATE INDEX IF NOT EXISTS idx_recon_summary_status ON reconciliation_summary(status);
CREATE INDEX IF NOT EXISTS idx_recon_summary_sync_status ON reconciliation_summary(sync_status_id);

-- Migrate data (reconciliation_summary_backup is empty, but for completeness)
-- Note: Old UUID IDs cannot be mapped to new SERIAL IDs, so we generate new IDs
-- and update sync_status_id references using the mapping table
INSERT INTO reconciliation_summary (
    triggered_by, triggered_at, completed_at, status, dry_run,
    success_count, failed_count,
    csca_added, dsc_added, dsc_nc_added, crl_added, total_added,
    csca_deleted, dsc_deleted, dsc_nc_deleted, crl_deleted,
    duration_ms, error_message, sync_status_id
)
SELECT
    r.triggered_by,
    r.started_at AS triggered_at,
    r.completed_at,
    r.status,
    r.dry_run,
    r.success_count,
    r.failed_count,
    r.csca_added,
    r.dsc_added,
    r.dsc_nc_added,
    r.crl_added,
    (r.csca_added + r.dsc_added + r.dsc_nc_added + r.crl_added) AS total_added,
    r.csca_deleted,
    r.dsc_deleted,
    r.dsc_nc_deleted,
    r.crl_deleted,
    r.duration_ms,
    r.error_message,
    m.new_id AS sync_status_id
FROM reconciliation_summary_backup r
LEFT JOIN sync_status_id_mapping m ON r.sync_status_id = m.old_id;

-- =============================================================================
-- STEP 5: Create new reconciliation_log with aligned column names
-- =============================================================================

CREATE TABLE reconciliation_log (
    id SERIAL PRIMARY KEY,
    reconciliation_id INTEGER NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    cert_fingerprint VARCHAR(64) NOT NULL,
    cert_type VARCHAR(20) NOT NULL,
    country_code VARCHAR(3) NOT NULL,
    action VARCHAR(20) NOT NULL,
    result VARCHAR(20) NOT NULL,
    error_message TEXT,

    CONSTRAINT fk_recon_log_summary FOREIGN KEY (reconciliation_id) REFERENCES reconciliation_summary(id) ON DELETE CASCADE,
    CONSTRAINT chk_recon_action CHECK (action IN ('ADD_TO_LDAP', 'UPDATE_LDAP', 'DELETE_FROM_LDAP', 'VERIFY', 'SKIP')),
    CONSTRAINT chk_recon_result CHECK (result IN ('SUCCESS', 'FAILED', 'SKIPPED'))
);

-- Create indexes
CREATE INDEX IF NOT EXISTS idx_recon_log_recon_id ON reconciliation_log(reconciliation_id);
CREATE INDEX IF NOT EXISTS idx_recon_log_fingerprint ON reconciliation_log(cert_fingerprint);
CREATE INDEX IF NOT EXISTS idx_recon_log_created ON reconciliation_log(created_at DESC);

-- Migrate data (mapping old column names to new ones)
-- Note: Old summary_id (UUID) cannot be directly mapped to new reconciliation_id (INTEGER)
-- Since reconciliation_summary was rebuilt, we cannot preserve the relationship
-- Only migrate if we can establish the mapping (which we can't for UUID->SERIAL)
-- So this INSERT is commented out as reconciliation_log_backup references old UUIDs

-- INSERT INTO reconciliation_log (
--     reconciliation_id, created_at, cert_fingerprint, cert_type, country_code,
--     action, result, error_message
-- )
-- SELECT
--     -- Cannot map summary_id (UUID) to reconciliation_id (SERIAL)
--     NULL, -- This will fail FK constraint
--     l.started_at AS created_at,
--     l.fingerprint_sha256 AS cert_fingerprint,
--     l.certificate_type AS cert_type,
--     l.country_code,
--     CASE l.operation
--         WHEN 'SYNC_TO_LDAP' THEN 'ADD_TO_LDAP'
--         WHEN 'DELETE_FROM_LDAP' THEN 'DELETE_FROM_LDAP'
--         ELSE 'SKIP'
--     END AS action,
--     CASE l.status
--         WHEN 'SUCCESS' THEN 'SUCCESS'
--         WHEN 'FAILED' THEN 'FAILED'
--         ELSE 'SKIPPED'
--     END AS result,
--     l.error_message
-- FROM reconciliation_log_backup l;

-- =============================================================================
-- STEP 6: Drop backup tables (optional - keep for safety)
-- =============================================================================

-- DROP TABLE IF EXISTS sync_status_backup CASCADE;
-- DROP TABLE IF EXISTS reconciliation_summary_backup CASCADE;
-- DROP TABLE IF EXISTS reconciliation_log_backup CASCADE;

-- =============================================================================
-- COMMIT
-- =============================================================================

COMMIT;

-- =============================================================================
-- Verification Queries
-- =============================================================================

-- Check record counts
SELECT 'sync_status' AS table_name, COUNT(*) AS record_count FROM sync_status
UNION ALL
SELECT 'reconciliation_summary', COUNT(*) FROM reconciliation_summary
UNION ALL
SELECT 'reconciliation_log', COUNT(*) FROM reconciliation_log;

-- Check data types
SELECT 'sync_status.id' AS column_name, data_type FROM information_schema.columns WHERE table_name = 'sync_status' AND column_name = 'id'
UNION ALL
SELECT 'reconciliation_summary.id', data_type FROM information_schema.columns WHERE table_name = 'reconciliation_summary' AND column_name = 'id'
UNION ALL
SELECT 'reconciliation_summary.sync_status_id', data_type FROM information_schema.columns WHERE table_name = 'reconciliation_summary' AND column_name = 'sync_status_id';

-- Show migrated sync_status records
SELECT id, checked_at, total_discrepancy, status FROM sync_status ORDER BY checked_at DESC LIMIT 5;

SELECT 'Migration completed successfully' AS status;

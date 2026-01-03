-- =============================================================================
-- ICAO Local PKD - Sync Status Schema
-- =============================================================================
-- Version: 1.0
-- Created: 2026-01-03
-- Description: DB-LDAP synchronization status and discrepancy tracking
-- =============================================================================

-- =============================================================================
-- Sync Status Table (periodic sync check results)
-- =============================================================================

CREATE TABLE IF NOT EXISTS sync_status (
    id SERIAL PRIMARY KEY,
    checked_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    -- DB Statistics
    db_csca_count INTEGER NOT NULL DEFAULT 0,
    db_dsc_count INTEGER NOT NULL DEFAULT 0,
    db_dsc_nc_count INTEGER NOT NULL DEFAULT 0,
    db_crl_count INTEGER NOT NULL DEFAULT 0,
    db_stored_in_ldap_count INTEGER NOT NULL DEFAULT 0,  -- stored_in_ldap=TRUE count

    -- LDAP Statistics
    ldap_csca_count INTEGER NOT NULL DEFAULT 0,
    ldap_dsc_count INTEGER NOT NULL DEFAULT 0,
    ldap_dsc_nc_count INTEGER NOT NULL DEFAULT 0,
    ldap_crl_count INTEGER NOT NULL DEFAULT 0,
    ldap_total_entries INTEGER NOT NULL DEFAULT 0,

    -- Country breakdown (JSON for flexibility)
    db_country_stats JSONB,   -- {"KOR": {"csca": 5, "dsc": 100}, ...}
    ldap_country_stats JSONB, -- {"KOR": {"csca": 5, "dsc": 100}, ...}

    -- Discrepancy counts
    csca_discrepancy INTEGER GENERATED ALWAYS AS (db_csca_count - ldap_csca_count) STORED,
    dsc_discrepancy INTEGER GENERATED ALWAYS AS (db_dsc_count - ldap_dsc_count) STORED,
    dsc_nc_discrepancy INTEGER GENERATED ALWAYS AS (db_dsc_nc_count - ldap_dsc_nc_count) STORED,
    crl_discrepancy INTEGER GENERATED ALWAYS AS (db_crl_count - ldap_crl_count) STORED,
    total_discrepancy INTEGER GENERATED ALWAYS AS (
        ABS(db_csca_count - ldap_csca_count) +
        ABS(db_dsc_count - ldap_dsc_count) +
        ABS(db_dsc_nc_count - ldap_dsc_nc_count) +
        ABS(db_crl_count - ldap_crl_count)
    ) STORED,

    -- Overall status
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',  -- SYNCED, DISCREPANCY, ERROR, PENDING
    error_message TEXT,

    -- Timing
    check_duration_ms INTEGER,

    CONSTRAINT chk_sync_status CHECK (status IN ('SYNCED', 'DISCREPANCY', 'ERROR', 'PENDING'))
);

CREATE INDEX IF NOT EXISTS idx_sync_status_checked_at ON sync_status(checked_at DESC);
CREATE INDEX IF NOT EXISTS idx_sync_status_status ON sync_status(status);

-- =============================================================================
-- Sync Discrepancy Table (individual mismatches)
-- =============================================================================

CREATE TABLE IF NOT EXISTS sync_discrepancy (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    sync_status_id INTEGER REFERENCES sync_status(id) ON DELETE CASCADE,
    detected_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    -- Reference to the item
    item_type VARCHAR(20) NOT NULL,  -- CERTIFICATE, CRL
    certificate_id UUID REFERENCES certificate(id) ON DELETE SET NULL,
    crl_id UUID REFERENCES crl(id) ON DELETE SET NULL,

    -- Item details (denormalized for when reference is deleted)
    certificate_type VARCHAR(20),  -- CSCA, DSC, DSC_NC
    country_code VARCHAR(3),
    fingerprint VARCHAR(64),
    subject_dn TEXT,
    ldap_dn TEXT,

    -- Issue details
    issue_type VARCHAR(30) NOT NULL,  -- MISSING_IN_LDAP, MISSING_IN_DB, MISMATCH
    issue_details TEXT,

    -- DB state
    db_exists BOOLEAN NOT NULL DEFAULT FALSE,
    db_stored_in_ldap BOOLEAN,
    db_stored_at TIMESTAMP WITH TIME ZONE,

    -- LDAP state
    ldap_exists BOOLEAN NOT NULL DEFAULT FALSE,
    ldap_entry_dn TEXT,

    -- Resolution
    resolved BOOLEAN NOT NULL DEFAULT FALSE,
    resolved_at TIMESTAMP WITH TIME ZONE,
    resolution_type VARCHAR(30),  -- SYNCED_TO_LDAP, DELETED_FROM_LDAP, UPDATED_IN_DB, MANUAL, SKIPPED
    resolution_message TEXT,
    resolved_by VARCHAR(100),  -- 'AUTO' or username

    CONSTRAINT chk_item_type CHECK (item_type IN ('CERTIFICATE', 'CRL')),
    CONSTRAINT chk_issue_type CHECK (issue_type IN ('MISSING_IN_LDAP', 'MISSING_IN_DB', 'MISMATCH')),
    CONSTRAINT chk_resolution_type CHECK (resolution_type IS NULL OR resolution_type IN (
        'SYNCED_TO_LDAP', 'DELETED_FROM_LDAP', 'UPDATED_IN_DB', 'MANUAL', 'SKIPPED'
    ))
);

CREATE INDEX IF NOT EXISTS idx_sync_discrepancy_status_id ON sync_discrepancy(sync_status_id);
CREATE INDEX IF NOT EXISTS idx_sync_discrepancy_cert_id ON sync_discrepancy(certificate_id);
CREATE INDEX IF NOT EXISTS idx_sync_discrepancy_resolved ON sync_discrepancy(resolved);
CREATE INDEX IF NOT EXISTS idx_sync_discrepancy_issue_type ON sync_discrepancy(issue_type);
CREATE INDEX IF NOT EXISTS idx_sync_discrepancy_detected_at ON sync_discrepancy(detected_at DESC);

-- =============================================================================
-- Sync Configuration Table
-- =============================================================================

CREATE TABLE IF NOT EXISTS sync_config (
    key VARCHAR(100) PRIMARY KEY,
    value TEXT NOT NULL,
    description TEXT,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Insert default configuration
INSERT INTO sync_config (key, value, description) VALUES
    ('sync_interval_minutes', '5', 'Interval between automatic sync checks'),
    ('auto_reconcile_enabled', 'true', 'Automatically fix MISSING_IN_LDAP issues'),
    ('max_reconcile_batch_size', '100', 'Maximum items to reconcile in one batch'),
    ('alert_threshold', '10', 'Number of discrepancies to trigger alert'),
    ('retention_days', '30', 'Days to keep sync history')
ON CONFLICT (key) DO NOTHING;

-- =============================================================================
-- Views
-- =============================================================================

-- Latest sync status view
CREATE OR REPLACE VIEW v_latest_sync_status AS
SELECT * FROM sync_status
ORDER BY checked_at DESC
LIMIT 1;

-- Unresolved discrepancies view
CREATE OR REPLACE VIEW v_unresolved_discrepancies AS
SELECT
    sd.*,
    ss.checked_at as sync_checked_at
FROM sync_discrepancy sd
LEFT JOIN sync_status ss ON sd.sync_status_id = ss.id
WHERE sd.resolved = FALSE
ORDER BY sd.detected_at DESC;

-- Sync statistics view (last 7 days)
CREATE OR REPLACE VIEW v_sync_statistics AS
SELECT
    DATE_TRUNC('day', checked_at) as date,
    COUNT(*) as check_count,
    SUM(CASE WHEN status = 'SYNCED' THEN 1 ELSE 0 END) as synced_count,
    SUM(CASE WHEN status = 'DISCREPANCY' THEN 1 ELSE 0 END) as discrepancy_count,
    SUM(CASE WHEN status = 'ERROR' THEN 1 ELSE 0 END) as error_count,
    AVG(total_discrepancy) as avg_discrepancy,
    AVG(check_duration_ms) as avg_check_duration_ms
FROM sync_status
WHERE checked_at >= NOW() - INTERVAL '7 days'
GROUP BY DATE_TRUNC('day', checked_at)
ORDER BY date DESC;

-- =============================================================================
-- Functions
-- =============================================================================

-- Function to get sync config value
CREATE OR REPLACE FUNCTION get_sync_config(config_key VARCHAR)
RETURNS TEXT AS $$
DECLARE
    config_value TEXT;
BEGIN
    SELECT value INTO config_value FROM sync_config WHERE key = config_key;
    RETURN config_value;
END;
$$ LANGUAGE plpgsql;

-- Function to clean up old sync history
CREATE OR REPLACE FUNCTION cleanup_sync_history()
RETURNS INTEGER AS $$
DECLARE
    retention_days INTEGER;
    deleted_count INTEGER;
BEGIN
    SELECT COALESCE(value::INTEGER, 30) INTO retention_days
    FROM sync_config WHERE key = 'retention_days';

    DELETE FROM sync_status
    WHERE checked_at < NOW() - (retention_days || ' days')::INTERVAL;

    GET DIAGNOSTICS deleted_count = ROW_COUNT;
    RETURN deleted_count;
END;
$$ LANGUAGE plpgsql;

-- =============================================================================
-- Comments
-- =============================================================================

COMMENT ON TABLE sync_status IS 'Periodic sync check results between PostgreSQL and LDAP';
COMMENT ON TABLE sync_discrepancy IS 'Individual mismatches found during sync checks';
COMMENT ON TABLE sync_config IS 'Configuration for sync service';
COMMENT ON COLUMN sync_status.db_stored_in_ldap_count IS 'Certificates marked as stored_in_ldap=TRUE in DB';
COMMENT ON COLUMN sync_discrepancy.issue_type IS 'MISSING_IN_LDAP: DB has but LDAP missing, MISSING_IN_DB: LDAP has but DB missing, MISMATCH: both exist but differ';

-- =============================================================================
-- ICAO PKD LDAP Sync Log Table (v2.39.0)
-- =============================================================================
-- Tracks automatic synchronization with ICAO PKD LDAP (simulation/production)
-- =============================================================================

CREATE TABLE IF NOT EXISTS icao_ldap_sync_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    sync_type VARCHAR(50) NOT NULL,        -- FULL, INCREMENTAL
    status VARCHAR(50) NOT NULL,           -- RUNNING, COMPLETED, FAILED
    triggered_by VARCHAR(100) NOT NULL,    -- MANUAL, SCHEDULED
    total_remote_count INT DEFAULT 0,
    new_certificates INT DEFAULT 0,
    updated_certificates INT DEFAULT 0,
    failed_count INT DEFAULT 0,
    duration_ms INT DEFAULT 0,
    error_message TEXT,
    started_at TIMESTAMPTZ DEFAULT NOW(),
    completed_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_icao_ldap_sync_log_created
    ON icao_ldap_sync_log (created_at DESC);

CREATE INDEX IF NOT EXISTS idx_icao_ldap_sync_log_status
    ON icao_ldap_sync_log (status);

-- Add source_type 'ICAO_PKD_SYNC' to valid values if needed
-- (certificate.source_type is VARCHAR, no enum constraint)

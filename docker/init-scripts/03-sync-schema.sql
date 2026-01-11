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
    checked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

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

    -- Discrepancy counts (computed columns compatible with sync-service INSERT)
    csca_discrepancy INTEGER NOT NULL DEFAULT 0,
    dsc_discrepancy INTEGER NOT NULL DEFAULT 0,
    dsc_nc_discrepancy INTEGER NOT NULL DEFAULT 0,
    crl_discrepancy INTEGER NOT NULL DEFAULT 0,
    total_discrepancy INTEGER NOT NULL DEFAULT 0,

    -- Country breakdown (JSON for flexibility)
    db_country_stats JSONB,   -- {"KOR": {"csca": 5, "dsc": 100}, ...}
    ldap_country_stats JSONB, -- {"KOR": {"csca": 5, "dsc": 100}, ...}

    -- Overall status
    status VARCHAR(20) NOT NULL DEFAULT 'UNKNOWN',  -- SYNCED, DISCREPANCY, ERROR, UNKNOWN
    error_message TEXT,

    -- Timing
    check_duration_ms INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_sync_status_checked_at ON sync_status(checked_at DESC);
CREATE INDEX IF NOT EXISTS idx_sync_status_status ON sync_status(status);

-- =============================================================================
-- Comments
-- =============================================================================

COMMENT ON TABLE sync_status IS 'Periodic sync check results between PostgreSQL and LDAP';
COMMENT ON COLUMN sync_status.db_stored_in_ldap_count IS 'Certificates marked as stored_in_ldap=TRUE in DB';

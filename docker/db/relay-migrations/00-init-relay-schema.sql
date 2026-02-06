-- =============================================================================
-- ICAO Local PKD - PKD Relay Service PostgreSQL Schema
-- =============================================================================
-- Version: 2.5.0 (Phase 5.3)
-- Created: 2026-02-05
-- Description: PostgreSQL schema for PKD Relay Service (sync & reconciliation)
-- Database: localpkd_relay
-- User: pkd_relay
-- =============================================================================

-- =============================================================================
-- Certificate Table (for PKD Relay sync operations)
-- =============================================================================

CREATE TABLE IF NOT EXISTS certificate (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    fingerprint_sha256 VARCHAR(64) NOT NULL UNIQUE,
    certificate_type VARCHAR(20) NOT NULL,
    country_code VARCHAR(3) NOT NULL,
    subject_dn TEXT NOT NULL,
    issuer_dn TEXT NOT NULL,
    serial_number VARCHAR(100) NOT NULL,
    not_before TIMESTAMP,
    not_after TIMESTAMP,
    certificate_data BYTEA NOT NULL,

    -- LDAP storage tracking
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT NOW(),

    CONSTRAINT chk_cert_type CHECK (certificate_type IN ('CSCA', 'DSC', 'DSC_NC', 'MLSC', 'LINK_CERT'))
);

CREATE INDEX IF NOT EXISTS idx_cert_fingerprint ON certificate(fingerprint_sha256);
CREATE INDEX IF NOT EXISTS idx_cert_type ON certificate(certificate_type);
CREATE INDEX IF NOT EXISTS idx_cert_country ON certificate(country_code);
CREATE INDEX IF NOT EXISTS idx_cert_stored_ldap ON certificate(stored_in_ldap);

-- =============================================================================
-- CRL Table (for PKD Relay sync operations)
-- =============================================================================

CREATE TABLE IF NOT EXISTS crl (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    fingerprint_sha256 VARCHAR(64) NOT NULL UNIQUE,
    country_code VARCHAR(3) NOT NULL,
    issuer_dn TEXT NOT NULL,
    this_update TIMESTAMP NOT NULL,
    next_update TIMESTAMP,
    crl_data BYTEA NOT NULL,

    -- LDAP storage tracking
    stored_in_ldap BOOLEAN DEFAULT FALSE,
    stored_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_crl_fingerprint ON crl(fingerprint_sha256);
CREATE INDEX IF NOT EXISTS idx_crl_country ON crl(country_code);
CREATE INDEX IF NOT EXISTS idx_crl_stored_ldap ON crl(stored_in_ldap);

-- =============================================================================
-- Sync Status Table
-- =============================================================================

CREATE TABLE IF NOT EXISTS sync_status (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT NOW(),

    -- Database counts
    db_csca_count INTEGER DEFAULT 0,
    db_mlsc_count INTEGER DEFAULT 0,
    db_dsc_count INTEGER DEFAULT 0,
    db_dsc_nc_count INTEGER DEFAULT 0,
    db_crl_count INTEGER DEFAULT 0,

    -- LDAP counts
    ldap_csca_count INTEGER DEFAULT 0,
    ldap_mlsc_count INTEGER DEFAULT 0,
    ldap_dsc_count INTEGER DEFAULT 0,
    ldap_dsc_nc_count INTEGER DEFAULT 0,
    ldap_crl_count INTEGER DEFAULT 0,

    -- Discrepancies
    csca_discrepancy INTEGER DEFAULT 0,
    mlsc_discrepancy INTEGER DEFAULT 0,
    dsc_discrepancy INTEGER DEFAULT 0,
    dsc_nc_discrepancy INTEGER DEFAULT 0,
    crl_discrepancy INTEGER DEFAULT 0,
    total_discrepancy INTEGER DEFAULT 0,

    -- Country-level statistics (JSON format)
    country_stats JSONB
);

CREATE INDEX IF NOT EXISTS idx_sync_status_checked_at ON sync_status(checked_at DESC);

-- =============================================================================
-- Reconciliation Summary Table
-- =============================================================================

CREATE TABLE IF NOT EXISTS reconciliation_summary (
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

    -- Optional link to sync_status
    sync_status_id UUID,

    CONSTRAINT chk_recon_status CHECK (status IN ('PENDING', 'IN_PROGRESS', 'COMPLETED', 'FAILED'))
);

CREATE INDEX IF NOT EXISTS idx_recon_summary_triggered ON reconciliation_summary(triggered_at DESC);

-- =============================================================================
-- Reconciliation Log Table (detailed per-certificate)
-- =============================================================================

CREATE TABLE IF NOT EXISTS reconciliation_log (
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

CREATE INDEX IF NOT EXISTS idx_recon_log_recon_id ON reconciliation_log(reconciliation_id);
CREATE INDEX IF NOT EXISTS idx_recon_log_fingerprint ON reconciliation_log(cert_fingerprint);
CREATE INDEX IF NOT EXISTS idx_recon_log_created ON reconciliation_log(created_at DESC);

-- =============================================================================
-- Success Message
-- =============================================================================
SELECT 'PostgreSQL PKD Relay schema initialized successfully' AS status;

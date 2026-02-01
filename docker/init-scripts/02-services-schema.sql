-- =============================================================================
-- ICAO Local PKD - Services Schema
-- =============================================================================
-- Version: 2.0.0
-- Created: 2026-01-25
-- Description: Schema for Sync, Monitoring, and Reconciliation services
-- =============================================================================

-- =============================================================================
-- DB-LDAP Sync Service
-- =============================================================================

-- Sync status tracking (periodic sync check results)
CREATE TABLE IF NOT EXISTS sync_status (
    id SERIAL PRIMARY KEY,
    checked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    -- DB statistics
    db_csca_count INTEGER NOT NULL DEFAULT 0,
    db_dsc_count INTEGER NOT NULL DEFAULT 0,
    db_dsc_nc_count INTEGER NOT NULL DEFAULT 0,
    db_crl_count INTEGER NOT NULL DEFAULT 0,
    db_stored_in_ldap_count INTEGER NOT NULL DEFAULT 0,

    -- LDAP statistics
    ldap_csca_count INTEGER NOT NULL DEFAULT 0,
    ldap_dsc_count INTEGER NOT NULL DEFAULT 0,
    ldap_dsc_nc_count INTEGER NOT NULL DEFAULT 0,
    ldap_crl_count INTEGER NOT NULL DEFAULT 0,
    ldap_total_entries INTEGER NOT NULL DEFAULT 0,

    -- Discrepancy counts
    csca_discrepancy INTEGER NOT NULL DEFAULT 0,
    dsc_discrepancy INTEGER NOT NULL DEFAULT 0,
    dsc_nc_discrepancy INTEGER NOT NULL DEFAULT 0,
    crl_discrepancy INTEGER NOT NULL DEFAULT 0,
    total_discrepancy INTEGER NOT NULL DEFAULT 0,

    -- Country-level statistics (JSONB)
    db_country_stats JSONB,
    ldap_country_stats JSONB,

    -- Overall status
    status VARCHAR(20) NOT NULL DEFAULT 'UNKNOWN',
    error_message TEXT,
    check_duration_ms INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_sync_status_checked_at ON sync_status(checked_at);
CREATE INDEX idx_sync_status_status ON sync_status(status);

-- =============================================================================
-- Monitoring Service
-- =============================================================================

-- System metrics tracking
CREATE TABLE IF NOT EXISTS system_metrics (
    id SERIAL PRIMARY KEY,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    metric_name VARCHAR(100) NOT NULL,
    metric_value NUMERIC NOT NULL,
    metric_unit VARCHAR(20),
    service_name VARCHAR(50),
    tags JSONB
);

CREATE INDEX idx_system_metrics_timestamp ON system_metrics(timestamp);
CREATE INDEX idx_system_metrics_name ON system_metrics(metric_name);
CREATE INDEX idx_system_metrics_service ON system_metrics(service_name);

-- Service health status
CREATE TABLE IF NOT EXISTS service_health (
    id SERIAL PRIMARY KEY,
    service_name VARCHAR(50) NOT NULL UNIQUE,
    status VARCHAR(20) NOT NULL DEFAULT 'UNKNOWN',
    last_check_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    error_message TEXT,
    uptime_seconds BIGINT,
    metadata JSONB
);

CREATE INDEX idx_service_health_name ON service_health(service_name);
CREATE INDEX idx_service_health_status ON service_health(status);

-- =============================================================================
-- Auto Reconciliation Service
-- =============================================================================

-- Reconciliation summary (high-level reconciliation results)
CREATE TABLE IF NOT EXISTS reconciliation_summary (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    sync_status_id INTEGER REFERENCES sync_status(id) ON DELETE SET NULL,
    triggered_by VARCHAR(20) NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'IN_PROGRESS',

    -- Execution details
    started_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMP,
    duration_ms INTEGER,

    -- Results
    total_processed INTEGER NOT NULL DEFAULT 0,
    total_success INTEGER NOT NULL DEFAULT 0,
    total_failed INTEGER NOT NULL DEFAULT 0,
    csca_added INTEGER NOT NULL DEFAULT 0,
    dsc_added INTEGER NOT NULL DEFAULT 0,
    dsc_nc_added INTEGER NOT NULL DEFAULT 0,
    crl_added INTEGER NOT NULL DEFAULT 0,

    error_message TEXT,
    metadata JSONB
);

CREATE INDEX idx_recon_summary_triggered_by ON reconciliation_summary(triggered_by);
CREATE INDEX idx_recon_summary_status ON reconciliation_summary(status);
CREATE INDEX idx_recon_summary_started_at ON reconciliation_summary(started_at);
CREATE INDEX idx_recon_summary_sync_status ON reconciliation_summary(sync_status_id);

-- Reconciliation detailed logs (per-operation logs)
CREATE TABLE IF NOT EXISTS reconciliation_log (
    id SERIAL PRIMARY KEY,
    summary_id UUID NOT NULL REFERENCES reconciliation_summary(id) ON DELETE CASCADE,
    operation VARCHAR(20) NOT NULL,
    certificate_type VARCHAR(10),
    country_code VARCHAR(3),
    subject_dn TEXT,
    fingerprint_sha256 VARCHAR(64),
    status VARCHAR(20) NOT NULL,
    error_message TEXT,
    started_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMP,
    duration_ms INTEGER
);

CREATE INDEX idx_recon_log_summary_id ON reconciliation_log(summary_id);
CREATE INDEX idx_recon_log_operation ON reconciliation_log(operation);
CREATE INDEX idx_recon_log_status ON reconciliation_log(status);
CREATE INDEX idx_recon_log_started_at ON reconciliation_log(started_at);

-- =============================================================================
-- Missing Tables: revalidation_history, sync_config, crl_revocation_log
-- =============================================================================
-- Date: 2026-02-27
-- Version: v2.23.1
-- Purpose: Add tables referenced by PKD Relay and PKD Management but missing
--          from init scripts (previously created manually or via runtime)
-- =============================================================================

-- =============================================================================
-- Revalidation History (certificate expiration re-checks)
-- =============================================================================

CREATE TABLE IF NOT EXISTS revalidation_history (
    id SERIAL PRIMARY KEY,
    executed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    total_processed INTEGER DEFAULT 0 NOT NULL,
    newly_expired INTEGER DEFAULT 0 NOT NULL,
    newly_valid INTEGER DEFAULT 0 NOT NULL,
    unchanged INTEGER DEFAULT 0 NOT NULL,
    errors INTEGER DEFAULT 0 NOT NULL,
    duration_ms INTEGER DEFAULT 0 NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_reval_history_executed ON revalidation_history(executed_at DESC);

-- =============================================================================
-- Sync Configuration (daily sync settings)
-- =============================================================================

CREATE TABLE IF NOT EXISTS sync_config (
    id INTEGER PRIMARY KEY,
    daily_sync_enabled BOOLEAN DEFAULT TRUE NOT NULL,
    daily_sync_hour INTEGER DEFAULT 0 NOT NULL,
    daily_sync_minute INTEGER DEFAULT 0 NOT NULL,
    auto_reconcile BOOLEAN DEFAULT FALSE NOT NULL,
    revalidate_certs_on_sync BOOLEAN DEFAULT TRUE NOT NULL,
    max_reconcile_batch_size INTEGER DEFAULT 100 NOT NULL,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_by VARCHAR(100),
    CONSTRAINT single_config_row CHECK (id = 1)
);

-- Insert default config row (if not exists)
INSERT INTO sync_config (id, daily_sync_enabled, daily_sync_hour, daily_sync_minute,
    auto_reconcile, revalidate_certs_on_sync, max_reconcile_batch_size)
VALUES (1, TRUE, 0, 0, FALSE, TRUE, 100)
ON CONFLICT (id) DO NOTHING;

-- =============================================================================
-- CRL Revocation Log (CRL check audit trail)
-- =============================================================================

CREATE TABLE IF NOT EXISTS crl_revocation_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    certificate_id VARCHAR(128) NOT NULL,
    certificate_type VARCHAR(20) NOT NULL,
    serial_number VARCHAR(100),
    fingerprint_sha256 VARCHAR(64),
    subject_dn TEXT,
    revocation_status VARCHAR(20),
    revocation_reason VARCHAR(50),
    revocation_date VARCHAR(50),
    crl_id VARCHAR(128),
    crl_issuer_dn TEXT,
    crl_this_update VARCHAR(50),
    crl_next_update VARCHAR(50),
    checked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    check_duration_ms INTEGER
);

CREATE INDEX IF NOT EXISTS idx_crl_revlog_cert_id ON crl_revocation_log(certificate_id);
CREATE INDEX IF NOT EXISTS idx_crl_revlog_fingerprint ON crl_revocation_log(fingerprint_sha256);
CREATE INDEX IF NOT EXISTS idx_crl_revlog_checked_at ON crl_revocation_log(checked_at DESC);

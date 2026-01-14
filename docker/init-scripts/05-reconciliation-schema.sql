-- =============================================================================
-- ICAO Local PKD - Reconciliation Schema
-- =============================================================================
-- Version: 1.0
-- Created: 2026-01-14
-- Description: Auto reconciliation history and detailed logs
-- =============================================================================

-- =============================================================================
-- Reconciliation Summary Table (high-level reconciliation results)
-- =============================================================================

CREATE TABLE IF NOT EXISTS reconciliation_summary (
    id SERIAL PRIMARY KEY,
    started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMP,

    -- Execution metadata
    triggered_by VARCHAR(50) NOT NULL DEFAULT 'MANUAL',  -- MANUAL, AUTO, DAILY_SYNC
    dry_run BOOLEAN NOT NULL DEFAULT FALSE,

    -- Result summary
    status VARCHAR(20) NOT NULL DEFAULT 'IN_PROGRESS',  -- IN_PROGRESS, COMPLETED, FAILED, ABORTED
    total_processed INTEGER NOT NULL DEFAULT 0,
    success_count INTEGER NOT NULL DEFAULT 0,
    failed_count INTEGER NOT NULL DEFAULT 0,

    -- Certificate type breakdown
    csca_added INTEGER NOT NULL DEFAULT 0,
    csca_deleted INTEGER NOT NULL DEFAULT 0,
    dsc_added INTEGER NOT NULL DEFAULT 0,
    dsc_deleted INTEGER NOT NULL DEFAULT 0,
    dsc_nc_added INTEGER NOT NULL DEFAULT 0,
    dsc_nc_deleted INTEGER NOT NULL DEFAULT 0,
    crl_added INTEGER NOT NULL DEFAULT 0,
    crl_deleted INTEGER NOT NULL DEFAULT 0,

    -- Timing and error info
    duration_ms INTEGER NOT NULL DEFAULT 0,
    error_message TEXT,

    -- Optional: Link to sync_status that triggered this reconciliation
    sync_status_id INTEGER REFERENCES sync_status(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_reconciliation_summary_started_at ON reconciliation_summary(started_at DESC);
CREATE INDEX IF NOT EXISTS idx_reconciliation_summary_status ON reconciliation_summary(status);
CREATE INDEX IF NOT EXISTS idx_reconciliation_summary_triggered_by ON reconciliation_summary(triggered_by);
CREATE INDEX IF NOT EXISTS idx_reconciliation_summary_sync_status ON reconciliation_summary(sync_status_id);

COMMENT ON TABLE reconciliation_summary IS 'High-level reconciliation execution results and statistics';
COMMENT ON COLUMN reconciliation_summary.triggered_by IS 'Source that triggered reconciliation: MANUAL (API call), AUTO (auto_reconcile), DAILY_SYNC';
COMMENT ON COLUMN reconciliation_summary.dry_run IS 'If TRUE, no actual changes were made (simulation mode)';
COMMENT ON COLUMN reconciliation_summary.sync_status_id IS 'References the sync_status check that detected discrepancies';

-- =============================================================================
-- Reconciliation Log Table (detailed operation logs)
-- =============================================================================

CREATE TABLE IF NOT EXISTS reconciliation_log (
    id SERIAL PRIMARY KEY,
    reconciliation_id INTEGER NOT NULL REFERENCES reconciliation_summary(id) ON DELETE CASCADE,
    timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    -- Operation details
    operation VARCHAR(20) NOT NULL,  -- ADD, DELETE, UPDATE, SKIP
    cert_type VARCHAR(20) NOT NULL,  -- CSCA, DSC, DSC_NC, CRL
    cert_id INTEGER,                 -- References certificate.id or crl.id

    -- Certificate identification
    country_code VARCHAR(3),
    subject TEXT,
    issuer TEXT,
    ldap_dn TEXT,

    -- Result
    status VARCHAR(20) NOT NULL,     -- SUCCESS, FAILED, SKIPPED
    error_message TEXT,

    -- Timing
    duration_ms INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_reconciliation_log_reconciliation_id ON reconciliation_log(reconciliation_id);
CREATE INDEX IF NOT EXISTS idx_reconciliation_log_timestamp ON reconciliation_log(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_reconciliation_log_status ON reconciliation_log(status);
CREATE INDEX IF NOT EXISTS idx_reconciliation_log_operation ON reconciliation_log(operation);
CREATE INDEX IF NOT EXISTS idx_reconciliation_log_cert_type ON reconciliation_log(cert_type);
CREATE INDEX IF NOT EXISTS idx_reconciliation_log_country ON reconciliation_log(country_code);

COMMENT ON TABLE reconciliation_log IS 'Detailed per-operation logs for reconciliation executions';
COMMENT ON COLUMN reconciliation_log.operation IS 'Operation performed: ADD (new to LDAP), DELETE (remove from LDAP), SKIP (already synced)';
COMMENT ON COLUMN reconciliation_log.cert_id IS 'Foreign key to certificate or CRL table (not enforced for flexibility)';
COMMENT ON COLUMN reconciliation_log.ldap_dn IS 'LDAP Distinguished Name where operation was performed';

-- =============================================================================
-- Sample Query Examples
-- =============================================================================

COMMENT ON TABLE reconciliation_summary IS
$DOC$
High-level reconciliation execution results and statistics

Example queries:
1. Get recent reconciliations:
   SELECT * FROM reconciliation_summary ORDER BY started_at DESC LIMIT 10;

2. Get failed reconciliation details:
   SELECT rs.*, COUNT(rl.id) as log_entries
   FROM reconciliation_summary rs
   LEFT JOIN reconciliation_log rl ON rs.id = rl.reconciliation_id
   WHERE rs.status = 'FAILED'
   GROUP BY rs.id;

3. Get reconciliation triggered by specific sync check:
   SELECT * FROM reconciliation_summary WHERE sync_status_id = 123;
$DOC$;

COMMENT ON TABLE reconciliation_log IS
$DOC$
Detailed per-operation logs for reconciliation executions

Example queries:
1. Get all operations for a reconciliation:
   SELECT * FROM reconciliation_log WHERE reconciliation_id = 1 ORDER BY timestamp;

2. Get failed operations with details:
   SELECT cert_type, operation, country_code, subject, error_message
   FROM reconciliation_log
   WHERE reconciliation_id = 1 AND status = 'FAILED';

3. Get statistics by country:
   SELECT country_code, operation, cert_type, COUNT(*) as count
   FROM reconciliation_log
   WHERE reconciliation_id = 1
   GROUP BY country_code, operation, cert_type
   ORDER BY country_code, cert_type;
$DOC$;
